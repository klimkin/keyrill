#include "service.h"
#include "keys.h"
#include <algorithm>
#include <cwctype>
#include <exception>
#include <inputscope.h>
#include <limits>
#include <new>
#include <olectl.h>
#include <richedit.h>

namespace keyrill {
using diagnostics::Cause;
using diagnostics::Counter;
using diagnostics::Stage;
long object_count = 0;

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
    Service::DiagnosticCallback callback(owner_);
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
  std::optional<native_field::FieldIdentity> FreshFieldIdentity() {
    Observe observe(*this, Stage::Focus);
    ComPtr<ITfThreadMgr> manager = owner_.native_ ? owner_.native_->manager : nullptr;
    if (!Check(Alive(), Cause::Stale) || !Check(manager != nullptr, Cause::Missing))
      return std::nullopt;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> top;
    ComPtr<ITfContextView> view;
    HWND window = nullptr;
    if (!Good(manager->GetFocus(&document)) || !Check(document != nullptr, Cause::Missing) ||
        !Good(document->GetTop(&top)) || !Identity(top.Get(), context_id_.Get()) ||
        !Good(context_->GetActiveView(&view)) || !Check(view != nullptr, Cause::Missing) ||
        !Good(view->GetWnd(&window)))
      return std::nullopt;
    stage_ = Stage::ProofWindow;
    const auto facts = owner_.field_source_
                           ? owner_.field_source_->Capture(window, owner_.host_identity_)
                           : native_field::Capture(window, owner_.host_identity_);
    auto identity = native_field::Match(facts);
    if (!Check(identity.has_value() && Alive()))
      return std::nullopt;
    return identity;
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
    auto selection = ObserveSelection(cookie, collapsed);
    if (!selection)
      return false;
    range = selection->range;
    caret = selection->value.start;
    return true;
  }
  bool Disabled(IUnknown *target, REFGUID id, Stage stage) {
    Observe observe(*this, stage);
    ComPtr<ITfCompartmentMgr> compartments;
    if (!Check(target != nullptr, Cause::Missing) ||
        !Exact(target->QueryInterface(IID_PPV_ARGS(&compartments))) ||
        !Check(compartments != nullptr, Cause::Missing))
      return true;
    ComPtr<ITfCompartment> compartment;
    if (!Exact(compartments->GetCompartment(id, &compartment)) ||
        !Check(compartment != nullptr, Cause::Missing))
      return true;
    VARIANT value;
    VariantInit(&value);
    HRESULT result = compartment->GetValue(&value);
    const bool unset = result == S_FALSE && value.vt == VT_EMPTY;
    bool disabled = !(unset ? Good(result) : Exact(result)) ||
                    !Check(value.vt == VT_EMPTY || (value.vt == VT_I4 && value.lVal == 0));
    VariantClear(&value);
    return disabled || !Check(Alive(), Cause::Stale);
  }
  bool WritableContext() {
    Observe observe(*this, Stage::Status);
    TF_STATUS status{};
    ComPtr<ITfThreadMgr> manager = owner_.native_ ? owner_.native_->manager : nullptr;
    return Exact(context_->GetStatus(&status)) &&
           Check(!(status.dwDynamicFlags & TF_SD_READONLY)) &&
           !Disabled(context_.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED, Stage::ContextDisabled) &&
           !Disabled(context_.Get(), GUID_COMPARTMENT_EMPTYCONTEXT, Stage::EmptyContext) &&
           !Disabled(manager.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED, Stage::ThreadDisabled);
  }
  enum class ScopeAuthority { ExplicitOrdinary, NotepadUnavailableValue };
  std::optional<ScopeAuthority> OrdinaryScope(TfEditCookie cookie, ITfRange *selection,
                                              native_field::Kind kind) {
    Observe observe(*this, Stage::AppProperty);
    ComPtr<ITfReadOnlyProperty> property;
    HRESULT result = context_->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);
    if (!Exact(result) || !Check(property != nullptr, Cause::Missing))
      return std::nullopt;
    VARIANT value;
    VariantInit(&value);
    stage_ = Stage::AppValue;
    result = property->GetValue(cookie, selection, &value);
    if (result == E_FAIL && value.vt == VT_EMPTY && kind == native_field::Kind::Notepad &&
        Check(Alive(), Cause::Stale)) {
      VariantClear(&value);
      return ScopeAuthority::NotepadUnavailableValue;
    }
    if (!Exact(result) || !Check(value.vt == VT_UNKNOWN, Cause::WrongType) ||
        !Check(value.punkVal != nullptr, Cause::Missing)) {
      VariantClear(&value);
      return std::nullopt;
    }
    stage_ = Stage::ScopeInterface;
    ComPtr<ITfInputScope> scope;
    result = value.punkVal->QueryInterface(IID_PPV_ARGS(&scope));
    VariantClear(&value);
    if (!Exact(result) || !Check(scope != nullptr, Cause::Missing))
      return std::nullopt;
    stage_ = Stage::Scopes;
    InputScope *scopes = nullptr;
    UINT count = 0;
    result = scope->GetInputScopes(&scopes, &count);
    const bool ordinary = Exact(result) && Check(count == 1 && scopes &&
                                                 (scopes[0] == IS_DEFAULT || scopes[0] == IS_TEXT));
    CoTaskMemFree(scopes);
    if (!ordinary || !Check(Alive(), Cause::Stale))
      return std::nullopt;
    return ScopeAuthority::ExplicitOrdinary;
  }
  struct AdmittedSelection {
    native_field::FieldIdentity field;
    ComPtr<ITfRange> range;
    LONG caret;
  };
  struct SelectionValue {
    LONG start, length;
    TF_SELECTIONSTYLE style;
    bool operator==(const SelectionValue &other) const {
      return start == other.start && length == other.length && style.ase == other.style.ase &&
             style.fInterimChar == other.style.fInterimChar;
    }
  };
  struct SelectionObservation {
    ComPtr<ITfRange> range;
    SelectionValue value;
  };
  struct FinishSelection {
    SelectionValue original;
    ComPtr<ITfRange> carrier;
  };
  bool AllowedRange(TfEditCookie cookie, ITfRange *range,
                    const native_field::FieldIdentity &expected) {
    const auto field = FreshFieldIdentity();
    if (!field || !Check(*field == expected))
      return false;
    ComPtr<ITfContext> context;
    if (!Good(range->GetContext(&context)) || !Identity(context.Get(), context_id_.Get()))
      return false;
    context.Reset();
    return WritableContext() && OrdinaryScope(cookie, range, expected.kind).has_value() &&
           Check(FreshFieldIdentity() == field && Alive(), Cause::Stale);
  }
  std::optional<SelectionObservation> ObserveSelection(TfEditCookie cookie,
                                                       bool require_collapsed) {
    Observe observe(*this, Stage::Focus);
    const auto field = FreshFieldIdentity();
    if (!field || (field_ && !Check(field == field_)))
      return std::nullopt;
    if (!WritableContext())
      return std::nullopt;
    stage_ = Stage::Selection;
    TF_SELECTION selection{};
    ULONG fetched = 0;
    HRESULT result = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
    ComPtr<ITfRange> range;
    range.Attach(selection.range);
    if (!Exact(result) ||
        !Check(fetched == 1 && range && !selection.style.fInterimChar &&
               (selection.style.ase == TF_AE_NONE || selection.style.ase == TF_AE_START ||
                selection.style.ase == TF_AE_END)))
      return std::nullopt;
    if (!AllowedRange(cookie, range.Get(), *field))
      return std::nullopt;
    LONG caret = 0, length = 0;
    if (!Extent(range.Get(), caret, length) || !Check(!require_collapsed || length == 0) ||
        !AllowedRange(cookie, range.Get(), *field) ||
        !Check(FreshFieldIdentity() == field && Alive(), Cause::Stale))
      return std::nullopt;
    field_ = field;
    return SelectionObservation{range, {caret, length, selection.style}};
  }
  std::optional<AdmittedSelection> AcquireAdmittedSelection(TfEditCookie cookie,
                                                            bool require_collapsed) {
    auto selection = ObserveSelection(cookie, require_collapsed);
    if (!selection)
      return std::nullopt;
    return AdmittedSelection{*field_, selection->range, selection->value.start};
  }
  bool SelectionMatches(TfEditCookie cookie, const SelectionValue &expected) {
    auto selection = ObserveSelection(cookie, false);
    return selection && Check(selection->value == expected);
  }
  std::optional<FinishSelection> CaptureFinishSelection(TfEditCookie cookie) {
    auto selection = ObserveSelection(cookie, false);
    if (!selection)
      return std::nullopt;
    ComPtr<ITfRange> carrier;
    LONG start = 0, length = 0;
    if (!Exact(selection->range->Clone(&carrier)) || !Check(carrier != nullptr, Cause::Missing) ||
        !Extent(carrier.Get(), start, length) ||
        !Check(start == selection->value.start && length == selection->value.length))
      return std::nullopt;
    return FinishSelection{selection->value, carrier};
  }
  bool PreserveFinishSelection(TfEditCookie cookie, const FinishSelection &saved) {
    auto selection = ObserveSelection(cookie, false);
    if (!selection)
      return false;
    if (selection->value == saved.original)
      return true;
    // Clones can move with replacement gravity. Only the saved numeric extent is the target.
    LONG start = 0, length = 0, shifted = 0;
    ITfRange *carrier = saved.carrier.Get();
    const LONG end = saved.original.start + saved.original.length;
    if (!Extent(carrier, start, length) || !field_ || !AllowedRange(cookie, carrier, *field_))
      return false;
    LONG amount = end - (start + length);
    if (amount &&
        (!Exact(carrier->ShiftEnd(cookie, amount, &shifted, nullptr)) || !Check(shifted == amount)))
      return false;
    if (!Extent(carrier, start, length) || !Check(start + length == end) || !field_ ||
        !AllowedRange(cookie, carrier, *field_))
      return false;
    amount = saved.original.start - start;
    if (amount && (!Exact(carrier->ShiftStart(cookie, amount, &shifted, nullptr)) ||
                   !Check(shifted == amount)))
      return false;
    if (!Extent(carrier, start, length) ||
        !Check(start == saved.original.start && length == saved.original.length))
      return false;
    ComPtr<ITfContextComposition> cc;
    if (!NoForeign(cookie, carrier, cc) || !field_ || !AllowedRange(cookie, carrier, *field_))
      return false;
    TF_SELECTION restored{carrier, saved.original.style};
    return Exact(context_->SetSelection(cookie, 1, &restored)) &&
           SelectionMatches(cookie, saved.original);
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
    return field_ && AllowedRange(cookie, range, *field_) &&
           Good(range->GetText(cookie, 0, text, KR_TEXT_CAPACITY + 1, &count)) &&
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
    return field_ && AllowedRange(cookie, prefix.Get(), *field_) &&
           Good(prefix->GetText(cookie, 0, text, 2, &count)) &&
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
      if (!field_ || !AllowedRange(cookie, range.Get(), *field_) ||
          !Good(cc->StartComposition(cookie, range.Get(), &owner_, &composition)) || !composition)
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
    const bool preserve_selection = plan.action == KR_FINISH && desired == expected;
    const auto original =
        preserve_selection ? CaptureFinishSelection(cookie) : std::optional<FinishSelection>{};
    if (preserve_selection && (!original || !OwnedRange(cookie, expected, false, range)))
      return false;
    if ((original && !SelectionMatches(cookie, original->original)) || !field_ ||
        !AllowedRange(cookie, range.Get(), *field_))
      return false;
    diagnostics::Count(owner_.diagnostics_, Counter::Attempted);
    wrote_ = true;
    if (!Good(range->SetText(cookie, 0, desired.data(), static_cast<LONG>(desired.size()))))
      return false;
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
      if (!field_ || !AllowedRange(cookie, caret.Get(), *field_) ||
          !Good(context_->SetSelection(cookie, 1, &selection)))
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
      if (!field_ || !AllowedRange(cookie, actual.Get(), *field_))
        return false;
      diagnostics::Count(owner_.diagnostics_, Counter::Attempted);
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
      if (original) {
        if (!PreserveFinishSelection(cookie, *original) ||
            !Extent(actual.Get(), finished_start, finished_length) || finished_start != start ||
            finished_length != length || !Read(cookie, actual.Get(), desired) ||
            !NoForeign(cookie, actual.Get(), cc) || !SelectionMatches(cookie, original->original))
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
    auto admitted = AcquireAdmittedSelection(cookie, owner_.owned_ == nullptr);
    if (!admitted)
      return;
    if (owner_.owned_ && owner_.owned_->context_id.Get() != context_id_.Get()) {
      owner_.Refuse(Stage::Context, Cause::Predicate);
      owner_.Invalidate(true);
      return;
    }
    event_.context = KR_CTX_INELIGIBLE;
    if (!owner_.owned_) {
      ComPtr<ITfContextComposition> cc;
      if (!NoForeign(cookie, admitted->range.Get(), cc) ||
          !Boundary(cookie, admitted->range.Get(), admitted->caret))
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
      ComPtr<ITfContextComposition> cc;
      admitted = AcquireAdmittedSelection(cookie, true);
      if (!admitted || !NoForeign(cookie, admitted->range.Get(), cc) ||
          !Boundary(cookie, admitted->range.Get(), admitted->caret))
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
  std::optional<native_field::FieldIdentity> field_;
  Stage stage_ = Stage::Session;
  ULONG references_ = 1;
  bool entered_ = false;
  bool expired_ = false;
  bool wrote_ = false;
  bool outcome_recorded_ = false;
  uint32_t last_action_ = KR_PASS;
  uint32_t last_disposition_ = KR_KEY_PASS;
};

Service::Service(ITfInputProcessorProfileMgr *profile_api, decltype(&GetKeyboardLayoutList) layouts,
                 vocabulary::ModuleOwner module_owner, const native_field::Source *field_source)
    : profile_dependency_(profile_api), layouts_(layouts), field_source_(field_source),
      module_owner_(module_owner) {
  InterlockedIncrement(&object_count);
}
Service::~Service() {
  StopMaintenance();
  activation_ = Activation::Inactive;
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
  else if (id == runtime::reader_iid)
    *out = static_cast<runtime::Reader *>(this);
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
  else if (interface_id == runtime::reader_iid)
    *output = static_cast<runtime::Reader *>(this);
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
  output->active = Active();
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
        !diagnostics::SameActivation(Active(), diagnostics_.activation, diagnostics_.activation))
      return;
    ComPtr<Service> self(this);
    const auto client = native_ ? native_->client : TF_CLIENTID_NULL;
    ComPtr<ITfThreadMgr> manager = native_ ? native_->manager : nullptr;
    const auto activation = diagnostics_.activation;
    ComPtr<ITfSourceSingle> source;
    if (!manager || FAILED(manager.As(&source)) || !source ||
        !diagnostics::SameActivation(Active(), diagnostics_.activation, activation))
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
        diagnostics::SameActivation(Active(), diagnostics_.activation, activation)) {
      diagnostic_source_ = source;
      diagnostic_client_ = client;
    } else {
      UnregisterDiagnostics(source.Get(), client);
    }
  } catch (...) {
  }
}
bool Service::Current(uint64_t generation) const {
  return Active() && !stopped_ && generation_ == generation && GetCurrentThreadId() == thread_;
}
void Service::PublishObservation() noexcept {
  if (!host_publisher_.instance() || GetCurrentThreadId() != thread_)
    return;
  auto snapshot = diagnostics_;
  snapshot.active = Active();
  snapshot.stopped = stopped_;
  snapshot.mode = mode_;
  diagnostics::SnapshotV2 complete;
  complete.base = snapshot;
  complete.lifecycle = lifecycle_.Copy(owned_ != nullptr, owned_ ? owned_->diagnostic_serial : 0);
  host_publisher_.Publish(probe::Encode(host_identity_, host_metadata_, complete, runtime_,
                                        host_publisher_.instance()));
}
void Service::ObserveHost(ITfContext *context) {
  if (observing_host_ || observed_generation_ == generation_ || !Current(generation_) || !context)
    return;
  observed_generation_ = generation_;
  const auto captured = generation_;
  observing_host_ = true;
  struct Observation {
    bool &busy;
    ~Observation() { busy = false; }
  } observation{observing_host_};
  const auto client = native_ ? native_->client : TF_CLIENTID_NULL;
  ComPtr<ITfThreadMgr> manager = native_ ? native_->manager : nullptr;
  host_metadata_ = host::Observe(context, manager.Get(), client,
                                 {static_cast<ITfTextInputProcessorEx *>(this), this, generation_,
                                  [](void *state, uint64_t generation) noexcept {
                                    return static_cast<Service *>(state)->Current(generation);
                                  }});
  if (!Current(captured)) {
    host_metadata_ = {};
    host_metadata_.reason = host::Reason::Stale;
  }
}
void Service::Invalidate(bool stop) {
  host_metadata_ = {};
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
bool Service::Preparing(ActivationEpoch epoch) const {
  return activation_ == Activation::Preparing && activation_epoch_ == epoch;
}
bool Service::SameActivation(ActivationEpoch epoch) const {
  return Active() && activation_epoch_ == epoch;
}
HRESULT Service::AcquireActivation(ActivationAttempt &attempt) {
  auto &native = attempt.native;
  const auto epoch = attempt.epoch;
  HRESULT result = native.manager->QueryInterface(IID_PPV_ARGS(&native.source));
  if (result != S_OK || !native.source || !Preparing(epoch))
    return result == S_OK ? E_FAIL : result;
  result = native.manager->QueryInterface(IID_PPV_ARGS(&native.keys));
  if (result != S_OK || !native.keys || !Preparing(epoch))
    return result == S_OK ? E_FAIL : result;
  for (bool thread : {true, false}) {
    DWORD cookie = TF_INVALID_COOKIE;
    attempt.uncertain_acquisition = true;
    result = native.source->AdviseSink(
        thread ? IID_ITfThreadMgrEventSink : IID_ITfInputProcessorProfileActivationSink,
        thread
            ? static_cast<IUnknown *>(static_cast<ITfThreadMgrEventSink *>(this))
            : static_cast<IUnknown *>(static_cast<ITfInputProcessorProfileActivationSink *>(this)),
        &cookie);
    attempt.uncertain_acquisition = false;
    if (result != S_OK)
      return result;
    (thread ? native.thread_cookie : native.profile_cookie) = cookie;
    if (cookie == TF_INVALID_COOKIE) {
      attempt.uncertain_acquisition = true;
      return E_FAIL;
    }
    if (!Preparing(epoch))
      return E_FAIL;
  }
  attempt.uncertain_acquisition = true;
  result = native.keys->AdviseKeyEventSink(native.client, this, TRUE);
  attempt.uncertain_acquisition = false;
  if (result == S_OK)
    native.key_sink = true;
  if (result != S_OK || !Preparing(epoch))
    return result == S_OK ? E_FAIL : result;
  for (const auto &command : commands) {
    attempt.uncertain_acquisition = true;
    result = native.keys->PreserveKey(native.client, command.id, &command.key, command.description,
                                      static_cast<ULONG>(wcslen(command.description)));
    attempt.uncertain_acquisition = false;
    if (result == S_OK)
      ++native.command_prefix;
    if (result != S_OK || !Preparing(epoch))
      return result == S_OK ? E_FAIL : result;
  }
  if (profile_dependency_)
    native.profiles = profile_dependency_;
  else
    result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&native.profiles));
  if (!Preparing(epoch))
    return E_FAIL;
  if (result == S_OK && native.profiles) {
    const auto generation = generation_;
    TF_INPUTPROCESSORPROFILE profile{};
    result = native.profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile);
    if (result == S_OK && Preparing(epoch) && generation_ == generation)
      OnActivated(profile.dwProfileType, profile.langid, profile.clsid, profile.catid,
                  profile.guidProfile, profile.hkl, TF_IPSINK_FLAG_ACTIVE);
  }
  return Preparing(epoch) ? S_OK : E_FAIL;
}
void Service::CleanupResult::Record(HRESULT result, bool absent) noexcept {
  if (result == S_OK)
    return;
  if (error == S_OK)
    error = FAILED(result) ? result : E_FAIL;
  uncertain |= !absent;
}
Service::CleanupResult Service::DrainActivation(ActivationResources &native) noexcept {
  CleanupResult cleanup;
  while (native.command_prefix) {
    const auto &command = commands[--native.command_prefix];
    try {
      HRESULT result = native.keys->UnpreserveKey(command.id, &command.key);
      cleanup.Record(result, result == CONNECT_E_NOCONNECTION);
    } catch (...) {
      cleanup.Record(E_FAIL);
    }
  }
  if (std::exchange(native.key_sink, false)) {
    try {
      cleanup.Record(native.keys->UnadviseKeyEventSink(native.client));
    } catch (...) {
      cleanup.Record(E_FAIL);
    }
  }
  for (auto *entry : {&native.thread_cookie, &native.profile_cookie}) {
    const auto cookie = std::exchange(*entry, TF_INVALID_COOKIE);
    if (cookie == TF_INVALID_COOKIE)
      continue;
    try {
      cleanup.Record(native.source->UnadviseSink(cookie));
    } catch (...) {
      cleanup.Record(E_FAIL);
    }
  }
  native.client = TF_CLIENTID_NULL;
  native.profiles.Reset();
  native.keys.Reset();
  native.source.Reset();
  native.manager.Reset();
  return cleanup;
}
HRESULT Service::ActivateEx(ITfThreadMgr *manager, TfClientId client, DWORD flags) {
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  if (activation_ != Activation::Inactive || activation_call_open_ || !manager ||
      client == TF_CLIENTID_NULL || activation_epoch_.value == UINT64_MAX)
    return E_INVALIDARG;
  if (flags & TF_TMAE_SECUREMODE)
    return E_ACCESSDENIED;
  ComPtr<Service> self(this);
  DiagnosticCallback callback(*this);
  activation_call_open_ = true;
  struct ActivationFrame {
    bool &open;
    ~ActivationFrame() { open = false; }
  } frame{activation_call_open_};
  activation_ = Activation::Preparing;
  ActivationAttempt attempt{ActivationEpoch{++activation_epoch_.value}, {}};
  HRESULT result = E_FAIL;
  try {
    auto config = vocabulary::CaptureConfig();
    const bool invalid_config = !config;
    if (config)
      accepted_config_ = std::move(config);
    else if (!accepted_config_)
      accepted_config_ = vocabulary::Off{};
    runtime_ = {};
    runtime_.requested = vocabulary::Policy(*accepted_config_);
    runtime_.configuration_error =
        invalid_config ? vocabulary::Error::Configuration : vocabulary::Error::None;
    if ((!engine_ && kr_engine_new(KR_ABI_VERSION, &engine_) != KR_OK) ||
        kr_reset(engine_) != KR_OK || kr_set_keyboard(engine_, KR_KEYBOARD_PC) != KR_OK ||
        kr_adopt_dictionaries(engine_, nullptr, nullptr) != KR_OK ||
        kr_set_automatic_policy(engine_, runtime_.requested == KR_POLICY_MODEL
                                             ? KR_POLICY_OFF
                                             : runtime_.requested) != KR_OK) {
      activation_ = Activation::Inactive;
      return E_FAIL;
    }
    attempt.native.client = client;
    attempt.native.manager = manager;
    stopped_ = false;
    mode_ = 0;
    if (Preparing(attempt.epoch))
      result = AcquireActivation(attempt);
    if (result == S_OK && Preparing(attempt.epoch)) {
      native_.emplace(std::exchange(attempt.native, {}));
      activation_ = Activation::Active;
      diagnostics::Increment(diagnostics_.activation, diagnostics_.saturated);
      runtime_.activation = diagnostics_.activation;
      runtime_.effective =
          runtime_.requested == KR_POLICY_MODEL ? KR_POLICY_OFF : runtime_.requested;
      host::Capture(GetCurrentProcess(), GetCurrentProcessId(), thread_, host_identity_);
      host_metadata_ = {};
      observed_generation_ = 0;
      if (host_identity_.kind != host::Kind::Unknown)
        host_publisher_.Attach(host_identity_);
      if (SameActivation(attempt.epoch))
        RegisterDiagnostics();
      if (SameActivation(attempt.epoch) && !stopped_ && runtime_.requested == KR_POLICY_MODEL) {
        const auto runtime_generation = generation_;
        const bool maintenance = StartMaintenance();
        if (SameActivation(attempt.epoch) && Current(runtime_generation) &&
            (!maintenance || !vocabulary_.Start(std::get<vocabulary::Model>(*accepted_config_),
                                                runtime_.activation, module_owner_))) {
          StopMaintenance();
          runtime_.error = vocabulary::Error::Startup;
        }
      }
      result = SameActivation(attempt.epoch) ? S_OK : E_FAIL;
    }
  } catch (...) {
    result = E_FAIL;
  }
  if (SameActivation(attempt.epoch)) {
    if (result == S_OK)
      return S_OK;
    Deactivate();
  } else if (activation_ == Activation::Preparing || activation_ == Activation::Cancelling) {
    activation_ = Activation::Draining;
    InvalidateForLifecycle(diagnostics::LifecycleCause::Deactivate);
    mode_ = 0;
    const auto cleanup = DrainActivation(attempt.native);
    activation_ = cleanup.uncertain || attempt.uncertain_acquisition ? Activation::Faulted
                                                                     : Activation::Inactive;
  }
  return FAILED(result) ? result : E_FAIL;
}
void Service::InvalidateForLifecycle(diagnostics::LifecycleCause cause) {
  lifecycle_.Record(diagnostics::LifecycleKind::LifecycleInvalidation,
                    owned_ ? owned_->diagnostic_serial : 0, cause);
  Invalidate(owned_ != nullptr);
}
HRESULT Service::Copy(uint32_t size, uint32_t requested_version, runtime::Snapshot *output) {
  if (!output)
    return E_POINTER;
  ZeroMemory(output, (std::min)(static_cast<size_t>(size), sizeof(*output)));
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  if (size != sizeof(*output) || requested_version != runtime::version)
    return E_INVALIDARG;
  if (busy_ || diagnostic_depth_)
    return E_PENDING;
  *output = runtime_;
  return S_OK;
}
LRESULT CALLBACK Service::MaintenanceWindow(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_NCCREATE) {
    auto *create = reinterpret_cast<CREATESTRUCTW *>(l);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  if (message == WM_TIMER) {
    auto *service = reinterpret_cast<Service *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (service)
      service->Maintain();
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
bool Service::StartMaintenance() {
  const auto generation = generation_;
  HINSTANCE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&MaintenanceWindow), &module) ||
      !Current(generation))
    return false;
  WNDCLASSW type{};
  type.hInstance = module;
  type.lpszClassName = L"KeyrillSnapshotMaintenance";
  type.lpfnWndProc = MaintenanceWindow;
  if (!RegisterClassW(&type)) {
    WNDCLASSW existing{};
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS ||
        !GetClassInfoW(module, type.lpszClassName, &existing) ||
        existing.lpfnWndProc != MaintenanceWindow)
      return false;
  }
  if (!Current(generation)) {
    UnregisterClassW(type.lpszClassName, module);
    return false;
  }
  HWND window = CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                module, this);
  if (!Current(generation)) {
    if (window) {
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      DestroyWindow(window);
    }
    UnregisterClassW(type.lpszClassName, module);
    return false;
  }
  maintenance_window_ = window;
  maintenance_module_ = module;
  return window && SetTimer(window, 1, 100, nullptr) && Current(generation);
}
void Service::StopMaintenance() noexcept {
  vocabulary_.Stop();
  pending_ready_.reset();
  HWND window = std::exchange(maintenance_window_, nullptr);
  HINSTANCE module = std::exchange(maintenance_module_, nullptr);
  if (window) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    KillTimer(window, 1);
    DestroyWindow(window);
  }
  if (module)
    UnregisterClassW(L"KeyrillSnapshotMaintenance", module);
}
void Service::Maintain() noexcept {
  if (GetCurrentThreadId() != thread_ || !Active() || stopped_ || diagnostic_depth_ || busy_ ||
      key_admission_)
    return;
  try {
    if (auto latest = vocabulary_.Poll();
        latest && latest->activation == runtime_.activation && latest->revision > runtime_.revision)
      pending_ready_ = std::move(latest);
    if (!pending_ready_)
      return;
    auto &ready = *pending_ready_;
    if (kr_adopt_dictionaries(engine_, ready.sources[0].dictionary.get(),
                              ready.sources[1].dictionary.get()) != KR_OK) {
      runtime_.error = vocabulary::Error::Startup;
      return;
    }
    runtime_.model_ready = ready.model;
    runtime_.error = ready.error;
    for (size_t i = 0; i < ready.sources.size(); ++i) {
      runtime_.sources[i] = ready.sources[i].status;
      runtime_.source_errors[i] = ready.sources[i].error;
    }
    if (ready.model && runtime_.effective != KR_POLICY_MODEL) {
      if (owned_)
        return;
      if (kr_set_automatic_policy(engine_, KR_POLICY_MODEL) != KR_OK) {
        runtime_.error = vocabulary::Error::Startup;
        return;
      }
      runtime_.effective = KR_POLICY_MODEL;
    }
    runtime_.revision = ready.revision;
    pending_ready_.reset();
  } catch (...) {
    runtime_.error = vocabulary::Error::Startup;
  }
}
HRESULT Service::Deactivate() {
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  if (activation_ == Activation::Preparing) {
    activation_ = Activation::Cancelling;
    InvalidateForLifecycle(diagnostics::LifecycleCause::Deactivate);
    mode_ = 0;
    return S_OK;
  }
  if (!Active())
    return S_OK;
  ComPtr<Service> self(this);
  DiagnosticCallback callback(*this);
  activation_ = Activation::Draining;
  InvalidateForLifecycle(diagnostics::LifecycleCause::Deactivate);
  auto native = std::move(*native_);
  native_.reset();
  auto diagnostic_source = std::move(diagnostic_source_);
  const auto diagnostic_client = std::exchange(diagnostic_client_, TF_CLIENTID_NULL);
  if (owned_)
    lifecycle_.Record(diagnostics::LifecycleKind::Detached, owned_->diagnostic_serial);
  auto owned = std::move(owned_);
  mode_ = 0;
  StopMaintenance();
  const auto cleanup = DrainActivation(native);
  if (diagnostic_source && diagnostic_client != TF_CLIENTID_NULL)
    UnregisterDiagnostics(diagnostic_source.Get(), diagnostic_client);
  diagnostic_source.Reset();
  owned.reset();
  activation_ = cleanup.uncertain ? Activation::Faulted : Activation::Inactive;
  return cleanup.error;
}
bool Service::HandleEvent(ITfContext *context, const kr_event &event) {
  if (GetCurrentThreadId() != thread_)
    return false;
  DiagnosticCallback callback(*this);
  diagnostics::Count(diagnostics_, Counter::HandleEntered);
  if (!Active() || stopped_ || !context) {
    Refuse(Stage::Handle, !Active() ? Cause::Inactive : stopped_ ? Cause::Stopped : Cause::Missing);
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
      HRESULT request_result =
          context->RequestEditSession(native_ ? native_->client : TF_CLIENTID_NULL, request.Get(),
                                      TF_ES_SYNC | TF_ES_READWRITE, &session_result);
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
Service::KeyAdmission::KeyAdmission(Service &service, ITfContext *target)
    : owner(&service), context(target), epoch(service.activation_epoch_),
      generation(service.generation_), mode(service.mode_) {
  owner->key_admission_ = true;
}
Service::KeyAdmission::KeyAdmission(KeyAdmission &&other) noexcept
    : owner(std::exchange(other.owner, nullptr)), context(other.context), epoch(other.epoch),
      generation(other.generation), mode(other.mode), layout(other.layout) {
  std::copy(std::begin(other.state), std::end(other.state), state);
}
Service::KeyAdmission::~KeyAdmission() {
  if (owner)
    owner->key_admission_ = false;
}
bool Service::KeyAdmission::Dispatch(const kr_event &event) {
  auto *service = std::exchange(owner, nullptr);
  if (!service)
    return false;
  service->key_admission_ = false;
  if (!service->SameActivation(epoch) || !service->Current(generation) || service->mode_ != mode ||
      event.mode != mode) {
    service->Refuse(Stage::Mode, event.mode != mode ? Cause::Predicate : Cause::Stale);
    service->InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
    return false;
  }
  return service->HandleEvent(context, event);
}
std::optional<Service::KeyAdmission> Service::AdmitKey(ITfContext *context) {
  if (busy_ || key_admission_ || observing_host_) {
    Refuse(Stage::Key, Cause::Reentry);
    Invalidate(owned_ != nullptr);
    return std::nullopt;
  }
  if (!context) {
    Refuse(Stage::Key, Cause::Missing);
    return std::nullopt;
  }
  KeyAdmission admission(*this, context);
  const auto generation = admission.generation;
  const auto mode = admission.mode;
  auto &state = admission.state;
  if (!GetKeyboardState(state)) {
    Refuse(Stage::Keyboard, Cause::Predicate);
    InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
    return std::nullopt;
  }
  TF_INPUTPROCESSORPROFILE profile{};
  ComPtr<ITfInputProcessorProfileMgr> manager = native_ ? native_->profiles : nullptr;
  HRESULT profile_result = S_OK;
  if (!Current(generation) || !manager ||
      (profile_result = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile)) != S_OK ||
      !Current(generation)) {
    Refuse(Stage::Profile,
           profile_result != S_OK ? Cause::Native
           : !Active()            ? Cause::Inactive
           : stopped_             ? Cause::Stopped
           : !manager             ? Cause::Missing
                                  : Cause::Stale,
           profile_result != S_OK ? profile_result : S_OK);
    InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
    return std::nullopt;
  }
  if (!ActiveProfile(profile, mode)) {
    Cause cause = profile.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR ? Cause::WrongType
                  : profile.clsid != service_id                          ? Cause::WrongService
                  : !(profile.dwFlags & TF_IPP_FLAG_ACTIVE)              ? Cause::NotActive
                                                                         : Cause::WrongProfile;
    Refuse(Stage::Layout, cause);
    InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
    return std::nullopt;
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
    return std::nullopt;
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
    return std::nullopt;
  }
  HKL layout = ProfileLayout(profile, mode, actual);
  if (!layout || !layouts_) {
    Refuse(Stage::Layout, !layout ? Cause::Substitute : Cause::Missing);
    InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
    return std::nullopt;
  }
  HKL loaded[64]{};
  int count = layouts_(0, nullptr);
  if (!Current(generation) || count <= 0 || count > 64) {
    Refuse(Stage::Layout, !Current(generation) ? Cause::Stale : Cause::Substitute);
    InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
    return std::nullopt;
  }
  int filled = layouts_(64, loaded);
  if (!Current(generation) || filled != count) {
    Refuse(Stage::Layout, !Current(generation) ? Cause::Stale : Cause::Substitute);
    InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
    return std::nullopt;
  }
  int after = layouts_(0, nullptr);
  if (!Current(generation) || after != count ||
      std::find(loaded, loaded + count, layout) == loaded + count) {
    Refuse(Stage::Layout, !Current(generation) ? Cause::Stale : Cause::Substitute);
    InvalidateForLifecycle(diagnostics::LifecycleCause::KeyAdmission);
    return std::nullopt;
  }
  if (host_identity_.kind == host::Kind::PackagedNotepad &&
      !native_field::KnownNotepad(host_identity_)) {
    ObserveHost(context);
    return std::nullopt;
  }
  admission.layout = layout;
  return admission;
}
HRESULT Service::OnKeyDown(ITfContext *context, WPARAM key, LPARAM flags, BOOL *eaten) {
  if (!eaten)
    return E_POINTER;
  *eaten = FALSE;
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  ComPtr<Service> self(this);
  DiagnosticCallback callback(*this);
  try {
    diagnostics::Count(diagnostics_, Counter::KeyEntered);
    auto admission = AdmitKey(context);
    if (!admission)
      return S_OK;
    NativeKey translated = TranslateKey(key, flags, admission->layout, admission->state);
    if (const auto *event = std::get_if<kr_event>(&translated)) {
      *eaten = admission->Dispatch(*event);
      if (*eaten)
        diagnostics::Count(diagnostics_, Counter::Consumed);
    }
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
  *eaten = Active() && !stopped_ && mode_ != 0;
  if (*eaten)
    diagnostics::Count(diagnostics_, Counter::Interested);
  else
    Refuse(Stage::Test, !Active() ? Cause::Inactive : stopped_ ? Cause::Stopped : Cause::ModeUnset);
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
HRESULT Service::OnPreservedKey(ITfContext *context, REFGUID id, BOOL *eaten) {
  if (!eaten)
    return E_POINTER;
  *eaten = FALSE;
  if (GetCurrentThreadId() != thread_)
    return RPC_E_WRONG_THREAD;
  const auto command = std::find_if(commands.begin(), commands.end(),
                                    [&](const CommandSpec &entry) { return entry.id == id; });
  if (command == commands.end())
    return S_OK;
  ComPtr<Service> self(this);
  DiagnosticCallback callback(*this);
  try {
    auto admission = AdmitKey(context);
    if (!admission || !ExactCommandModifiers(admission->state))
      return S_OK;
    kr_event event{
        static_cast<uint32_t>(command->intent == ManualIntent::Convert ? KR_MANUAL : KR_REVERSE),
        admission->mode,
        KR_CTX_INELIGIBLE,
        0,
        {}};
    *eaten = admission->Dispatch(event);
    return S_OK;
  } catch (...) {
    Refuse(Stage::Key, Cause::Exception);
    Invalidate(true);
    return E_FAIL;
  }
}
HRESULT Service::OnSetFocus(BOOL foreground) {
  DiagnosticCallback callback(*this);
  if (!foreground)
    InvalidateForLifecycle(diagnostics::LifecycleCause::ForegroundLost);
  return S_OK;
}
HRESULT Service::OnInitDocumentMgr(ITfDocumentMgr *) { return S_OK; }
HRESULT Service::OnUninitDocumentMgr(ITfDocumentMgr *) {
  DiagnosticCallback callback(*this);
  InvalidateForLifecycle(diagnostics::LifecycleCause::DocumentUninit);
  return S_OK;
}
HRESULT Service::OnSetFocus(ITfDocumentMgr *, ITfDocumentMgr *) {
  DiagnosticCallback callback(*this);
  InvalidateForLifecycle(diagnostics::LifecycleCause::DocumentFocus);
  return S_OK;
}
HRESULT Service::OnPushContext(ITfContext *) {
  DiagnosticCallback callback(*this);
  InvalidateForLifecycle(diagnostics::LifecycleCause::ContextPush);
  return S_OK;
}
HRESULT Service::OnPopContext(ITfContext *) {
  DiagnosticCallback callback(*this);
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
  DiagnosticCallback callback(*this);
  InvalidateForLifecycle(diagnostics::LifecycleCause::ProfileChanged);
  mode_ = 0;
  if (clsid == service_id && (flags & TF_IPSINK_FLAG_ACTIVE))
    for (const auto &entry : profiles)
      if (profile == entry.id)
        mode_ = entry.mode;
  return S_OK;
}
} // namespace keyrill
