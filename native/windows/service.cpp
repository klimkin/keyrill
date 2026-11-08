#include "service.h"
#include "keys.h"
#include <algorithm>
#include <cwctype>
#include <exception>
#include <inputscope.h>
#include <limits>
#include <new>
#include <richedit.h>

namespace keyrill {
using diagnostics::Cause;
using diagnostics::Counter;
using diagnostics::Stage;
long object_count = 0;

bool ProofWindow(HWND window) {
  DWORD process = 0;
  if (!window || GetWindowThreadProcessId(window, &process) != GetCurrentThreadId() ||
      process != GetCurrentProcessId() || !IsWindowEnabled(window))
    return false;
  wchar_t name[64]{};
  if (!GetClassNameW(window, name, 64) || _wcsicmp(name, MSFTEDIT_CLASS) != 0)
    return false;
  LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  if (style & (ES_PASSWORD | ES_READONLY | WS_DISABLED))
    return false;
  int id = GetDlgCtrlID(window);
  if (id != 101 && id != 102)
    return false;
  HWND root = GetAncestor(window, GA_ROOT);
  if (!GetClassNameW(root, name, 64) || wcscmp(name, fixture_class) != 0)
    return false;
  wchar_t path[32768]{};
  DWORD count = GetModuleFileNameW(nullptr, path, 32768);
  if (!count || count >= 32768)
    return false;
  const wchar_t *base = wcsrchr(path, L'\\');
  return base && _wcsicmp(base + 1, L"KeyrillProofEditor.exe") == 0;
}

static const Profile *ActiveProfile(const TF_INPUTPROCESSORPROFILE &profile, uint32_t mode) {
  if (profile.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR || profile.clsid != service_id ||
      !(profile.dwFlags & TF_IPP_FLAG_ACTIVE))
    return nullptr;
  for (const auto &entry : profiles)
    if (entry.id == profile.guidProfile && entry.language == profile.langid && entry.mode == mode)
      return &entry;
  return nullptr;
}
HKL ProfileLayout(const TF_INPUTPROCESSORPROFILE &profile, uint32_t mode, HKL substitute) {
  const auto *entry = ActiveProfile(profile, mode);
  if (entry && reinterpret_cast<ULONG_PTR>(substitute) ==
                   (static_cast<ULONG_PTR>(entry->language) << 16 | entry->language))
    return substitute;
  return nullptr;
}

class EditSession final : public ITfEditSession {
public:
  EditSession(Service &owner, ITfContext *context, kr_event event)
      : owner_keep_(&owner), owner_(owner), generation_(owner.generation_), event_(event) {
    context_ = context;
    if (owner_.Current(generation_) && context_) {
      HRESULT result = context_->QueryInterface(IID_PPV_ARGS(&context_id_));
      if (FAILED(result))
        owner_.Refuse(Stage::Session, Cause::Native, result);
      else if (!context_id_)
        owner_.Refuse(Stage::Session, Cause::Missing);
    }
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfEditSession)
      return E_NOINTERFACE;
    *out = static_cast<ITfEditSession *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG n = --references_;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
    if (entered_ || !Alive()) {
      owner_.Refuse(Stage::Session, entered_ ? Cause::Reentry : Cause::Stale);
      return E_FAIL;
    }
    entered_ = true;
    diagnostics::Count(owner_.diagnostics_, Counter::SessionEntered);
    try {
      Run(cookie);
    } catch (...) {
      owner_.Refuse(stage_, Cause::Exception);
      if (wrote_ && !outcome_recorded_)
        diagnostics::Count(owner_.diagnostics_, Counter::Unknown);
      owner_.Invalidate(true);
      eaten = UnknownDisposition();
    }
    completed = true;
    diagnostics::Count(owner_.diagnostics_, Counter::SessionCompleted);
    return S_OK;
  }
  bool completed = false;
  bool eaten = false;
  bool attempted() const { return wrote_; }
  bool valid() const { return Alive(); }
  void Expire() { expired_ = true; }
  bool UnknownDisposition() const {
    return wrote_ && (last_action_ == KR_MARK || last_disposition_ == KR_KEY_CONSUMED);
  }

private:
  ~EditSession() = default;
  bool Alive() const { return !expired_ && context_id_ && owner_.Current(generation_); }
  struct Observe {
    EditSession &session;
    Stage previous;
    int exceptions = std::uncaught_exceptions();
    Observe(EditSession &value, Stage stage) : session(value), previous(value.stage_) {
      session.stage_ = stage;
    }
    ~Observe() {
      if (std::uncaught_exceptions() == exceptions)
        session.stage_ = previous;
    }
  };
  bool Check(bool value, Cause cause = Cause::Predicate) {
    if (!value)
      owner_.Refuse(stage_, cause);
    return value;
  }
  bool Good(HRESULT result) {
    bool good = SUCCEEDED(result) && Alive();
    if (!good)
      owner_.Refuse(stage_, FAILED(result) ? Cause::Native : Cause::Stale,
                    FAILED(result) ? result : S_OK);
    return good;
  }
  bool Exact(HRESULT result) {
    if (!Good(result))
      return false;
    if (result != S_OK) {
      owner_.Refuse(stage_, Cause::Native, result);
      return false;
    }
    return true;
  }
  bool Identity(IUnknown *object, IUnknown *expected) {
    Observe observe(*this, Stage::Context);
    ComPtr<IUnknown> identity;
    if (!Check(object != nullptr, Cause::Missing) ||
        !Good(object->QueryInterface(IID_PPV_ARGS(&identity))))
      return false;
    return Check(identity.Get() == expected && Alive());
  }
  bool Focused() {
    Observe observe(*this, Stage::Focus);
    ComPtr<ITfThreadMgr> manager = owner_.manager_;
    if (!Check(Alive(), Cause::Stale) || !Check(manager != nullptr, Cause::Missing))
      return false;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> top;
    ComPtr<ITfContextView> view;
    HWND window = nullptr;
    if (!Good(manager->GetFocus(&document)) || !Check(document != nullptr, Cause::Missing) ||
        !Good(document->GetTop(&top)) || !Identity(top.Get(), context_id_.Get()) ||
        !Good(context_->GetActiveView(&view)) || !Check(view != nullptr, Cause::Missing) ||
        !Good(view->GetWnd(&window)))
      return false;
    if (!Check(window && ::GetFocus() == window))
      return false;
    stage_ = Stage::ProofWindow;
    return Check(ProofWindow(window) && Alive());
  }
  bool Extent(ITfRange *range, LONG &start, LONG &length) {
    Observe observe(*this, Stage::Context);
    ComPtr<ITfContext> context;
    if (!Good(range->GetContext(&context)) || !Identity(context.Get(), context_id_.Get()))
      return false;
    ComPtr<ITfRangeACP> acp;
    if (!Good(range->QueryInterface(IID_PPV_ARGS(&acp))) || !Good(acp->GetExtent(&start, &length)))
      return false;
    return Check(start >= 0 && length >= 0 && start <= std::numeric_limits<LONG>::max() - length);
  }
  bool Selection(TfEditCookie cookie, ComPtr<ITfRange> &range, LONG &caret, bool collapsed = true) {
    Observe observe(*this, Stage::Selection);
    TF_SELECTION selection{};
    ULONG fetched = 0;
    HRESULT result = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
    range.Attach(selection.range);
    if (!Good(result) || !Check(fetched == 1 && range && !selection.style.fInterimChar))
      return false;
    LONG length = 0;
    return Extent(range.Get(), caret, length) && Check(!collapsed || length == 0);
  }
  bool Disabled(IUnknown *target, REFGUID id, Stage stage) {
    Observe observe(*this, stage);
    ComPtr<ITfCompartmentMgr> compartments;
    if (!Check(target != nullptr, Cause::Missing) ||
        !Good(target->QueryInterface(IID_PPV_ARGS(&compartments))))
      return true;
    ComPtr<ITfCompartment> compartment;
    if (!Good(compartments->GetCompartment(id, &compartment)) ||
        !Check(compartment != nullptr, Cause::Missing))
      return true;
    VARIANT value;
    VariantInit(&value);
    HRESULT result = compartment->GetValue(&value);
    bool disabled = !Good(result) || !Check((value.vt == VT_EMPTY || value.vt == VT_I4) &&
                                            (value.vt != VT_I4 || value.lVal == 0));
    VariantClear(&value);
    return disabled || !Check(Alive(), Cause::Stale);
  }
  bool Admit(TfEditCookie cookie, ITfRange *selection) {
    Observe observe(*this, Stage::Focus);
    ComPtr<ITfContextView> view;
    HWND window = nullptr;
    if (!Focused() || !Good(context_->GetActiveView(&view)) ||
        !Check(view != nullptr, Cause::Missing) || !Good(view->GetWnd(&window)) ||
        !Check(ProofWindow(window) && Alive()))
      return false;
    stage_ = Stage::Status;
    TF_STATUS status{};
    ComPtr<ITfThreadMgr> manager = owner_.manager_;
    if (!Good(context_->GetStatus(&status)) || !Check(!(status.dwDynamicFlags & TF_SD_READONLY)) ||
        Disabled(context_.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED, Stage::ContextDisabled) ||
        Disabled(context_.Get(), GUID_COMPARTMENT_EMPTYCONTEXT, Stage::EmptyContext) ||
        Disabled(manager.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED, Stage::ThreadDisabled))
      return false;
    stage_ = Stage::AppProperty;
    ComPtr<ITfReadOnlyProperty> property;
    HRESULT result = context_->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);
    if (!Exact(result) || !Check(property != nullptr, Cause::Missing))
      return false;
    VARIANT value;
    VariantInit(&value);
    stage_ = Stage::AppValue;
    result = property->GetValue(cookie, selection, &value);
    if (!Exact(result) || !Check(value.vt == VT_UNKNOWN, Cause::WrongType) ||
        !Check(value.punkVal != nullptr, Cause::Missing)) {
      VariantClear(&value);
      return false;
    }
    stage_ = Stage::ScopeInterface;
    ComPtr<ITfInputScope> scope;
    result = value.punkVal->QueryInterface(IID_PPV_ARGS(&scope));
    VariantClear(&value);
    if (!Good(result) || !Check(scope != nullptr, Cause::Missing))
      return false;
    stage_ = Stage::Scopes;
    InputScope *scopes = nullptr;
    UINT count = 0;
    result = scope->GetInputScopes(&scopes, &count);
    bool ordinary = Exact(result) && Check(count == 1 && scopes &&
                                           (scopes[0] == IS_DEFAULT || scopes[0] == IS_TEXT));
    CoTaskMemFree(scopes);
    return ordinary && Check(Alive(), Cause::Stale);
  }
  bool NoForeign(TfEditCookie, ITfRange *, ComPtr<ITfContextComposition> &cc) {
    Observe observe(*this, Stage::Foreign);
    if (!Good(context_->QueryInterface(IID_PPV_ARGS(&cc))))
      return false;
    ComPtr<IEnumITfCompositionView> enumerator;
    if (!Good(cc->EnumCompositions(&enumerator)) || !Check(enumerator != nullptr, Cause::Missing))
      return false;
    ComPtr<ITfCompositionView> view;
    ULONG fetched = 0;
    HRESULT result = enumerator->Next(1, &view, &fetched);
    return Good(result) && Check(fetched == 0);
  }
  bool ActiveComposition(IUnknown *expected) {
    ComPtr<ITfContextComposition> cc;
    ComPtr<IEnumITfCompositionView> enumerator;
    ComPtr<ITfCompositionView> view;
    ULONG fetched = 0;
    if (!Good(context_->QueryInterface(IID_PPV_ARGS(&cc))) ||
        !Good(cc->EnumCompositions(&enumerator)) || !Check(enumerator != nullptr, Cause::Missing) ||
        !Good(enumerator->Next(1, &view, &fetched)) || fetched != 1 || !view ||
        !Identity(view.Get(), expected))
      return false;
    view.Reset();
    if (!Alive())
      return false;
    return Good(enumerator->Next(1, &view, &fetched)) && fetched == 0;
  }
  bool Read(TfEditCookie cookie, ITfRange *range, const std::wstring &expected) {
    wchar_t text[KR_TEXT_CAPACITY + 1]{};
    ULONG count = 0;
    return Good(range->GetText(cookie, 0, text, KR_TEXT_CAPACITY + 1, &count)) &&
           count == expected.size() && std::equal(expected.begin(), expected.end(), text);
  }
  bool OwnedRange(TfEditCookie cookie, const std::wstring &expected, bool caret,
                  ComPtr<ITfRange> &range) {
    if (!owner_.owned_ || owner_.owned_->context_id.Get() != context_id_.Get())
      return false;
    ComPtr<ITfComposition> composition = owner_.owned_->composition;
    if (!Alive() || !owner_.owned_)
      return false;
    ComPtr<IUnknown> identity = owner_.owned_->composition_id;
    if (!Alive() || !Identity(composition.Get(), identity.Get()) ||
        !ActiveComposition(identity.Get()) || !Good(composition->GetRange(&range)) || !range)
      return false;
    LONG start = 0, length = 0;
    if (!Extent(range.Get(), start, length) || !owner_.owned_ || start != owner_.owned_->start ||
        length != static_cast<LONG>(expected.size()) || !Read(cookie, range.Get(), expected))
      return false;
    if (caret) {
      ComPtr<ITfRange> selection;
      LONG position = 0;
      if (!Selection(cookie, selection, position) || position != start + length)
        return false;
    }
    return Alive();
  }
  bool Boundary(TfEditCookie cookie, ITfRange *selection, LONG caret) {
    Observe observe(*this, Stage::Boundary);
    if (!caret)
      return true;
    ComPtr<ITfRange> prefix;
    LONG shifted = 0;
    if (!Good(selection->Clone(&prefix)) ||
        !Good(prefix->ShiftStart(cookie, -1, &shifted, nullptr)) || !Check(shifted == -1))
      return false;
    LONG start = 0, length = 0;
    if (!Extent(prefix.Get(), start, length) || !Check(start == caret - 1 && length == 1))
      return false;
    wchar_t text[2]{};
    ULONG count = 0;
    return Good(prefix->GetText(cookie, 0, text, 2, &count)) &&
           Check(count == 1 && iswspace(text[0]));
  }
  bool Apply(TfEditCookie cookie, const kr_plan &plan) {
    Observe observe(*this, Stage::Apply);
    std::wstring expected(plan.expected, plan.expected + plan.expected_len);
    std::wstring desired(plan.mark, plan.mark + plan.mark_len);
    ComPtr<ITfRange> range;
    if (owner_.owned_) {
      if (expected != owner_.owned_->verified)
        return false;
      if (!OwnedRange(cookie, expected, plan.action != KR_FINISH || desired != expected, range))
        return false;
    } else {
      LONG caret = 0;
      if (plan.action != KR_MARK || !expected.empty() || !Selection(cookie, range, caret))
        return false;
      ComPtr<ITfContextComposition> cc;
      if (!NoForeign(cookie, range.Get(), cc))
        return false;
      ComPtr<ITfComposition> composition;
      if (!Focused() || !Good(cc->StartComposition(cookie, range.Get(), &owner_, &composition)) ||
          !composition)
        return false;
      ComPtr<IUnknown> identity;
      if (!Good(composition->QueryInterface(IID_PPV_ARGS(&identity))))
        return false;
      auto owned = std::make_unique<Service::Owned>();
      owned->context = context_;
      owned->context_id = context_id_;
      owned->composition = composition;
      owned->composition_id = identity;
      owned->start = caret;
      if (!Alive())
        return false;
      owned->diagnostic_serial = owner_.lifecycle_.Acquire();
      owner_.owned_ = std::move(owned);
      range.Reset();
      if (!Alive() || !OwnedRange(cookie, expected, true, range))
        return false;
    }
    if (!Focused())
      return false;
    if (plan.action != KR_FINISH || desired != expected) {
      diagnostics::Count(owner_.diagnostics_, Counter::Attempted);
      wrote_ = true;
      if (!Good(range->SetText(cookie, 0, desired.data(), static_cast<LONG>(desired.size()))))
        return false;
    }
    LONG start = 0, length = 0;
    if (!Extent(range.Get(), start, length) || !owner_.owned_ || start != owner_.owned_->start ||
        length != static_cast<LONG>(desired.size()) || !Read(cookie, range.Get(), desired))
      return false;
    ComPtr<ITfRange> actual;
    ComPtr<ITfComposition> composition = owner_.owned_->composition;
    if (!Alive() || !Good(composition->GetRange(&actual)) || !actual)
      return false;
    LONG actual_start = 0, actual_length = 0;
    if (!Extent(actual.Get(), actual_start, actual_length) || actual_start != start ||
        actual_length != length || !Read(cookie, actual.Get(), desired))
      return false;
    if (plan.action == KR_MARK || desired != expected) {
      ComPtr<ITfRange> caret;
      if (!Good(actual->Clone(&caret)) || !Good(caret->Collapse(cookie, TF_ANCHOR_END)))
        return false;
      TF_SELECTION selection{caret.Get(), {TF_AE_NONE, FALSE}};
      if (!Focused() || !Good(context_->SetSelection(cookie, 1, &selection)))
        return false;
      ComPtr<ITfRange> verified_caret;
      LONG position = 0;
      if (!Selection(cookie, verified_caret, position) || position != start + length)
        return false;
      if (!OwnedRange(cookie, desired, true, actual))
        return false;
    }
    if (plan.action == KR_MARK) {
      owner_.owned_->verified = desired;
    } else {
      if (!Focused())
        return false;
      diagnostics::Count(owner_.diagnostics_, Counter::Attempted);
      wrote_ = true;
      const auto serial = owner_.owned_ ? owner_.owned_->diagnostic_serial : 0;
      owner_.lifecycle_.Record(diagnostics::LifecycleKind::EndRequested, serial);
      if (!Good(composition->EndComposition(cookie)))
        return false;
      LONG finished_start = 0, finished_length = 0;
      if (!Extent(actual.Get(), finished_start, finished_length) || finished_start != start ||
          finished_length != length || !Read(cookie, actual.Get(), desired))
        return false;
      ComPtr<ITfContextComposition> cc;
      if (!NoForeign(cookie, actual.Get(), cc))
        return false;
      if (desired != expected) {
        ComPtr<ITfRange> verified_caret;
        LONG position = 0;
        if (!Selection(cookie, verified_caret, position) || position != start + length)
          return false;
      }
      owner_.owned_.reset();
      if (!Alive())
        return false;
      owner_.lifecycle_.Record(diagnostics::LifecycleKind::FinishVerified, serial);
    }
    return true;
  }
  void Run(TfEditCookie cookie) {
    if (!Check(Alive(), Cause::Stale))
      return;
    ComPtr<ITfRange> selection;
    LONG caret = 0;
    if (!Selection(cookie, selection, caret, owner_.owned_ == nullptr))
      return;
    if (!Admit(cookie, selection.Get()))
      return;
    if (owner_.owned_ && owner_.owned_->context_id.Get() != context_id_.Get()) {
      owner_.Refuse(Stage::Context, Cause::Predicate);
      owner_.Invalidate(true);
      return;
    }
    event_.context = KR_CTX_INELIGIBLE;
    if (!owner_.owned_) {
      ComPtr<ITfContextComposition> cc;
      if (!NoForeign(cookie, selection.Get(), cc) || !Boundary(cookie, selection.Get(), caret))
        return;
      event_.context = KR_CTX_BOUNDARY;
    }
    for (int iteration = 0; iteration != 2 && Alive(); ++iteration) {
      stage_ = Stage::Plan;
      kr_plan plan{};
      if (kr_plan_event(owner_.engine_, &event_, &plan) != KR_OK) {
        owner_.Refuse(Stage::Plan, Cause::Predicate);
        owner_.Invalidate(true);
        return;
      }
      last_action_ = plan.action;
      last_disposition_ = plan.disposition;
      if (plan.action != KR_MARK && plan.action != KR_FINISH) {
        if (!Alive() || kr_commit(owner_.engine_, plan.plan_id, KR_APPLIED_VERIFIED) != KR_OK) {
          owner_.Refuse(Stage::Acknowledge, Cause::Predicate);
          owner_.Invalidate(true);
          return;
        }
        eaten = plan.disposition == KR_KEY_CONSUMED;
        return;
      }
      wrote_ = false;
      outcome_recorded_ = false;
      const auto previous_refusals = owner_.diagnostics_.refusals;
      bool applied = Apply(cookie, plan);
      uint32_t outcome = applied  ? KR_APPLIED_VERIFIED
                         : wrote_ ? KR_ATTEMPTED_UNKNOWN
                                  : KR_REFUSED_BEFORE_WRITE;
      diagnostics::Count(owner_.diagnostics_, applied  ? Counter::Verified
                                              : wrote_ ? Counter::Unknown
                                                       : Counter::Refused);
      outcome_recorded_ = true;
      if (!applied && owner_.diagnostics_.refusals == previous_refusals)
        owner_.Refuse(Stage::Apply, Cause::Predicate);
      bool current = Alive();
      if (current && kr_commit(owner_.engine_, plan.plan_id, outcome) != KR_OK) {
        owner_.Refuse(Stage::Acknowledge, Cause::Predicate);
        applied = false;
      }
      if (!applied || !current) {
        eaten = UnknownDisposition();
        owner_.Invalidate(wrote_ || owner_.owned_ != nullptr);
        return;
      }
      eaten = plan.disposition == KR_KEY_CONSUMED;
      if (plan.disposition != KR_KEY_REPLAN)
        return;
      ComPtr<ITfRange> next_selection;
      ComPtr<ITfContextComposition> cc;
      LONG next_caret = 0;
      if (!Selection(cookie, next_selection, next_caret) || !Admit(cookie, next_selection.Get()) ||
          !NoForeign(cookie, next_selection.Get(), cc) ||
          !Boundary(cookie, next_selection.Get(), next_caret))
        return;
      event_.context = KR_CTX_BOUNDARY;
    }
  }
  ComPtr<Service> owner_keep_;
  Service &owner_;
  const uint64_t generation_;
  kr_event event_;
  ComPtr<ITfContext> context_;
  ComPtr<IUnknown> context_id_;
  Stage stage_ = Stage::Session;
  ULONG references_ = 1;
  bool entered_ = false;
  bool expired_ = false;
  bool wrote_ = false;
  bool outcome_recorded_ = false;
  uint32_t last_action_ = KR_PASS;
  uint32_t last_disposition_ = KR_KEY_PASS;
};

Service::Service(ITfInputProcessorProfileMgr *profile_api, decltype(&GetKeyboardLayoutList) layouts)
    : profile_dependency_(profile_api), layouts_(layouts) {
  InterlockedIncrement(&object_count);
}
Service::~Service() {
  active_ = false;
  stopped_ = true;
  kr_engine_free(engine_);
  engine_ = nullptr;
  InterlockedDecrement(&object_count);
}
HRESULT Service::QueryInterface(REFIID id, void **out) {
  if (!out)
    return E_POINTER;
  *out = nullptr;
  if (id == IID_IUnknown || id == IID_ITfTextInputProcessor || id == IID_ITfTextInputProcessorEx)
    *out = static_cast<ITfTextInputProcessorEx *>(this);
  else if (id == IID_ITfKeyEventSink)
    *out = static_cast<ITfKeyEventSink *>(this);
  else if (id == IID_ITfThreadMgrEventSink)
    *out = static_cast<ITfThreadMgrEventSink *>(this);
  else if (id == IID_ITfCompositionSink)
    *out = static_cast<ITfCompositionSink *>(this);
  else if (id == IID_ITfInputProcessorProfileActivationSink)
    *out = static_cast<ITfInputProcessorProfileActivationSink *>(this);
  else if (id == IID_ITfFunctionProvider)
    *out = static_cast<ITfFunctionProvider *>(this);
  else if (id == diagnostics::snapshot_iid)
    *out = static_cast<diagnostics::SnapshotReader *>(this);
  else if (id == diagnostics::snapshot_v2_iid)
    *out = static_cast<diagnostics::SnapshotReaderV2 *>(this);
  if (!*out)
    return E_NOINTERFACE;
  AddRef();
  return S_OK;
}
ULONG Service::AddRef() { return ++references_; }
ULONG Service::Release() {
  ULONG n = --references_;
  if (!n)
    delete this;
  return n;
}
HRESULT Service::GetType(GUID *type) {
  if (!type)
    return E_POINTER;
  *type = service_id;
  return S_OK;
}
HRESULT Service::GetDescription(BSTR *description) {
  if (!description)
    return E_POINTER;
  *description = SysAllocString(L"Keyrill proof service diagnostics");
  return *description ? S_OK : E_OUTOFMEMORY;
}
HRESULT Service::GetFunction(REFGUID function, REFIID interface_id, IUnknown **output) {
  if (!output)
    return E_POINTER;
  *output = nullptr;
  if (function != GUID_NULL)
    return E_NOINTERFACE;
  if (interface_id == diagnostics::snapshot_iid)
    *output = static_cast<diagnostics::SnapshotReader *>(this);
  else if (interface_id == diagnostics::snapshot_v2_iid)
    *output = static_cast<diagnostics::SnapshotReaderV2 *>(this);
  else
    return E_NOINTERFACE;
  AddRef();
  return S_OK;
}
HRESULT Service::Copy(uint32_t size, uint32_t requested_version, diagnostics::Snapshot *output) {
  if (!output)
    return E_POINTER;
  ZeroMemory(output, (std::min)(static_cast<size_t>(size), sizeof(*output)));
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  if (size != sizeof(*output) || requested_version != diagnostics::version)
    return E_INVALIDARG;
  if (busy_ || diagnostic_depth_)
    return E_PENDING;
  *output = diagnostics_;
  output->active = active_;
  output->stopped = stopped_;
  output->mode = mode_;
  return S_OK;
}
HRESULT Service::Copy(uint32_t size, uint32_t requested_version, diagnostics::SnapshotV2 *output) {
  if (!output)
    return E_POINTER;
  ZeroMemory(output, (std::min)(static_cast<size_t>(size), sizeof(*output)));
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  if (size != sizeof(*output) || requested_version != diagnostics::version_v2)
    return E_INVALIDARG;
  const auto result = Copy(sizeof(output->base), diagnostics::version, &output->base);
  if (result != S_OK)
    return result;
  output->lifecycle = lifecycle_.Copy(owned_ != nullptr, owned_ ? owned_->diagnostic_serial : 0);
  return S_OK;
}
void Service::UnregisterDiagnostics(ITfSourceSingle *source, TfClientId client) noexcept {
  try {
    source->UnadviseSingleSink(client, IID_ITfFunctionProvider);
  } catch (...) {
  }
}
void Service::RegisterDiagnostics() noexcept {
  try {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    const wchar_t *base = wcsrchr(path, L'\\');
    if (!length || length >= 32768 || !base || _wcsicmp(base + 1, L"KeyrillProofEditor.exe") != 0 ||
        !diagnostics::SameActivation(active_, diagnostics_.activation, diagnostics_.activation))
      return;
    ComPtr<Service> self(this);
    ComPtr<ITfThreadMgr> manager = manager_;
    const auto client = client_;
    const auto activation = diagnostics_.activation;
    ComPtr<ITfSourceSingle> source;
    if (!manager || FAILED(manager.As(&source)) || !source ||
        !diagnostics::SameActivation(active_, diagnostics_.activation, activation))
      return;
    HRESULT result = E_UNEXPECTED;
    try {
      result = source->AdviseSingleSink(client, IID_ITfFunctionProvider,
                                        static_cast<ITfFunctionProvider *>(this));
    } catch (...) {
      UnregisterDiagnostics(source.Get(), client);
      return;
    }
    if (SUCCEEDED(result) &&
        diagnostics::SameActivation(active_, diagnostics_.activation, activation)) {
      diagnostic_source_ = source;
      diagnostic_client_ = client;
    } else {
      UnregisterDiagnostics(source.Get(), client);
    }
  } catch (...) {
  }
}
bool Service::Current(uint64_t generation) const {
  return active_ && !stopped_ && generation_ == generation && GetCurrentThreadId() == thread_;
}
void Service::Invalidate(bool stop) {
  if (generation_ == std::numeric_limits<uint64_t>::max())
    stop = true;
  else
    ++generation_;
  stopped_ |= stop;
  if (engine_)
    kr_reset(engine_);
}
HRESULT Service::Activate(ITfThreadMgr *manager, TfClientId client) {
  return ActivateEx(manager, client, 0);
}
HRESULT Service::ActivateEx(ITfThreadMgr *manager, TfClientId client, DWORD flags) try {
  if (active_ || !manager || client == TF_CLIENTID_NULL || GetCurrentThreadId() != thread_)
    return E_INVALIDARG;
  if (flags & TF_TMAE_SECUREMODE)
    return E_ACCESSDENIED;
  if (!engine_ && kr_engine_new(KR_ABI_VERSION, &engine_) != KR_OK)
    return E_FAIL;
  if (kr_reset(engine_) != KR_OK || kr_set_keyboard(engine_, KR_KEYBOARD_PC) != KR_OK ||
      kr_set_automatic_policy(engine_, KR_POLICY_FIXTURE) != KR_OK)
    return E_FAIL;
  manager_ = manager;
  client_ = client;
  active_ = true;
  diagnostics::Increment(diagnostics_.activation, diagnostics_.saturated);
  stopped_ = false;
  uint64_t generation = generation_;
  if (FAILED(manager_->QueryInterface(IID_PPV_ARGS(&source_))) || !Current(generation) ||
      FAILED(manager_->QueryInterface(IID_PPV_ARGS(&keys_))) || !Current(generation) ||
      FAILED(source_->AdviseSink(IID_ITfThreadMgrEventSink,
                                 static_cast<ITfThreadMgrEventSink *>(this), &thread_cookie_)) ||
      !Current(generation) ||
      FAILED(source_->AdviseSink(IID_ITfInputProcessorProfileActivationSink,
                                 static_cast<ITfInputProcessorProfileActivationSink *>(this),
                                 &profile_cookie_)) ||
      !Current(generation) || FAILED(keys_->AdviseKeyEventSink(client_, this, TRUE)) ||
      !Current(generation)) {
    Deactivate();
    return E_FAIL;
  }
  TF_INPUTPROCESSORPROFILE profile{};
  HRESULT profile_result = S_OK;
  if (profile_dependency_)
    profiles_manager_ = profile_dependency_;
  else
    profile_result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles_manager_));
  if (SUCCEEDED(profile_result) && profiles_manager_ && Current(generation) &&
      SUCCEEDED(profiles_manager_->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile)) &&
      Current(generation))
    OnActivated(profile.dwProfileType, profile.langid, profile.clsid, profile.catid,
                profile.guidProfile, profile.hkl, TF_IPSINK_FLAG_ACTIVE);
  RegisterDiagnostics();
  return S_OK;
} catch (...) {
  Deactivate();
  return E_FAIL;
}
void Service::InvalidateForLifecycle(diagnostics::LifecycleCause cause) {
  lifecycle_.Record(diagnostics::LifecycleKind::LifecycleInvalidation,
                    owned_ ? owned_->diagnostic_serial : 0, cause);
  Invalidate(owned_ != nullptr);
}
HRESULT Service::Deactivate() {
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  ComPtr<Service> self(this);
  DiagnosticCallback callback(*this);
  InvalidateForLifecycle(diagnostics::LifecycleCause::Deactivate);
  active_ = false;
  auto diagnostic_source = std::move(diagnostic_source_);
  const auto diagnostic_client = diagnostic_client_;
  diagnostic_client_ = TF_CLIENTID_NULL;
  auto keys = std::move(keys_);
  auto source = std::move(source_);
  auto manager = std::move(manager_);
  auto profiles_manager = std::move(profiles_manager_);
  if (owned_)
    lifecycle_.Record(diagnostics::LifecycleKind::Detached, owned_->diagnostic_serial);
  auto owned = std::move(owned_);
  const auto client = client_;
  const auto thread_cookie = thread_cookie_;
  const auto profile_cookie = profile_cookie_;
  thread_cookie_ = profile_cookie_ = TF_INVALID_COOKIE;
  client_ = TF_CLIENTID_NULL;
  mode_ = 0;
  HRESULT result = S_OK;
  try {
    if (keys && client != TF_CLIENTID_NULL && FAILED(keys->UnadviseKeyEventSink(client)))
      result = E_FAIL;
  } catch (...) {
    result = E_FAIL;
  }
  try {
    if (source && thread_cookie != TF_INVALID_COOKIE && FAILED(source->UnadviseSink(thread_cookie)))
      result = E_FAIL;
  } catch (...) {
    result = E_FAIL;
  }
  try {
    if (source && profile_cookie != TF_INVALID_COOKIE &&
        FAILED(source->UnadviseSink(profile_cookie)))
      result = E_FAIL;
  } catch (...) {
    result = E_FAIL;
  }
  if (diagnostic_source && diagnostic_client != TF_CLIENTID_NULL)
    UnregisterDiagnostics(diagnostic_source.Get(), diagnostic_client);
  return result;
}
bool Service::HandleEvent(ITfContext *context, const kr_event &event) {
  if (GetCurrentThreadId() != thread_)
    return false;
  DiagnosticCallback callback(*this);
  diagnostics::Count(diagnostics_, Counter::HandleEntered);
  if (!active_ || stopped_ || !context) {
    Refuse(Stage::Handle, !active_ ? Cause::Inactive : stopped_ ? Cause::Stopped : Cause::Missing);
    return false;
  }
  if (busy_) {
    Refuse(Stage::Handle, Cause::Reentry);
    Invalidate(owned_ != nullptr);
    return false;
  }
  busy_ = true;
  bool eaten = false;
  ComPtr<EditSession> request;
  try {
    request.Attach(new EditSession(*this, context, event));
    if (request->valid()) {
      HRESULT session_result = E_FAIL;
      diagnostics::Count(diagnostics_, Counter::Requested);
      diagnostics_.request_present = diagnostics_.session_present = 0;
      diagnostics_.request_result = diagnostics_.session_result = 0;
      HRESULT request_result = context->RequestEditSession(
          client_, request.Get(), TF_ES_SYNC | TF_ES_READWRITE, &session_result);
      diagnostics_.request_present = diagnostics_.session_present = 1;
      diagnostics_.request_result = static_cast<uint32_t>(request_result);
      diagnostics_.session_result = static_cast<uint32_t>(session_result);
      request->Expire();
      if (SUCCEEDED(request_result) && SUCCEEDED(session_result) && request->completed)
        eaten = request->eaten;
      else {
        if (FAILED(request_result))
          Refuse(Stage::Request, Cause::Native, request_result);
        else if (FAILED(session_result))
          Refuse(Stage::Session, Cause::Native, session_result);
        else
          Refuse(Stage::Session, Cause::Predicate);
        eaten = request->UnknownDisposition();
        Invalidate(request->attempted() || owned_ != nullptr);
      }
    } else {
      Refuse(Stage::Session, Cause::Stale);
    }
  } catch (...) {
    Refuse(Stage::Handle, Cause::Exception);
    if (request)
      eaten = request->UnknownDisposition();
    Invalidate(true);
  }
  if (request)
    request->Expire();
  busy_ = false;
  return eaten;
}
HRESULT Service::OnKeyDown(ITfContext *context, WPARAM key, LPARAM flags, BOOL *eaten) {
  if (!eaten)
    return E_POINTER;
  *eaten = FALSE;
  ComPtr<Service> self(this);
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  DiagnosticCallback callback(*this);
  try {
    diagnostics::Count(diagnostics_, Counter::KeyEntered);
    if (busy_ || key_admission_) {
      Refuse(Stage::Key, Cause::Reentry);
      Invalidate(owned_ != nullptr);
      return S_OK;
    }
    key_admission_ = true;
    struct Admission {
      bool &flag;
      ~Admission() { flag = false; }
    } admission{key_admission_};
    const uint64_t generation = generation_;
    const uint32_t mode = mode_;
    BYTE state[256]{};
    if (!GetKeyboardState(state)) {
      Refuse(Stage::Keyboard, Cause::Predicate);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    TF_INPUTPROCESSORPROFILE profile{};
    ComPtr<ITfInputProcessorProfileMgr> manager = profiles_manager_;
    HRESULT profile_result = S_OK;
    if (!Current(generation) || !manager ||
        (profile_result = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile)) != S_OK ||
        !Current(generation)) {
      Refuse(Stage::Profile,
             profile_result != S_OK ? Cause::Native
             : !active_             ? Cause::Inactive
             : stopped_             ? Cause::Stopped
             : !manager             ? Cause::Missing
                                    : Cause::Stale,
             profile_result != S_OK ? profile_result : S_OK);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    if (!ActiveProfile(profile, mode)) {
      Cause cause = profile.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR ? Cause::WrongType
                    : profile.clsid != service_id                          ? Cause::WrongService
                    : !(profile.dwFlags & TF_IPP_FLAG_ACTIVE)              ? Cause::NotActive
                                                                           : Cause::WrongProfile;
      Refuse(Stage::Layout, cause);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    ComPtr<ITfInputProcessorProfileSubstituteLayout> substitute;
    HRESULT result = manager->QueryInterface(IID_PPV_ARGS(&substitute));
    if (result != S_OK || !Current(generation) || !substitute) {
      Refuse(Stage::Layout,
             result != S_OK         ? Cause::Native
             : !Current(generation) ? Cause::Stale
                                    : Cause::Missing,
             result != S_OK ? result : S_OK);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    HKL actual = nullptr;
    result = substitute->GetSubstituteKeyboardLayout(profile.clsid, profile.langid,
                                                     profile.guidProfile, &actual);
    if (result != S_OK || !Current(generation) || !actual) {
      Refuse(Stage::Layout,
             result != S_OK         ? Cause::Native
             : !Current(generation) ? Cause::Stale
                                    : Cause::Missing,
             result != S_OK ? result : S_OK);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    HKL layout = ProfileLayout(profile, mode, actual);
    if (!layout || !layouts_) {
      Refuse(Stage::Layout, !layout ? Cause::Substitute : Cause::Missing);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    HKL loaded[64]{};
    int count = layouts_(0, nullptr);
    if (!Current(generation) || count <= 0 || count > 64) {
      Refuse(Stage::Layout, !Current(generation) ? Cause::Stale : Cause::Substitute);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    int filled = layouts_(64, loaded);
    if (!Current(generation) || filled != count) {
      Refuse(Stage::Layout, !Current(generation) ? Cause::Stale : Cause::Substitute);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    int after = layouts_(0, nullptr);
    if (!Current(generation) || after != count ||
        std::find(loaded, loaded + count, layout) == loaded + count) {
      Refuse(Stage::Layout, !Current(generation) ? Cause::Stale : Cause::Substitute);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    NativeKey translated = TranslateKey(key, flags, layout, state);
    const auto *event = std::get_if<kr_event>(&translated);
    if (!Current(generation) || (event && event->mode != mode)) {
      Refuse(Stage::Mode, !Current(generation) ? Cause::Stale : Cause::Predicate);
      InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
      return S_OK;
    }
    if (!event)
      return S_OK;
    key_admission_ = false;
    *eaten = HandleEvent(context, *event);
    if (*eaten)
      diagnostics::Count(diagnostics_, Counter::Consumed);
    return S_OK;
  } catch (...) {
    Refuse(Stage::Key, Cause::Exception);
    Invalidate(true);
    return E_FAIL;
  }
}
HRESULT Service::OnTestKeyDown(ITfContext *, WPARAM, LPARAM, BOOL *eaten) {
  if (!eaten)
    return E_POINTER;
  DiagnosticCallback callback(*this);
  diagnostics::Count(diagnostics_, Counter::TestEntered);
  *eaten = active_ && !stopped_ && mode_ != 0;
  if (*eaten)
    diagnostics::Count(diagnostics_, Counter::Interested);
  else
    Refuse(Stage::Test, !active_ ? Cause::Inactive : stopped_ ? Cause::Stopped : Cause::ModeUnset);
  return S_OK;
}
HRESULT Service::OnTestKeyUp(ITfContext *, WPARAM, LPARAM, BOOL *eaten) {
  if (!eaten)
    return E_POINTER;
  *eaten = FALSE;
  return S_OK;
}
HRESULT Service::OnKeyUp(ITfContext *c, WPARAM k, LPARAM l, BOOL *e) {
  return OnTestKeyUp(c, k, l, e);
}
HRESULT Service::OnPreservedKey(ITfContext *, REFGUID, BOOL *eaten) {
  if (!eaten)
    return E_POINTER;
  *eaten = FALSE;
  return S_OK;
}
HRESULT Service::OnSetFocus(BOOL foreground) {
  if (!foreground)
    InvalidateForLifecycle(diagnostics::LifecycleCause::ForegroundLost);
  return S_OK;
}
HRESULT Service::OnInitDocumentMgr(ITfDocumentMgr *) { return S_OK; }
HRESULT Service::OnUninitDocumentMgr(ITfDocumentMgr *) {
  InvalidateForLifecycle(diagnostics::LifecycleCause::DocumentUninit);
  return S_OK;
}
HRESULT Service::OnSetFocus(ITfDocumentMgr *, ITfDocumentMgr *) {
  InvalidateForLifecycle(diagnostics::LifecycleCause::DocumentFocus);
  return S_OK;
}
HRESULT Service::OnPushContext(ITfContext *) {
  InvalidateForLifecycle(diagnostics::LifecycleCause::ContextPush);
  return S_OK;
}
HRESULT Service::OnPopContext(ITfContext *) {
  InvalidateForLifecycle(diagnostics::LifecycleCause::ContextPop);
  return S_OK;
}
HRESULT Service::OnCompositionTerminated(TfEditCookie, ITfComposition *composition) {
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  DiagnosticCallback callback(*this);
  const auto serial = owned_ ? owned_->diagnostic_serial : 0;
  try {
    if (!composition || !owned_) {
      lifecycle_.Record(composition ? diagnostics::LifecycleKind::TerminationWithoutOwned
                                    : diagnostics::LifecycleKind::TerminationUnresolved,
                        serial, diagnostics::LifecycleCause::None,
                        composition ? diagnostics::Unresolved::None
                                    : diagnostics::Unresolved::NullArgument);
      return S_OK;
    }
    uint64_t generation = generation_;
    ComPtr<IUnknown> identity;
    HRESULT result = composition->QueryInterface(IID_PPV_ARGS(&identity));
    if (generation != generation_ || FAILED(result) || !owned_) {
      lifecycle_.Record(diagnostics::LifecycleKind::TerminationUnresolved, serial,
                        diagnostics::LifecycleCause::None,
                        FAILED(result) ? diagnostics::Unresolved::IdentityFailure
                                       : diagnostics::Unresolved::Stale);
      return S_OK;
    }
    const bool same_ownership = serial == owned_->diagnostic_serial;
    if (identity.Get() == owned_->composition_id.Get()) {
      lifecycle_.Record(same_ownership ? diagnostics::LifecycleKind::ExactTermination
                                       : diagnostics::LifecycleKind::TerminationUnresolved,
                        serial, diagnostics::LifecycleCause::None,
                        same_ownership ? diagnostics::Unresolved::None
                                       : diagnostics::Unresolved::Stale);
      Invalidate(false);
      owned_.reset();
    } else {
      lifecycle_.Record(same_ownership ? diagnostics::LifecycleKind::DifferentTermination
                                       : diagnostics::LifecycleKind::TerminationUnresolved,
                        serial, diagnostics::LifecycleCause::None,
                        same_ownership ? diagnostics::Unresolved::None
                                       : diagnostics::Unresolved::Stale);
    }
    return S_OK;
  } catch (...) {
    lifecycle_.Record(diagnostics::LifecycleKind::TerminationUnresolved, serial,
                      diagnostics::LifecycleCause::None, diagnostics::Unresolved::Exception);
    Invalidate(true);
    return E_FAIL;
  }
}
HRESULT Service::OnActivated(DWORD, LANGID, REFCLSID clsid, REFGUID, REFGUID profile, HKL,
                             DWORD flags) {
  InvalidateForLifecycle(diagnostics::LifecycleCause::ProfileChanged);
  mode_ = 0;
  if (clsid == service_id && (flags & TF_IPSINK_FLAG_ACTIVE))
    for (const auto &entry : profiles)
      if (profile == entry.id)
        mode_ = entry.mode;
  return S_OK;
}
} // namespace keyrill
