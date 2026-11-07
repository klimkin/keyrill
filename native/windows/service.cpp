#include "service.h"
#include <algorithm>
#include <cwctype>
#include <inputscope.h>
#include <limits>
#include <new>
#include <richedit.h>

namespace keyrill {
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

bool TranslateKey(WPARAM key, LPARAM flags, HKL layout, const BYTE (&state)[256], kr_event &e) {
  e = {};
  e.context = KR_CTX_INELIGIBLE;
  for (const auto &profile : profiles)
    if (LOWORD(reinterpret_cast<ULONG_PTR>(layout)) == profile.language &&
        HIWORD(reinterpret_cast<ULONG_PTR>(layout)) == profile.language)
      e.mode = profile.mode;
  if (!e.mode || LOWORD(flags) != 1 || (state[VK_CONTROL] & 0x80) || (state[VK_MENU] & 0x80) ||
      (state[VK_LWIN] & 0x80) || (state[VK_RWIN] & 0x80)) {
    e.role = KR_FINISH_REQUEST;
    return false;
  }
  if (key == VK_BACK || key == VK_ESCAPE) {
    e.role = key == VK_BACK ? KR_BACKSPACE : KR_ESCAPE;
    return true;
  }
  wchar_t text[KR_EVENT_CAPACITY]{};
  int count = ToUnicodeEx(static_cast<UINT>(key), (static_cast<UINT>(flags) >> 16) & 0xff, state,
                          text, KR_EVENT_CAPACITY, 4, layout);
  bool letter = (text[0] >= L'a' && text[0] <= L'z') || (text[0] >= L'A' && text[0] <= L'Z') ||
                (text[0] >= L'\x0410' && text[0] <= L'\x044f') || text[0] == L'\x0401' ||
                text[0] == L'\x0451';
  if (count != 1 || (text[0] != L' ' && !letter)) {
    e.role = KR_FINISH_REQUEST;
    return false;
  }
  e.role = KR_TEXT;
  e.len = 1;
  e.units[0] = static_cast<uint16_t>(text[0]);
  return true;
}
HKL ProfileLayout(const TF_INPUTPROCESSORPROFILE &profile, uint32_t mode) {
  if (profile.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR || profile.clsid != service_id ||
      !(profile.dwFlags & TF_IPP_FLAG_ACTIVE))
    return nullptr;
  for (const auto &entry : profiles)
    if (entry.id == profile.guidProfile && entry.language == profile.langid && entry.mode == mode &&
        reinterpret_cast<ULONG_PTR>(profile.hklSubstitute) ==
            (static_cast<ULONG_PTR>(entry.language) << 16 | entry.language))
      return profile.hklSubstitute;
  return nullptr;
}

class EditSession final : public ITfEditSession {
public:
  EditSession(Service &owner, ITfContext *context, kr_event event)
      : owner_keep_(&owner), owner_(owner), generation_(owner.generation_), event_(event) {
    context_ = context;
    if (owner_.Current(generation_) && context_)
      context_->QueryInterface(IID_PPV_ARGS(&context_id_));
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
    if (entered_ || !Alive())
      return E_FAIL;
    entered_ = true;
    try {
      Run(cookie);
    } catch (...) {
      owner_.Invalidate(true);
      eaten = UnknownDisposition();
    }
    completed = true;
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
  bool Good(HRESULT result) const { return SUCCEEDED(result) && Alive(); }
  bool Identity(IUnknown *object, IUnknown *expected) {
    ComPtr<IUnknown> identity;
    if (!object || !Good(object->QueryInterface(IID_PPV_ARGS(&identity))))
      return false;
    return identity.Get() == expected && Alive();
  }
  bool Focused() {
    ComPtr<ITfThreadMgr> manager = owner_.manager_;
    if (!Alive() || !manager)
      return false;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> top;
    ComPtr<ITfContextView> view;
    HWND window = nullptr;
    if (!Good(manager->GetFocus(&document)) || !document || !Good(document->GetTop(&top)) ||
        !Identity(top.Get(), context_id_.Get()) || !Good(context_->GetActiveView(&view)) || !view ||
        !Good(view->GetWnd(&window)))
      return false;
    return window && ::GetFocus() == window && ProofWindow(window) && Alive();
  }
  bool Extent(ITfRange *range, LONG &start, LONG &length) {
    ComPtr<ITfContext> context;
    if (!Good(range->GetContext(&context)) || !Identity(context.Get(), context_id_.Get()))
      return false;
    ComPtr<ITfRangeACP> acp;
    if (!Good(range->QueryInterface(IID_PPV_ARGS(&acp))) || !Good(acp->GetExtent(&start, &length)))
      return false;
    return start >= 0 && length >= 0 && start <= std::numeric_limits<LONG>::max() - length;
  }
  bool Selection(TfEditCookie cookie, ComPtr<ITfRange> &range, LONG &caret, bool collapsed = true) {
    TF_SELECTION selection{};
    ULONG fetched = 0;
    HRESULT result = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
    range.Attach(selection.range);
    if (!Good(result) || fetched != 1 || !range || selection.style.fInterimChar)
      return false;
    LONG length = 0;
    return Extent(range.Get(), caret, length) && (!collapsed || length == 0);
  }
  bool Disabled(IUnknown *target, REFGUID id) {
    ComPtr<ITfCompartmentMgr> compartments;
    if (!target || !Good(target->QueryInterface(IID_PPV_ARGS(&compartments))))
      return true;
    ComPtr<ITfCompartment> compartment;
    if (!Good(compartments->GetCompartment(id, &compartment)) || !compartment)
      return true;
    VARIANT value;
    VariantInit(&value);
    HRESULT result = compartment->GetValue(&value);
    bool disabled = !Good(result) || (value.vt != VT_EMPTY && value.vt != VT_I4) ||
                    (value.vt == VT_I4 && value.lVal != 0);
    VariantClear(&value);
    return disabled || !Alive();
  }
  bool Admit(TfEditCookie cookie, ITfRange *selection) {
    ComPtr<ITfContextView> view;
    HWND window = nullptr;
    if (!Focused() || !Good(context_->GetActiveView(&view)) || !view ||
        !Good(view->GetWnd(&window)) || !ProofWindow(window) || !Alive())
      return false;
    TF_STATUS status{};
    ComPtr<ITfThreadMgr> manager = owner_.manager_;
    if (!Good(context_->GetStatus(&status)) || (status.dwDynamicFlags & TF_SD_READONLY) ||
        Disabled(context_.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED) ||
        Disabled(context_.Get(), GUID_COMPARTMENT_EMPTYCONTEXT) ||
        Disabled(manager.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED))
      return false;
    ComPtr<ITfReadOnlyProperty> property;
    HRESULT result = context_->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);
    if (!Good(result) || result != S_OK || !property)
      return false;
    VARIANT value;
    VariantInit(&value);
    result = property->GetValue(cookie, selection, &value);
    if (!Good(result) || result != S_OK || value.vt != VT_UNKNOWN || !value.punkVal) {
      VariantClear(&value);
      return false;
    }
    ComPtr<ITfInputScope> scope;
    result = value.punkVal->QueryInterface(IID_PPV_ARGS(&scope));
    VariantClear(&value);
    if (!Good(result) || !scope)
      return false;
    InputScope *scopes = nullptr;
    UINT count = 0;
    result = scope->GetInputScopes(&scopes, &count);
    bool ordinary = Good(result) && result == S_OK && count == 1 && scopes &&
                    (scopes[0] == IS_DEFAULT || scopes[0] == IS_TEXT);
    CoTaskMemFree(scopes);
    return ordinary && Alive();
  }
  bool NoForeign(TfEditCookie, ITfRange *, ComPtr<ITfContextComposition> &cc) {
    if (!Good(context_->QueryInterface(IID_PPV_ARGS(&cc))))
      return false;
    ComPtr<IEnumITfCompositionView> enumerator;
    if (!Good(cc->EnumCompositions(&enumerator)) || !enumerator)
      return false;
    ComPtr<ITfCompositionView> view;
    ULONG fetched = 0;
    HRESULT result = enumerator->Next(1, &view, &fetched);
    return Good(result) && fetched == 0;
  }
  bool ActiveComposition(IUnknown *expected) {
    ComPtr<ITfContextComposition> cc;
    ComPtr<IEnumITfCompositionView> enumerator;
    ComPtr<ITfCompositionView> view;
    ULONG fetched = 0;
    if (!Good(context_->QueryInterface(IID_PPV_ARGS(&cc))) ||
        !Good(cc->EnumCompositions(&enumerator)) || !enumerator ||
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
    if (!caret)
      return true;
    ComPtr<ITfRange> prefix;
    LONG shifted = 0;
    if (!Good(selection->Clone(&prefix)) ||
        !Good(prefix->ShiftStart(cookie, -1, &shifted, nullptr)) || shifted != -1)
      return false;
    LONG start = 0, length = 0;
    if (!Extent(prefix.Get(), start, length) || start != caret - 1 || length != 1)
      return false;
    wchar_t text[2]{};
    ULONG count = 0;
    return Good(prefix->GetText(cookie, 0, text, 2, &count)) && count == 1 && iswspace(text[0]);
  }
  bool Apply(TfEditCookie cookie, const kr_plan &plan) {
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
      owner_.owned_ = std::move(owned);
      range.Reset();
      if (!Alive() || !OwnedRange(cookie, expected, true, range))
        return false;
    }
    if (!Focused())
      return false;
    if (plan.action != KR_FINISH || desired != expected) {
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
    if (plan.action == KR_MARK) {
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
      owner_.owned_->verified = desired;
    } else {
      if (!Focused())
        return false;
      wrote_ = true;
      if (!Good(composition->EndComposition(cookie)))
        return false;
      LONG finished_start = 0, finished_length = 0;
      if (!Extent(actual.Get(), finished_start, finished_length) || finished_start != start ||
          finished_length != length || !Read(cookie, actual.Get(), desired))
        return false;
      ComPtr<ITfContextComposition> cc;
      if (!NoForeign(cookie, actual.Get(), cc))
        return false;
      owner_.owned_.reset();
      if (!Alive())
        return false;
    }
    return true;
  }
  void Run(TfEditCookie cookie) {
    if (!Alive())
      return;
    ComPtr<ITfRange> selection;
    LONG caret = 0;
    if (!Selection(cookie, selection, caret, owner_.owned_ == nullptr))
      return;
    if (!Admit(cookie, selection.Get()))
      return;
    if (owner_.owned_ && owner_.owned_->context_id.Get() != context_id_.Get()) {
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
      kr_plan plan{};
      if (kr_plan_event(owner_.engine_, &event_, &plan) != KR_OK) {
        owner_.Invalidate(true);
        return;
      }
      last_action_ = plan.action;
      last_disposition_ = plan.disposition;
      if (plan.action != KR_MARK && plan.action != KR_FINISH) {
        kr_commit(owner_.engine_, plan.plan_id, KR_APPLIED_VERIFIED);
        return;
      }
      wrote_ = false;
      bool applied = Apply(cookie, plan);
      uint32_t outcome = applied  ? KR_APPLIED_VERIFIED
                         : wrote_ ? KR_ATTEMPTED_UNKNOWN
                                  : KR_REFUSED_BEFORE_WRITE;
      bool current = Alive();
      if (current && kr_commit(owner_.engine_, plan.plan_id, outcome) != KR_OK)
        applied = false;
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
  ULONG references_ = 1;
  bool entered_ = false;
  bool expired_ = false;
  bool wrote_ = false;
  uint32_t last_action_ = KR_PASS;
  uint32_t last_disposition_ = KR_KEY_PASS;
};

Service::Service() { InterlockedIncrement(&object_count); }
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
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&profiles_manager_))) &&
      Current(generation) &&
      SUCCEEDED(profiles_manager_->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile)) &&
      Current(generation))
    OnActivated(profile.dwProfileType, profile.langid, profile.clsid, profile.catid,
                profile.guidProfile, profile.hkl, TF_IPSINK_FLAG_ACTIVE);
  return S_OK;
} catch (...) {
  Deactivate();
  return E_FAIL;
}
void Service::InvalidateForLifecycle() { Invalidate(owned_ != nullptr); }
HRESULT Service::Deactivate() {
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  ComPtr<Service> self(this);
  InvalidateForLifecycle();
  active_ = false;
  auto keys = std::move(keys_);
  auto source = std::move(source_);
  auto manager = std::move(manager_);
  auto profiles_manager = std::move(profiles_manager_);
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
  return result;
}
bool Service::HandleEvent(ITfContext *context, const kr_event &event) {
  if (GetCurrentThreadId() != thread_ || !active_ || stopped_ || !context)
    return false;
  if (busy_) {
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
      HRESULT request_result = context->RequestEditSession(
          client_, request.Get(), TF_ES_SYNC | TF_ES_READWRITE, &session_result);
      request->Expire();
      if (SUCCEEDED(request_result) && SUCCEEDED(session_result) && request->completed)
        eaten = request->eaten;
      else {
        eaten = request->UnknownDisposition();
        Invalidate(request->attempted() || owned_ != nullptr);
      }
    }
  } catch (...) {
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
  try {
    if (GetCurrentThreadId() != thread_)
      return RPC_E_WRONG_THREAD;
    if (busy_) {
      Invalidate(owned_ != nullptr);
      return S_OK;
    }
    BYTE state[256]{};
    kr_event event{};
    if (!GetKeyboardState(state)) {
      InvalidateForLifecycle();
      return S_OK;
    }
    uint64_t generation = generation_;
    TF_INPUTPROCESSORPROFILE profile{};
    ComPtr<ITfInputProcessorProfileMgr> manager = profiles_manager_;
    if (!Current(generation) || !manager ||
        FAILED(manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile)) ||
        !Current(generation)) {
      InvalidateForLifecycle();
      return S_OK;
    }
    HKL layout = ProfileLayout(profile, mode_);
    if (!layout) {
      InvalidateForLifecycle();
      return S_OK;
    }
    TranslateKey(key, flags, layout, state, event);
    if (event.mode != mode_) {
      InvalidateForLifecycle();
      return S_OK;
    }
    *eaten = HandleEvent(context, event);
    return S_OK;
  } catch (...) {
    Invalidate(true);
    return E_FAIL;
  }
}
HRESULT Service::OnTestKeyDown(ITfContext *, WPARAM, LPARAM, BOOL *eaten) {
  if (!eaten)
    return E_POINTER;
  *eaten = active_ && !stopped_ && mode_ != 0;
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
    InvalidateForLifecycle();
  return S_OK;
}
HRESULT Service::OnInitDocumentMgr(ITfDocumentMgr *) { return S_OK; }
HRESULT Service::OnUninitDocumentMgr(ITfDocumentMgr *) {
  InvalidateForLifecycle();
  return S_OK;
}
HRESULT Service::OnSetFocus(ITfDocumentMgr *, ITfDocumentMgr *) {
  InvalidateForLifecycle();
  return S_OK;
}
HRESULT Service::OnPushContext(ITfContext *) {
  InvalidateForLifecycle();
  return S_OK;
}
HRESULT Service::OnPopContext(ITfContext *) {
  InvalidateForLifecycle();
  return S_OK;
}
HRESULT Service::OnCompositionTerminated(TfEditCookie, ITfComposition *composition) try {
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  if (!composition || !owned_)
    return S_OK;
  uint64_t generation = generation_;
  ComPtr<IUnknown> identity;
  HRESULT result = composition->QueryInterface(IID_PPV_ARGS(&identity));
  if (generation != generation_ || FAILED(result) || !owned_)
    return S_OK;
  if (identity.Get() == owned_->composition_id.Get()) {
    Invalidate(false);
    owned_.reset();
  }
  return S_OK;
} catch (...) {
  Invalidate(true);
  return E_FAIL;
}
HRESULT Service::OnActivated(DWORD, LANGID, REFCLSID clsid, REFGUID, REFGUID profile, HKL,
                             DWORD flags) {
  InvalidateForLifecycle();
  mode_ = 0;
  if (clsid == service_id && (flags & TF_IPSINK_FLAG_ACTIVE))
    for (const auto &entry : profiles)
      if (profile == entry.id)
        mode_ = entry.mode;
  return S_OK;
}
} // namespace keyrill
