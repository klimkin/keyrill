#include "denial_fixture.h"
#include "fixture.h"
#include "service.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <thread>
#include <vector>

using namespace keyrill;
namespace {
void Require(bool condition, const char *label) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", label);
    std::exit(1);
  }
}
void NativeDenialGateSchedules() {
  using namespace diagnostics;
  bool (*volatile matches)(denial::Setup, const PublicationV2 &, const PublicationV2 &) noexcept =
      denial::Matches;
  const wchar_t *normal[] = {L"KeyrillProofEditor.exe"};
  const wchar_t *denied[] = {normal[0], L"--deny-tsf-locks"};
  const wchar_t *unknown[] = {normal[0], L"--unknown"};
  const wchar_t *duplicate[] = {normal[0], denied[1], denied[1]};
  const wchar_t *extra[] = {normal[0], denied[1], L"extra"};
  Require(denial::ParseStartup(1, normal) == denial::Startup::Normal &&
              denial::ParseStartup(2, denied) == denial::Startup::DenyTsfLocks &&
              denial::ParseStartup(2, unknown) == denial::Startup::Invalid &&
              denial::ParseStartup(3, duplicate) == denial::Startup::Invalid &&
              denial::ParseStartup(3, extra) == denial::Startup::Invalid,
          "startup accepts only no arguments or one denial flag");
  Require(denial::ParseSetup(denial::setup_status) == denial::Setup::Confirmed &&
              denial::ParseSetup(L"") == denial::Setup::Unknown &&
              denial::ParseSetup(L"Keyrill native denial v2; setup=Confirmed") ==
                  denial::Setup::Unknown,
          "denial setup requires the exact known confirmation");
  PublicationV2 first;
  first.publication = 10;
  first.source = 2;
  first.availability = Availability::Available;
  first.continuity = Continuity::Same;
  auto &s = first.snapshot.base;
  s.activation = 3;
  s.active = 1;
  s.mode = KR_EN;
  s.request_present = s.session_present = 1;
  s.request_result = S_OK;
  s.session_result = static_cast<uint32_t>(TF_E_SYNCHRONOUS);
  s.counts[static_cast<size_t>(Counter::Requested)] = 8;
  Refuse(s, Stage::Session, Cause::Native, TF_E_SYNCHRONOUS);
  PublicationV2 second = first;
  ++second.publication;
  Require(matches(denial::Setup::Confirmed, first, second),
          "fresh matching native session denial with zero effects is accepted");
  auto request_first = first;
  auto &r = request_first.snapshot.base;
  r.request_result = static_cast<uint32_t>(E_ACCESSDENIED);
  r.session_present = r.session_result = 0;
  r.refusals = {};
  r.first = r.latest = {};
  Refuse(r, Stage::Request, Cause::Native, E_ACCESSDENIED);
  auto request_second = request_first;
  ++request_second.publication;
  Require(matches(denial::Setup::Confirmed, request_first, request_second),
          "fresh matching failed native request with zero effects is accepted");
  Require(!matches(denial::Setup::Unknown, first, second),
          "normal or unconfirmed host cannot establish denial");
  auto reject = [&](const PublicationV2 &changed, const char *label) {
    Require(!matches(denial::Setup::Confirmed, first, changed), label);
  };
  auto changed = second;
  changed.availability = Availability::Missing;
  changed.continuity = Continuity::None;
  changed.snapshot = {};
  reject(changed, "provider absence cannot establish denial");
  changed = second;
  changed.snapshot.base.counts[static_cast<size_t>(Counter::Requested)] = 0;
  reject(changed, "no edit requests cannot establish denial");
  for (uint32_t mode : {0u, static_cast<uint32_t>(KR_RU)}) {
    changed = second;
    changed.snapshot.base.mode = mode;
    reject(changed, "wrong or unknown source mode cannot establish EN denial");
  }
  reject(first, "unchanged publication cannot establish freshness");
  changed = second;
  changed.publication = first.publication - 1;
  reject(changed, "older publication cannot establish freshness");
  changed = second;
  changed.snapshot.base.session_result = S_OK;
  reject(changed, "successful session cannot establish native denial");
  changed = second;
  changed.snapshot.base.request_result = S_FALSE;
  reject(changed, "non-S_OK success request cannot certify session denial");
  changed = second;
  changed.snapshot.base.session_present = changed.snapshot.base.session_result = 0;
  reject(changed, "absent session result cannot certify denial");
  for (auto effect : {Counter::SessionEntered, Counter::SessionCompleted, Counter::Attempted,
                      Counter::Verified, Counter::Unknown, Counter::Consumed}) {
    changed = second;
    changed.snapshot.base.counts[static_cast<size_t>(effect)] = 1;
    reject(changed, "callback entry or document or key effect cannot establish zero-effect denial");
    auto dirty_first = first;
    dirty_first.snapshot.base.counts[static_cast<size_t>(effect)] = 1;
    Require(!matches(denial::Setup::Confirmed, dirty_first, second),
            "earlier sample effects cannot disappear into a clean sample");
  }
  for (int identity = 0; identity != 3; ++identity) {
    changed = second;
    if (identity == 0)
      ++changed.source;
    else if (identity == 1)
      ++changed.snapshot.base.activation;
    else
      changed.continuity = Continuity::Reset;
    reject(changed, "foreign or reset publication identity cannot establish continuity");
  }
  changed = second;
  changed.snapshot.base.latest.result = static_cast<uint32_t>(E_FAIL);
  reject(changed, "unmatched native refusal cannot establish request outcome");
  changed = second;
  changed.snapshot.base.latest.cause = Cause::Predicate;
  changed.snapshot.base.latest.result = 0;
  reject(changed, "predicate refusal cannot establish native denial");
  changed = second;
  changed.snapshot.base.stopped = 1;
  reject(changed, "stopped service cannot establish live denial");
  changed = second;
  changed.saturated = changed.snapshot.base.saturated = 1;
  changed.continuity = Continuity::Indeterminate;
  reject(changed, "saturated observations cannot establish fresh denial");
  changed = second;
  changed.availability = Availability::Busy;
  changed.continuity = Continuity::None;
  changed.snapshot = {};
  reject(changed, "unavailable callback snapshot cannot establish denial");
}
struct FixtureScopeObservation {
  HWND window;
  int id;
  InputScope scope;
  bool alive;
  bool owning_thread;
};
struct FixtureScopeCalls {
  FixtureScopeObservation assignments[4]{};
  FixtureScopeObservation removals[4]{};
  UINT assignments_count = 0;
  UINT removals_count = 0;
  bool overflow = false;
  bool removing = false;
  UINT fail_assignment = 0;
  bool throw_assignment = false;
  UINT fail_removal = 0;
  UINT throw_removal = 0;
  HWND destroyed = nullptr;
  UINT destructions = 0;
  HRESULT removal_result = S_OK;
  void Record(HWND window, InputScope scope) noexcept {
    auto &count = removing ? removals_count : assignments_count;
    auto *calls = removing ? removals : assignments;
    if (count < 4)
      calls[count] = {window, GetDlgCtrlID(window), scope, IsWindow(window) != FALSE,
                      GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId()};
    else
      overflow = true;
    ++count;
  }
} fixture_scope_calls;
FixtureScopeSetter fixture_reset = SetFixtureInputScope;
HRESULT WINAPI ObserveFixtureScope(HWND window, InputScope scope) {
  fixture_scope_calls.Record(window, scope);
  if (fixture_scope_calls.removing) {
    if (fixture_scope_calls.removals_count == fixture_scope_calls.throw_removal)
      throw 1;
    if (fixture_scope_calls.removals_count == fixture_scope_calls.fail_removal)
      return E_ACCESSDENIED;
  } else if (fixture_scope_calls.assignments_count == fixture_scope_calls.fail_assignment) {
    if (fixture_scope_calls.throw_assignment)
      throw 1;
    return E_ACCESSDENIED;
  }
  return SetFixtureInputScope(window, scope);
}
LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_DESTROY) {
    fixture_scope_calls.removing = true;
    fixture_scope_calls.removal_result = ResetFixtureInputScopes(window, fixture_reset);
    fixture_scope_calls.removing = false;
    fixture_scope_calls.destroyed = window;
    ++fixture_scope_calls.destructions;
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
void FixtureScopeOracle(UINT children, HRESULT expected) {
  Require(!fixture_scope_calls.overflow && fixture_scope_calls.assignments_count == children &&
              fixture_scope_calls.removals_count == children &&
              fixture_scope_calls.destructions == 1 &&
              fixture_scope_calls.removal_result == expected &&
              !IsWindow(fixture_scope_calls.destroyed),
          "fixture attempts every known child reset once before root destruction");
  const InputScope expected_scopes[] = {IS_TEXT, IS_TEXT, IS_DEFAULT, IS_PASSWORD};
  for (UINT i = 0; i != children; ++i) {
    const auto &assigned = fixture_scope_calls.assignments[i];
    const auto &removed = fixture_scope_calls.removals[i];
    Require(assigned.id == static_cast<int>(101 + i) && assigned.scope == expected_scopes[i] &&
                assigned.alive && assigned.owning_thread && removed.id == assigned.id &&
                removed.window == assigned.window && removed.scope == IS_DEFAULT && removed.alive &&
                removed.owning_thread && !IsWindow(removed.window),
            "fixture declares exact scopes and resets live children on their owning thread");
  }
}
void FixtureLifecycle() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  fixture_reset = ObserveFixtureScope;
  fixture_scope_calls = {};
  HWND root = CreateFixture(instance, FixtureProc, false, ObserveFixtureScope);
  Require(root != nullptr, "observed fixture created");
  Require((GetWindowLongW(GetDlgItem(root, 103), GWL_STYLE) & ES_READONLY) &&
              (GetWindowLongW(GetDlgItem(root, 104), GWL_STYLE) & ES_PASSWORD),
          "fixture read-only and password styles remain excluded");
  Require(DestroyWindow(root) != FALSE, "observed fixture destroyed");
  FixtureScopeOracle(4, S_OK);
  for (bool throwing : {false, true}) {
    for (UINT position = 1; position != 5; ++position) {
      fixture_scope_calls = {};
      fixture_scope_calls.fail_assignment = position;
      fixture_scope_calls.throw_assignment = throwing;
      Require(!CreateFixture(instance, FixtureProc, false, ObserveFixtureScope),
              "failed or throwing setup returns no partial fixture");
      FixtureScopeOracle(position, S_OK);
    }
  }
  for (int scenario = 0; scenario != 2; ++scenario) {
    fixture_scope_calls = {};
    root = CreateFixture(instance, FixtureProc, false, ObserveFixtureScope);
    Require(root != nullptr, "fixture for reset failure created");
    fixture_scope_calls.fail_removal = scenario == 1 ? 2 : 1;
    fixture_scope_calls.throw_removal = scenario == 1 ? 1 : 2;
    Require(DestroyWindow(root) != FALSE, "fixture with reset failure destroyed");
    FixtureScopeOracle(4, scenario == 1 ? E_UNEXPECTED : E_ACCESSDENIED);
  }
  fixture_scope_calls = {};
  Require(!CreateFixture(instance, DefWindowProcW, false, ObserveFixtureScope) &&
              fixture_scope_calls.assignments_count == 0 && fixture_scope_calls.destructions == 0,
          "fixture class reuse refuses a different window procedure");
  fixture_reset = SetFixtureInputScope;
}
#define STUB(name, args)                                                                           \
  HRESULT STDMETHODCALLTYPE name args override { return E_NOTIMPL; }
#define REFCOUNT                                                                                   \
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }                               \
  ULONG STDMETHODCALLTYPE Release() override {                                                     \
    ULONG n = --references;                                                                        \
    if (!n)                                                                                        \
      delete this;                                                                                 \
    return n;                                                                                      \
  }                                                                                                \
  ULONG references = 1

struct Document;
struct ApplicationScopeProperty {
  HRESULT result = S_OK;
  bool present = true;
  HRESULT value_result = S_OK;
  VARTYPE value_type = VT_UNKNOWN;
  bool value_present = true;
  HRESULT scopes_result = S_OK;
  bool scopes_present = true;
  UINT scopes_count = 1;
  InputScope second_scope = IS_TEXT;
};
struct Span {
  LONG start, length;
};
struct Range final : ITfRangeACP {
  Document &document;
  std::shared_ptr<Span> span;
  Range(Document &d, LONG start, LONG length)
      : document(d), span(std::make_shared<Span>(Span{start, length})) {}
  Range(Document &d, std::shared_ptr<Span> s) : document(d), span(std::move(s)) {}
  REFCOUNT;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
  HRESULT STDMETHODCALLTYPE GetText(TfEditCookie, DWORD, WCHAR *, ULONG, ULONG *) override;
  HRESULT STDMETHODCALLTYPE SetText(TfEditCookie, DWORD, const WCHAR *, LONG) override;
  HRESULT STDMETHODCALLTYPE ShiftStart(TfEditCookie, LONG, LONG *, const TF_HALTCOND *) override;
  HRESULT STDMETHODCALLTYPE Collapse(TfEditCookie, TfAnchor) override;
  HRESULT STDMETHODCALLTYPE Clone(ITfRange **) override;
  HRESULT STDMETHODCALLTYPE GetContext(ITfContext **) override;
  HRESULT STDMETHODCALLTYPE GetExtent(LONG *, LONG *) override;
  STUB(SetExtent, (LONG, LONG));
  STUB(GetFormattedText, (TfEditCookie, IDataObject **));
  STUB(GetEmbedded, (TfEditCookie, REFGUID, REFIID, IUnknown **));
  STUB(InsertEmbedded, (TfEditCookie, DWORD, IDataObject *));
  STUB(ShiftEnd, (TfEditCookie, LONG, LONG *, const TF_HALTCOND *));
  STUB(ShiftStartToRange, (TfEditCookie, ITfRange *, TfAnchor));
  STUB(ShiftEndToRange, (TfEditCookie, ITfRange *, TfAnchor));
  STUB(ShiftStartRegion, (TfEditCookie, TfShiftDir, BOOL *));
  STUB(ShiftEndRegion, (TfEditCookie, TfShiftDir, BOOL *));
  STUB(IsEmpty, (TfEditCookie, BOOL *));
  STUB(IsEqualStart, (TfEditCookie, ITfRange *, TfAnchor, BOOL *));
  STUB(IsEqualEnd, (TfEditCookie, ITfRange *, TfAnchor, BOOL *));
  STUB(CompareStart, (TfEditCookie, ITfRange *, TfAnchor, LONG *));
  STUB(CompareEnd, (TfEditCookie, ITfRange *, TfAnchor, LONG *));
  STUB(AdjustForInsert, (TfEditCookie, ULONG, BOOL *));
  STUB(GetGravity, (TfGravity *, TfGravity *));
  STUB(SetGravity, (TfEditCookie, TfGravity, TfGravity));
};
struct Enumeration final : IEnumITfCompositionView {
  bool present;
  bool extra;
  ComPtr<ITfCompositionView> composition;
  std::function<void()> call;
  explicit Enumeration(bool p, ITfCompositionView *c = nullptr, bool e = false,
                       std::function<void()> h = {})
      : present(p), extra(e), composition(c), call(std::move(h)) {}
  REFCOUNT;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_IEnumITfCompositionView)
      return E_NOINTERFACE;
    *out = static_cast<IEnumITfCompositionView *>(this);
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Next(ULONG, ITfCompositionView **view, ULONG *count) override {
    if (call)
      call();
    *view = nullptr;
    *count = present ? 1 : 0;
    bool found = present;
    if (present && composition) {
      *view = composition.Get();
      (*view)->AddRef();
    }
    present = extra;
    extra = false;
    return found ? S_OK : S_FALSE;
  }
  STUB(Clone, (IEnumITfCompositionView **));
  STUB(Reset, ());
  STUB(Skip, (ULONG));
};
struct Composition final : ITfComposition, ITfCompositionView {
  Document &document;
  std::shared_ptr<Span> span;
  Composition(Document &d, std::shared_ptr<Span> s) : document(d), span(std::move(s)) {}
  ULONG references = 1;
  HRESULT identity_result = S_OK;
  bool throw_after_identity = false;
  std::function<void()> release_hook;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (release_hook)
      release_hook();
    const auto count = --references;
    if (!count)
      delete this;
    return count;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
  HRESULT STDMETHODCALLTYPE GetRange(ITfRange **) override;
  HRESULT STDMETHODCALLTYPE EndComposition(TfEditCookie) override;
  HRESULT STDMETHODCALLTYPE GetOwnerClsid(CLSID *out) override {
    *out = service_id;
    return S_OK;
  }
  STUB(ShiftStart, (TfEditCookie, ITfRange *));
  STUB(ShiftEnd, (TfEditCookie, ITfRange *));
};
struct Document final : ITfContext,
                        ITfContextComposition,
                        ITfContextView,
                        ITfProperty,
                        ITfInputScope,
                        ITfCompartmentMgr,
                        ITfCompartment {
  HWND window = nullptr;
  std::wstring text = L"prefix suffix";
  LONG caret = 7;
  LONG selection_length = 0;
  DWORD status = 0;
  InputScope scope = IS_DEFAULT;
  ApplicationScopeProperty application_scope;
  bool disabled = false;
  bool empty_disabled = false;
  bool queried_empty = false;
  bool view_present = true;
  HRESULT scope_interface_result = S_OK;
  bool foreign = false;
  bool reject_composition = false;
  bool callback = true;
  bool throw_after_callback = false;
  HRESULT request_result = S_OK;
  HRESULT session_result = S_OK;
  HRESULT write_result = S_OK;
  HRESULT selection_result = S_OK;
  HRESULT end_result = S_OK;
  bool keep_composition = false;
  bool active_composition = false;
  int reads = 0, writes = 0, starts = 0, ends = 0;
  int max_request_writes = 0;
  int references = 1;
  ComPtr<Composition> composition;
  ComPtr<ITfEditSession> retained_request;
  std::map<std::string, int> calls;
  std::vector<std::string> trace;
  std::function<void(const char *)> hook;
  void Call(const char *name) {
    ++calls[name];
    trace.emplace_back(name);
    if (hook)
      hook(name);
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { return --references; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    Call("context.QI");
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITfContext)
      *out = static_cast<ITfContext *>(this);
    else if (id == IID_ITfContextComposition)
      *out = static_cast<ITfContextComposition *>(this);
    else if (id == IID_ITfContextView)
      *out = static_cast<ITfContextView *>(this);
    else if (id == IID_ITfProperty)
      *out = static_cast<ITfProperty *>(this);
    else if (id == IID_ITfReadOnlyProperty)
      *out = static_cast<ITfReadOnlyProperty *>(static_cast<ITfProperty *>(this));
    else if (id == IID_ITfInputScope) {
      if (FAILED(scope_interface_result))
        return scope_interface_result;
      *out = static_cast<ITfInputScope *>(this);
    } else if (id == IID_ITfCompartmentMgr)
      *out = static_cast<ITfCompartmentMgr *>(this);
    else if (id == IID_ITfCompartment)
      *out = static_cast<ITfCompartment *>(this);
    if (!*out)
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId client, ITfEditSession *request,
                                               DWORD flags, HRESULT *result) override {
    Require(client == 42 && flags == (TF_ES_SYNC | TF_ES_READWRITE),
            "sync granted-cookie protocol");
    int before = writes;
    Call("RequestEditSession");
    if (callback)
      request->DoEditSession(123);
    else
      retained_request = request;
    if (throw_after_callback) {
      retained_request = request;
      throw 1;
    }
    *result = session_result;
    max_request_writes = std::max(max_request_writes, writes - before);
    return request_result;
  }
  HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie ec, ULONG, ULONG, TF_SELECTION *out,
                                         ULONG *fetched) override {
    Require(ec == 123, "cookie is scoped");
    Call("GetSelection");
    out->range = new Range(*this, caret, selection_length);
    out->style = {TF_AE_NONE, FALSE};
    *fetched = 1;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie, ULONG, const TF_SELECTION *value) override {
    Call("SetSelection");
    auto *range = static_cast<Range *>(value->range);
    Require(range->span->length == 0, "no temporary selection of document text");
    caret = range->span->start;
    selection_length = 0;
    Call("SetSelection.after");
    return selection_result;
  }
  HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView **out) override {
    Call("GetActiveView");
    *out = view_present ? static_cast<ITfContextView *>(this) : nullptr;
    if (*out)
      AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetWnd(HWND *out) override {
    Call("GetWnd");
    *out = window;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS *out) override {
    Call("GetStatus");
    *out = {status, 0};
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetProperty(REFGUID id, ITfProperty **out) override {
    Call("GetProperty");
    Require(id == GUID_PROP_INPUTSCOPE, "only scope metadata property");
    *out = nullptr;
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID id, ITfReadOnlyProperty **out) override {
    Call("GetAppProperty");
    Require(id == GUID_PROP_INPUTSCOPE, "only scope metadata application property");
    *out = nullptr;
    if (application_scope.present) {
      *out = static_cast<ITfProperty *>(this);
      AddRef();
    }
    return application_scope.result;
  }
  HRESULT STDMETHODCALLTYPE GetValue(TfEditCookie, ITfRange *, VARIANT *out) override {
    Call("scope.GetValue");
    VariantInit(out);
    out->vt = application_scope.value_type;
    if (out->vt == VT_UNKNOWN && application_scope.value_present) {
      out->punkVal = static_cast<ITfInputScope *>(this);
      AddRef();
    }
    return application_scope.value_result;
  }
  HRESULT STDMETHODCALLTYPE GetInputScopes(InputScope **out, UINT *count) override {
    Call("GetInputScopes");
    *out = nullptr;
    *count = application_scope.scopes_count;
    if (application_scope.scopes_present) {
      *out = static_cast<InputScope *>(CoTaskMemAlloc(sizeof(InputScope) * std::max(1u, *count)));
      if (!*out)
        return E_OUTOFMEMORY;
      **out = scope;
      if (*count > 1)
        (*out)[1] = application_scope.second_scope;
    }
    return application_scope.scopes_result;
  }
  HRESULT STDMETHODCALLTYPE GetCompartment(REFGUID id, ITfCompartment **out) override {
    Call("GetCompartment");
    queried_empty = id == GUID_COMPARTMENT_EMPTYCONTEXT;
    *out = static_cast<ITfCompartment *>(this);
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetValue(VARIANT *out) override {
    Call("compartment.GetValue");
    VariantInit(out);
    out->vt = VT_I4;
    out->lVal = queried_empty ? empty_disabled : disabled;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE StartComposition(TfEditCookie, ITfRange *range, ITfCompositionSink *,
                                             ITfComposition **out) override {
    Call("StartComposition");
    ++starts;
    *out = nullptr;
    if (reject_composition)
      return S_OK;
    composition.Attach(new Composition(*this, static_cast<Range *>(range)->span));
    active_composition = true;
    *out = composition.Get();
    (*out)->AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE FindComposition(TfEditCookie, ITfRange *,
                                            IEnumITfCompositionView **out) override {
    Call("FindComposition");
    *out = new Enumeration(foreign || active_composition);
    return S_OK;
  }
  STUB(InWriteSession, (TfClientId, BOOL *));
  STUB(GetStart, (TfEditCookie, ITfRange **));
  STUB(GetEnd, (TfEditCookie, ITfRange **));
  STUB(EnumViews, (IEnumTfContextViews **));
  STUB(TrackProperties, (const GUID **, ULONG, const GUID **, ULONG, ITfReadOnlyProperty **));
  STUB(EnumProperties, (IEnumTfProperties **));
  STUB(GetDocumentMgr, (ITfDocumentMgr **));
  STUB(CreateRangeBackup, (TfEditCookie, ITfRange *, ITfRangeBackup **));
  HRESULT STDMETHODCALLTYPE EnumCompositions(IEnumITfCompositionView **out) override {
    Call("EnumCompositions");
    *out = new Enumeration(foreign || active_composition,
                           active_composition ? static_cast<ITfCompositionView *>(composition.Get())
                                              : nullptr,
                           foreign && active_composition, [&] { Call("enumerator.Next"); });
    return S_OK;
  }
  STUB(TakeOwnership,
       (TfEditCookie, ITfCompositionView *, ITfCompositionSink *, ITfComposition **));
  STUB(GetRangeFromPoint, (TfEditCookie, const POINT *, DWORD, ITfRange **));
  STUB(GetTextExt, (TfEditCookie, ITfRange *, RECT *, BOOL *));
  STUB(GetScreenExt, (RECT *));
  STUB(GetType, (GUID *));
  STUB(EnumRanges, (TfEditCookie, IEnumTfRanges **, ITfRange *));
  STUB(GetContext, (ITfContext **));
  STUB(FindRange, (TfEditCookie, ITfRange *, ITfRange **, TfAnchor));
  STUB(SetValueStore, (TfEditCookie, ITfRange *, ITfPropertyStore *));
  STUB(SetValue, (TfEditCookie, ITfRange *, const VARIANT *));
  STUB(Clear, (TfEditCookie, ITfRange *));
  STUB(GetPhrase, (BSTR **, UINT *));
  STUB(GetRegularExpression, (BSTR *));
  STUB(GetSRGS, (BSTR *));
  STUB(GetXML, (BSTR *));
  STUB(ClearCompartment, (TfClientId, REFGUID));
  STUB(EnumCompartments, (IEnumGUID **));
  STUB(SetValue, (TfClientId, const VARIANT *));
};
HRESULT Range::QueryInterface(REFIID id, void **out) {
  document.Call("range.QI");
  *out = nullptr;
  if (id != IID_IUnknown && id != IID_ITfRange && id != IID_ITfRangeACP)
    return E_NOINTERFACE;
  *out = static_cast<ITfRangeACP *>(this);
  AddRef();
  return S_OK;
}
HRESULT Range::GetText(TfEditCookie, DWORD, WCHAR *text, ULONG capacity, ULONG *count) {
  document.Call("GetText");
  ++document.reads;
  if (span->start < 0 || span->length < 0 ||
      span->start + span->length > static_cast<LONG>(document.text.size()))
    return E_FAIL;
  *count = std::min(capacity, static_cast<ULONG>(span->length));
  std::copy_n(document.text.data() + span->start, *count, text);
  return S_OK;
}
HRESULT Range::SetText(TfEditCookie, DWORD, const WCHAR *text, LONG length) {
  document.Call("SetText.before");
  ++document.writes;
  Require(document.active_composition && document.composition->span == span,
          "text writes address only the owned composition");
  document.text.replace(span->start, span->length, text, length);
  span->length = length;
  document.Call("SetText.after");
  return document.write_result;
}
HRESULT Range::GetContext(ITfContext **out) {
  document.Call("range.GetContext");
  *out = &document;
  document.AddRef();
  return S_OK;
}
HRESULT Range::GetExtent(LONG *start, LONG *length) {
  document.Call("GetExtent");
  *start = span->start;
  *length = span->length;
  return S_OK;
}
HRESULT Range::ShiftStart(TfEditCookie, LONG amount, LONG *shift, const TF_HALTCOND *) {
  document.Call("ShiftStart");
  *shift = std::max(amount, -span->start);
  span->start += *shift;
  span->length -= *shift;
  return S_OK;
}
HRESULT Range::Collapse(TfEditCookie, TfAnchor anchor) {
  document.Call("Collapse");
  if (anchor == TF_ANCHOR_END)
    span->start += span->length;
  span->length = 0;
  return S_OK;
}
HRESULT Range::Clone(ITfRange **out) {
  document.Call("Clone");
  *out = new Range(document, span->start, span->length);
  return S_OK;
}
HRESULT Composition::QueryInterface(REFIID id, void **out) {
  document.Call("composition.QI");
  *out = nullptr;
  if (FAILED(identity_result))
    return identity_result;
  if (id == IID_IUnknown || id == IID_ITfComposition)
    *out = static_cast<ITfComposition *>(this);
  else if (id == IID_ITfCompositionView)
    *out = static_cast<ITfCompositionView *>(this);
  if (!*out)
    return E_NOINTERFACE;
  AddRef();
  if (throw_after_identity)
    throw 1;
  return S_OK;
}
HRESULT Composition::GetRange(ITfRange **out) {
  document.Call("GetRange");
  *out = new Range(document, span);
  return S_OK;
}
HRESULT Composition::EndComposition(TfEditCookie) {
  document.Call("EndComposition");
  ++document.ends;
  if (!document.keep_composition)
    document.active_composition = false;
  document.Call("EndComposition.after");
  return document.end_result;
}

struct Manager final : ITfThreadMgr,
                       ITfDocumentMgr,
                       ITfSource,
                       ITfSourceSingle,
                       ITfKeystrokeMgr,
                       ITfCompartmentMgr,
                       ITfCompartment {
  Document *top = nullptr;
  bool disabled = false;
  bool diagnostic_source = true;
  bool throw_advise = false;
  bool throw_unadvise = false;
  HRESULT diagnostic_advise_result = S_OK;
  HRESULT diagnostic_unadvise_result = S_OK;
  HRESULT provider_result = S_OK;
  bool provider_null = false;
  std::function<void()> diagnostic_advise_hook;
  std::function<void()> diagnostic_unadvise_hook;
  ComPtr<IUnknown> diagnostic_sink;
  int diagnostic_advises = 0, diagnostic_unadvises = 0, provider_queries = 0;
  int references = 1;
  int key_unadvises = 0, thread_unadvises = 0, profile_unadvises = 0;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { return --references; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    *out = nullptr;
    if (top)
      top->Call("manager.QI");
    if (id == IID_IUnknown || id == IID_ITfThreadMgr)
      *out = static_cast<ITfThreadMgr *>(this);
    else if (id == IID_ITfDocumentMgr)
      *out = static_cast<ITfDocumentMgr *>(this);
    else if (id == IID_ITfSource)
      *out = static_cast<ITfSource *>(this);
    else if (id == IID_ITfSourceSingle && diagnostic_source)
      *out = static_cast<ITfSourceSingle *>(this);
    else if (id == IID_ITfKeystrokeMgr)
      *out = static_cast<ITfKeystrokeMgr *>(this);
    else if (id == IID_ITfCompartmentMgr)
      *out = static_cast<ITfCompartmentMgr *>(this);
    if (!*out)
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFocus(ITfDocumentMgr **out) override {
    top->Call("manager.GetFocus");
    *out = static_cast<ITfDocumentMgr *>(this);
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetTop(ITfContext **out) override {
    top->Call("GetTop");
    *out = top;
    top->AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE AdviseSink(REFIID id, IUnknown *, DWORD *cookie) override {
    *cookie = id == IID_ITfThreadMgrEventSink ? 1 : 2;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override {
    if (cookie == 1) {
      ++thread_unadvises;
      top->Call("UnadviseThreadSink");
    } else {
      Require(cookie == 2, "captured profile sink cookie");
      ++profile_unadvises;
      top->Call("UnadviseProfileSink");
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE AdviseKeyEventSink(TfClientId, ITfKeyEventSink *, BOOL) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE UnadviseKeyEventSink(TfClientId client) override {
    Require(client == 42, "captured teardown client");
    ++key_unadvises;
    top->Call("UnadviseKeySink");
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCompartment(REFGUID, ITfCompartment **out) override {
    top->Call("thread.GetCompartment");
    *out = static_cast<ITfCompartment *>(this);
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetValue(VARIANT *out) override {
    top->Call("thread.GetValue");
    VariantInit(out);
    out->vt = VT_I4;
    out->lVal = disabled;
    return S_OK;
  }
  STUB(ClearCompartment, (TfClientId, REFGUID));
  STUB(EnumCompartments, (IEnumGUID **));
  STUB(SetValue, (TfClientId, const VARIANT *));
  STUB(Activate, (TfClientId *));
  STUB(Deactivate, ());
  STUB(CreateDocumentMgr, (ITfDocumentMgr **));
  STUB(EnumDocumentMgrs, (IEnumTfDocumentMgrs **));
  STUB(SetFocus, (ITfDocumentMgr *));
  STUB(AssociateFocus, (HWND, ITfDocumentMgr *, ITfDocumentMgr **));
  STUB(IsThreadFocus, (BOOL *));
  HRESULT STDMETHODCALLTYPE GetFunctionProvider(REFCLSID id, ITfFunctionProvider **out) override {
    Require(id == service_id, "poll asks only for the actual service provider");
    ++provider_queries;
    *out = nullptr;
    if (provider_result != S_OK)
      return provider_result;
    if (provider_null)
      return S_OK;
    return diagnostic_sink ? diagnostic_sink->QueryInterface(IID_PPV_ARGS(out)) : TF_E_NOPROVIDER;
  }
  HRESULT STDMETHODCALLTYPE AdviseSingleSink(TfClientId client, REFIID id,
                                             IUnknown *sink) override {
    Require(client == 42 && id == IID_ITfFunctionProvider,
            "diagnostic registration captured client");
    ++diagnostic_advises;
    if (diagnostic_advise_hook)
      diagnostic_advise_hook();
    diagnostic_sink = sink;
    if (throw_advise)
      throw 1;
    return diagnostic_advise_result;
  }
  HRESULT STDMETHODCALLTYPE UnadviseSingleSink(TfClientId client, REFIID id) override {
    Require(client == 42 && id == IID_ITfFunctionProvider, "diagnostic cleanup captured client");
    ++diagnostic_unadvises;
    diagnostic_sink.Reset();
    if (diagnostic_unadvise_hook)
      diagnostic_unadvise_hook();
    if (throw_unadvise)
      throw 1;
    return diagnostic_unadvise_result;
  }
  STUB(EnumFunctionProviders, (IEnumTfFunctionProviders **));
  STUB(GetGlobalCompartment, (ITfCompartmentMgr **));
  STUB(CreateContext, (TfClientId, DWORD, IUnknown *, ITfContext **, TfEditCookie *));
  STUB(Push, (ITfContext *));
  STUB(Pop, (DWORD));
  STUB(GetBase, (ITfContext **));
  STUB(EnumContexts, (IEnumTfContexts **));
  STUB(GetForeground, (CLSID *));
  STUB(TestKeyDown, (WPARAM, LPARAM, BOOL *));
  STUB(TestKeyUp, (WPARAM, LPARAM, BOOL *));
  STUB(KeyDown, (WPARAM, LPARAM, BOOL *));
  STUB(KeyUp, (WPARAM, LPARAM, BOOL *));
  STUB(GetPreservedKey, (ITfContext *, const TF_PRESERVEDKEY *, GUID *));
  STUB(IsPreservedKey, (REFGUID, const TF_PRESERVEDKEY *, BOOL *));
  STUB(PreserveKey, (TfClientId, REFGUID, const TF_PRESERVEDKEY *, const WCHAR *, ULONG));
  STUB(UnpreserveKey, (REFGUID, const TF_PRESERVEDKEY *));
  STUB(SetPreservedKeyDescription, (REFGUID, const WCHAR *, ULONG));
  STUB(GetPreservedKeyDescription, (REFGUID, BSTR *));
  STUB(SimulatePreservedKey, (ITfContext *, REFGUID, BOOL *));
};

kr_event Text(wchar_t letter) {
  kr_event event{KR_TEXT, KR_EN, KR_CTX_INELIGIBLE, 1, {static_cast<uint16_t>(letter)}};
  return event;
}
HWND controlled_root = nullptr;
struct ProfileManager final : ITfInputProcessorProfileMgr,
                              ITfInputProcessorProfileSubstituteLayout {
  ULONG references = 1;
  TF_INPUTPROCESSORPROFILE profile{};
  HRESULT active_result = S_OK, query_result = S_OK, substitute_result = S_OK;
  bool query_null = false;
  HKL substitute = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04090409));
  int active_calls = 0, query_calls = 0, substitute_calls = 0;
  std::function<void(const char *)> hook;
  ProfileManager() {
    profile.dwProfileType = TF_PROFILETYPE_INPUTPROCESSOR;
    profile.clsid = service_id;
    profile.guidProfile = profiles[0].id;
    profile.langid = profiles[0].language;
    profile.dwFlags = TF_IPP_FLAG_ACTIVE;
    profile.hkl = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0xe0200409));
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { return --references; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITfInputProcessorProfileMgr)
      *out = static_cast<ITfInputProcessorProfileMgr *>(this);
    else if (id == IID_ITfInputProcessorProfileSubstituteLayout) {
      ++query_calls;
      if (hook)
        hook("profiles.QI");
      if (!query_null)
        *out = static_cast<ITfInputProcessorProfileSubstituteLayout *>(this);
      if (*out)
        AddRef();
      return query_result;
    }
    if (!*out)
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetActiveProfile(REFGUID category,
                                             TF_INPUTPROCESSORPROFILE *out) override {
    Require(category == GUID_TFCAT_TIP_KEYBOARD, "lookup is restricted to keyboard profiles");
    ++active_calls;
    *out = profile;
    if (hook)
      hook("GetActiveProfile");
    return active_result;
  }
  HRESULT STDMETHODCALLTYPE GetSubstituteKeyboardLayout(REFCLSID clsid, LANGID language, REFGUID id,
                                                        HKL *out) override {
    Require(clsid == profile.clsid && language == profile.langid && id == profile.guidProfile,
            "dedicated lookup uses the exact captured active profile");
    ++substitute_calls;
    *out = substitute;
    if (hook)
      hook("GetSubstituteKeyboardLayout");
    return substitute_result;
  }
  STUB(ActivateProfile, (DWORD, LANGID, REFCLSID, REFGUID, HKL, DWORD));
  STUB(DeactivateProfile, (DWORD, LANGID, REFCLSID, REFGUID, HKL, DWORD));
  STUB(GetProfile, (DWORD, LANGID, REFCLSID, REFGUID, HKL, TF_INPUTPROCESSORPROFILE *));
  STUB(EnumProfiles, (LANGID, IEnumTfInputProcessorProfiles **));
  STUB(ReleaseInputProcessor, (REFCLSID, DWORD));
  STUB(RegisterProfile, (REFCLSID, LANGID, REFGUID, const WCHAR *, ULONG, const WCHAR *, ULONG,
                         ULONG, HKL, DWORD, BOOL, DWORD));
  STUB(UnregisterProfile, (REFCLSID, LANGID, REFGUID, DWORD));
};
struct Harness {
  HWND root = nullptr;
  Document a, b;
  Manager manager;
  ComPtr<Service> service;
  Harness(ITfInputProcessorProfileMgr *profile_api = nullptr,
          decltype(&GetKeyboardLayoutList) layouts = GetKeyboardLayoutList) {
    root = controlled_root;
    Require(root != nullptr, "controlled RichEdit metadata fixture created");
    a.window = GetDlgItem(root, 101);
    b.window = GetDlgItem(root, 102);
    manager.top = &a;
    ::SetActiveWindow(root);
    ::SetFocus(a.window);
    Require(::GetFocus() == a.window, "fixture native focus established");
    service.Attach(new Service(profile_api, layouts));
    Require(SUCCEEDED(service->Activate(&manager, 42)), "controlled COM service activation");
  }
  ~Harness() {
    a.hook = nullptr;
    b.hook = nullptr;
    service->Deactivate();
    a.retained_request.Reset();
    b.retained_request.Reset();
    service.Reset();
  }
  bool Key(wchar_t letter, bool pass = true) {
    bool eaten = service->HandleEvent(&a, Text(letter));
    if (!eaten && pass) {
      a.text.insert(a.caret, 1, letter);
      ++a.caret;
    }
    return eaten;
  }
  void Type(const wchar_t *text) {
    for (const wchar_t *p = text; *p; ++p)
      Require(Key(*p), "owned letter eaten");
  }
  void Oracle(const wchar_t *middle, LONG caret) {
    Require(a.text == std::wstring(L"prefix ") + middle + L"suffix",
            "whole document prefix and suffix preserved");
    Require(a.caret == caret, "exact UTF-16 caret");
    Require(b.text == L"prefix suffix" && b.caret == 7, "other document unchanged");
  }
};
diagnostics::Snapshot DiagnosticSnapshot(Service &service) {
  diagnostics::Snapshot snapshot;
  Require(service.Copy(sizeof(snapshot), diagnostics::version, &snapshot) == S_OK &&
              diagnostics::Valid(snapshot),
          "service copies one valid bounded scalar snapshot");
  return snapshot;
}
int WINAPI ProofLayouts(int capacity, HKL *out) {
  if (!capacity)
    return 2;
  Require(capacity >= 2 && out, "bounded complete loaded-layout observation");
  out[0] = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04090409));
  out[1] = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04190419));
  return 2;
}
int WINAPI MissingProofLayout(int capacity, HKL *out) {
  if (capacity)
    out[0] = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04090409));
  return 1;
}
int WINAPI IncompleteProofLayouts(int capacity, HKL *) { return capacity ? 1 : 2; }
int WINAPI ExcessProofLayouts(int, HKL *) { return 65; }
int WINAPI NoProofLayouts(int, HKL *) { return 0; }
struct LayoutSchedule {
  static inline thread_local LayoutSchedule *current = nullptr;
  std::function<void(int)> hook;
  int calls = 0;
  int final_count = 2;
  LayoutSchedule() {
    Require(!current, "one scoped loaded-layout fake owns this test thread");
    current = this;
  }
  ~LayoutSchedule() { current = nullptr; }
  static int WINAPI Observe(int capacity, HKL *out) {
    Require(current != nullptr, "loaded-layout callback has a live scoped test owner");
    auto &schedule = *current;
    int result = ++schedule.calls == 3 ? schedule.final_count : ProofLayouts(capacity, out);
    if (schedule.hook)
      schedule.hook(schedule.calls);
    return result;
  }
};
void DedicatedLayoutRegression() {
  using diagnostics::Counter;
  for (const auto &entry : profiles) {
    ProfileManager profile_api;
    profile_api.profile.guidProfile = entry.id;
    profile_api.profile.langid = entry.language;
    profile_api.substitute =
        reinterpret_cast<HKL>(static_cast<ULONG_PTR>(entry.language) << 16 | entry.language);
    {
      Harness h(&profile_api, ProofLayouts);
      for (HKL field : {static_cast<HKL>(nullptr), reinterpret_cast<HKL>(1)}) {
        profile_api.profile.hklSubstitute = field;
        BOOL eaten = TRUE;
        const auto before = DiagnosticSnapshot(*h.service.Get());
        Require(h.service->OnKeyDown(&h.a, VK_F12, 1, &eaten) == S_OK && !eaten,
                "nontext key passes after dedicated lookup");
        const auto after = DiagnosticSnapshot(*h.service.Get());
        Require(after.counts[static_cast<size_t>(Counter::HandleEntered)] ==
                    before.counts[static_cast<size_t>(Counter::HandleEntered)] + 1,
                "null active substitute field with valid dedicated layout reaches HandleEvent");
      }
      Require(profile_api.substitute_calls == 2 && h.a.writes == 0 && h.b.writes == 0,
              "each key independently queries its dedicated layout without text writes");
      h.Oracle(L"", 7);
    }
    Require(profile_api.references == 1, "profile dependencies release all local COM ownership");
  }
}
void LayoutRefusalOracle(Harness &h, const diagnostics::Snapshot &before) {
  using diagnostics::Counter;
  const auto after = DiagnosticSnapshot(*h.service.Get());
  for (auto counter :
       {Counter::HandleEntered, Counter::Requested, Counter::Attempted, Counter::Consumed})
    Require(after.counts[static_cast<size_t>(counter)] ==
                before.counts[static_cast<size_t>(counter)],
            "layout refusal never handles, requests, writes, or consumes");
  Require(h.a.writes == 0 && h.b.writes == 0 && h.a.reads == 0 && h.b.reads == 0,
          "layout refusal inspects no document text");
  h.Oracle(L"", 7);
}
void DedicatedLayoutNegatives() {
  using diagnostics::Cause;
  using diagnostics::Stage;
  for (int scenario = 0; scenario != 20; ++scenario) {
    ProfileManager profile_api;
    profile_api.profile.hklSubstitute = profile_api.substitute;
    decltype(&GetKeyboardLayoutList) layouts = ProofLayouts;
    Cause cause = Cause::Substitute;
    HRESULT result = S_OK;
    switch (scenario) {
    case 0:
      profile_api.active_result = E_FAIL;
      cause = Cause::Native;
      result = E_FAIL;
      break;
    case 1:
      profile_api.active_result = S_FALSE;
      cause = Cause::Native;
      result = S_FALSE;
      break;
    case 2:
      profile_api.query_result = E_NOINTERFACE;
      cause = Cause::Native;
      result = E_NOINTERFACE;
      break;
    case 3:
      profile_api.query_result = S_FALSE;
      cause = Cause::Native;
      result = S_FALSE;
      break;
    case 4:
      profile_api.query_null = true;
      cause = Cause::Missing;
      break;
    case 5:
      profile_api.substitute_result = E_FAIL;
      cause = Cause::Native;
      result = E_FAIL;
      break;
    case 6:
      profile_api.substitute_result = S_FALSE;
      cause = Cause::Native;
      result = S_FALSE;
      break;
    case 7:
      profile_api.substitute = nullptr;
      cause = Cause::Missing;
      break;
    case 8:
      profile_api.substitute = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0xe0200409));
      break;
    case 9:
      profile_api.substitute = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04190419));
      break;
    case 10:
      layouts = MissingProofLayout;
      profile_api.profile.guidProfile = profiles[1].id;
      profile_api.profile.langid = profiles[1].language;
      profile_api.substitute = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04190419));
      profile_api.profile.hklSubstitute = profile_api.substitute;
      break;
    case 11:
      layouts = IncompleteProofLayouts;
      break;
    case 12:
      layouts = ExcessProofLayouts;
      break;
    case 13:
      layouts = NoProofLayouts;
      break;
    case 14:
      layouts = nullptr;
      cause = Cause::Missing;
      break;
    case 15:
      profile_api.profile.dwProfileType = TF_PROFILETYPE_KEYBOARDLAYOUT;
      cause = Cause::WrongType;
      break;
    case 16:
      profile_api.profile.clsid = GUID_NULL;
      cause = Cause::WrongService;
      break;
    case 17:
      profile_api.profile.dwFlags = 0;
      cause = Cause::NotActive;
      break;
    case 18:
      profile_api.profile.guidProfile = GUID_NULL;
      cause = Cause::WrongProfile;
      break;
    case 19:
      profile_api.substitute = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0xffffffff04090409));
      break;
    }
    Harness h(&profile_api, layouts);
    const auto before = DiagnosticSnapshot(*h.service.Get());
    BOOL eaten = TRUE;
    Require(h.service->OnKeyDown(&h.a, VK_F12, 1, &eaten) == S_OK && !eaten,
            "invalid dedicated admission passes actual key unchanged");
    LayoutRefusalOracle(h, before);
    const auto after = DiagnosticSnapshot(*h.service.Get());
    Require(after.latest.stage == (scenario <= 1 ? Stage::Profile : Stage::Layout) &&
                after.latest.cause == cause && after.latest.result == static_cast<uint32_t>(result),
            "dedicated failures preserve the actual native result or predicate cause");
    if (scenario >= 15 && scenario <= 18)
      Require(profile_api.query_calls == 0 && profile_api.substitute_calls == 0,
              "foreign active identity is rejected before dedicated lookup");
  }
  for (int mismatch = 0; mismatch != 2; ++mismatch) {
    ProfileManager profile_api;
    Harness h(&profile_api, ProofLayouts);
    if (mismatch)
      profile_api.profile.langid = profiles[1].language;
    else
      h.service->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, profiles[1].language, service_id,
                             GUID_TFCAT_TIP_KEYBOARD, profiles[1].id, nullptr,
                             TF_IPSINK_FLAG_ACTIVE);
    const auto before = DiagnosticSnapshot(*h.service.Get());
    BOOL eaten = TRUE;
    Require(h.service->OnKeyDown(&h.a, VK_F12, 1, &eaten) == S_OK && !eaten,
            "mismatched profile language or selected mode passes unchanged");
    LayoutRefusalOracle(h, before);
    Require(profile_api.query_calls == 0 && profile_api.substitute_calls == 0,
            "mismatched profile identity cannot query a substitute");
  }
}
void DedicatedLayoutReentry() {
  for (const char *boundary : {"GetActiveProfile", "profiles.QI", "GetSubstituteKeyboardLayout"}) {
    for (int schedule = 0; schedule != 6; ++schedule) {
      ProfileManager profile_api;
      Harness h(&profile_api, ProofLayouts);
      bool injected = false;
      const auto before = DiagnosticSnapshot(*h.service.Get());
      profile_api.hook = [&](const char *name) {
        if (injected || std::string(name) != boundary)
          return;
        injected = true;
        if (schedule == 0) {
          BOOL nested = TRUE;
          Require(h.service->OnKeyDown(&h.a, VK_F12, 1, &nested) == S_OK && !nested,
                  "nested key during admission invalidates and passes unchanged");
        } else if (schedule == 1) {
          h.service->OnSetFocus(FALSE);
        } else if (schedule == 2) {
          Require(h.service->Deactivate() == S_OK && h.service->Activate(&h.manager, 42) == S_OK,
                  "deactivate and reactivate cannot revive captured admission");
        } else if (schedule == 3) {
          throw 1;
        } else if (schedule == 4) {
          Require(h.service->Deactivate() == S_OK, "deactivation cannot retain admission");
        } else {
          h.service->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, profiles[1].language, service_id,
                                 GUID_TFCAT_TIP_KEYBOARD, profiles[1].id, nullptr,
                                 TF_IPSINK_FLAG_ACTIVE);
        }
      };
      BOOL eaten = TRUE;
      const HRESULT result = h.service->OnKeyDown(&h.a, VK_F12, 1, &eaten);
      Require(injected && !eaten && result == (schedule == 3 ? E_FAIL : S_OK),
              "each reentrant native boundary refuses the outer key before handling");
      LayoutRefusalOracle(h, before);
      profile_api.hook = nullptr;
      if (schedule < 3) {
        const auto resumed = DiagnosticSnapshot(*h.service.Get());
        Require(h.service->OnKeyDown(&h.a, VK_F12, 1, &eaten) == S_OK && !eaten,
                "later fresh key can resume after scoped admission releases");
        const auto after = DiagnosticSnapshot(*h.service.Get());
        Require(after.counts[static_cast<size_t>(diagnostics::Counter::HandleEntered)] ==
                    resumed.counts[static_cast<size_t>(diagnostics::Counter::HandleEntered)] + 1,
                "scoped admission does not leave the service permanently busy");
      }
    }
  }
  for (int boundary = 1; boundary != 4; ++boundary) {
    for (int schedule = 0; schedule != 6; ++schedule) {
      LayoutSchedule layouts;
      ProfileManager profile_api;
      Harness h(&profile_api, LayoutSchedule::Observe);
      const auto before = DiagnosticSnapshot(*h.service.Get());
      layouts.hook = [&](int call) {
        if (call != boundary)
          return;
        if (schedule == 0) {
          BOOL nested = TRUE;
          Require(h.service->OnKeyDown(&h.a, VK_F12, 1, &nested) == S_OK && !nested,
                  "nested key at each loaded-layout boundary invalidates admission");
        } else if (schedule == 1) {
          h.service->OnSetFocus(FALSE);
        } else if (schedule == 2) {
          Require(h.service->Deactivate() == S_OK && h.service->Activate(&h.manager, 42) == S_OK,
                  "loaded-layout observation cannot survive reactivation");
        } else if (schedule == 3) {
          throw 1;
        } else if (schedule == 4) {
          Require(h.service->Deactivate() == S_OK,
                  "loaded-layout observation cannot outlive activation");
        } else {
          h.service->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, profiles[1].language, service_id,
                                 GUID_TFCAT_TIP_KEYBOARD, profiles[1].id, nullptr,
                                 TF_IPSINK_FLAG_ACTIVE);
        }
      };
      BOOL eaten = TRUE;
      Require(h.service->OnKeyDown(&h.a, VK_F12, 1, &eaten) == (schedule == 3 ? E_FAIL : S_OK) &&
                  !eaten && layouts.calls == boundary,
              "invalidation at each loaded-layout call stops all later admission work");
      LayoutRefusalOracle(h, before);
    }
  }
  LayoutSchedule layouts;
  layouts.final_count = 1;
  ProfileManager profile_api;
  Harness h(&profile_api, LayoutSchedule::Observe);
  const auto before = DiagnosticSnapshot(*h.service.Get());
  BOOL eaten = TRUE;
  Require(h.service->OnKeyDown(&h.a, VK_F12, 1, &eaten) == S_OK && !eaten && layouts.calls == 3,
          "changed count after a complete loaded-layout fill refuses admission");
  LayoutRefusalOracle(h, before);
}
void DiagnosticRefusalSchedules() {
  using diagnostics::Cause;
  using diagnostics::Counter;
  using diagnostics::Stage;
  for (int scenario = 0; scenario != 18; ++scenario) {
    Harness h;
    Stage stage = Stage::AppProperty;
    Cause cause = Cause::Native;
    HRESULT result = E_FAIL;
    auto &property = h.a.application_scope;
    switch (scenario) {
    case 0:
      property.result = E_FAIL;
      break;
    case 1:
      property.result = S_FALSE;
      result = S_FALSE;
      break;
    case 2:
      property.present = false;
      cause = Cause::Missing;
      result = S_OK;
      break;
    case 3:
      property.value_result = E_ACCESSDENIED;
      stage = Stage::AppValue;
      result = E_ACCESSDENIED;
      break;
    case 4:
      property.value_type = VT_EMPTY;
      stage = Stage::AppValue;
      cause = Cause::WrongType;
      result = S_OK;
      break;
    case 5:
      property.value_present = false;
      stage = Stage::AppValue;
      cause = Cause::Missing;
      result = S_OK;
      break;
    case 6:
      h.a.scope_interface_result = E_NOINTERFACE;
      stage = Stage::ScopeInterface;
      result = E_NOINTERFACE;
      break;
    case 7:
      property.scopes_result = S_FALSE;
      stage = Stage::Scopes;
      result = S_FALSE;
      break;
    case 8:
      property.scopes_count = 2;
      stage = Stage::Scopes;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    case 9:
      h.a.status = TF_SD_READONLY;
      stage = Stage::Status;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    case 10:
      h.a.disabled = true;
      stage = Stage::ContextDisabled;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    case 11:
      h.a.empty_disabled = true;
      stage = Stage::EmptyContext;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    case 12:
      h.manager.disabled = true;
      stage = Stage::ThreadDisabled;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    case 13:
      h.a.foreign = true;
      stage = Stage::Foreign;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    case 14:
      h.a.selection_length = 1;
      stage = Stage::Selection;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    case 15:
      h.a.view_present = false;
      stage = Stage::Focus;
      cause = Cause::Missing;
      result = S_OK;
      break;
    case 16:
      h.a.text[6] = L'x';
      stage = Stage::Boundary;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    case 17:
      h.a.window = GetDlgItem(h.root, 103);
      ::SetFocus(h.a.window);
      stage = Stage::ProofWindow;
      cause = Cause::Predicate;
      result = S_OK;
      break;
    }
    const auto before = h.a.text;
    const auto caret = h.a.caret;
    Require(!h.Key(L'g', false), "observed refusal preserves key disposition");
    const auto snapshot = DiagnosticSnapshot(*h.service.Get());
    Require(snapshot.first.present && snapshot.first.stage == stage &&
                snapshot.first.cause == cause &&
                snapshot.first.result == static_cast<uint32_t>(result) &&
                snapshot.latest == snapshot.first &&
                snapshot.refusals[static_cast<size_t>(stage)] == 1 &&
                snapshot.counts[static_cast<size_t>(Counter::HandleEntered)] == 1 &&
                snapshot.counts[static_cast<size_t>(Counter::SessionEntered)] == 1 &&
                snapshot.counts[static_cast<size_t>(Counter::SessionCompleted)] == 1 &&
                snapshot.counts[static_cast<size_t>(Counter::Attempted)] == 0,
            "first refusal identifies exact observed native result or predicate");
    Require(h.a.text == before && h.a.caret == caret && h.a.writes == 0 && h.a.starts == 0 &&
                h.b.text == L"prefix suffix",
            "diagnostic refusal preserves whole documents and authority");
  }
  for (int scenario = 0; scenario != 3; ++scenario) {
    Harness h;
    h.a.callback = false;
    if (scenario == 0)
      h.a.request_result = E_ACCESSDENIED;
    if (scenario == 1)
      h.a.session_result = TF_E_SYNCHRONOUS;
    Require(!h.Key(L'g', false), "observed unentered request passes key");
    auto snapshot = DiagnosticSnapshot(*h.service.Get());
    Require(snapshot.request_present && snapshot.session_present &&
                snapshot.request_result == static_cast<uint32_t>(h.a.request_result) &&
                snapshot.session_result == static_cast<uint32_t>(h.a.session_result) &&
                snapshot.first.stage == (scenario == 0 ? Stage::Request : Stage::Session) &&
                snapshot.first.cause == (scenario == 2 ? Cause::Predicate : Cause::Native) &&
                snapshot.counts[static_cast<size_t>(Counter::Requested)] == 1 &&
                snapshot.counts[static_cast<size_t>(Counter::SessionEntered)] == 0 &&
                snapshot.counts[static_cast<size_t>(Counter::SessionCompleted)] == 0 &&
                h.a.writes == 0,
            "request denial differs from denied or unentered session");
  }
  {
    Harness h;
    Require(h.Key(L'g', false), "diagnosed verified write consumes key");
    auto snapshot = DiagnosticSnapshot(*h.service.Get());
    Require(!snapshot.first.present &&
                snapshot.counts[static_cast<size_t>(Counter::Attempted)] == 1 &&
                snapshot.counts[static_cast<size_t>(Counter::Verified)] == 1,
            "attempted write and verified outcome remain distinct");
    h.a.write_result = E_FAIL;
    Require(h.Key(L'h', false), "attempted unknown preserves consumed disposition");
    snapshot = DiagnosticSnapshot(*h.service.Get());
    Require(snapshot.stopped && snapshot.first.stage == Stage::Apply &&
                snapshot.first.cause == Cause::Native &&
                snapshot.counts[static_cast<size_t>(Counter::Unknown)] == 1,
            "unknown write remains observable in stopped active service");
    BOOL interest = TRUE;
    h.service->OnTestKeyDown(&h.a, 0, 0, &interest);
    snapshot = DiagnosticSnapshot(*h.service.Get());
    Require(!interest && snapshot.latest.stage == Stage::Test &&
                snapshot.latest.cause == Cause::Stopped,
            "test refusal distinguishes stopped service");
  }
}
void DiagnosticExceptionSchedules() {
  using namespace diagnostics;
  for (const char *boundary : {"GetAppProperty", "SetText.before", "SetText.after"}) {
    Harness h;
    bool thrown = false;
    h.a.hook = [&](const char *name) {
      if (std::string(name) == boundary) {
        thrown = true;
        throw 1;
      }
    };
    const bool attempted = std::string(boundary) != "GetAppProperty";
    Require(h.Key(L'g', false) == attempted && thrown,
            "diagnostic exception preserves existing key disposition");
    h.a.hook = nullptr;
    const auto snapshot = DiagnosticSnapshot(*h.service.Get());
    Require(snapshot.first.present && snapshot.first == snapshot.latest &&
                snapshot.first.stage == (attempted ? Stage::Apply : Stage::AppProperty) &&
                snapshot.first.cause == Cause::Exception && snapshot.first.result == 0 &&
                snapshot.counts[static_cast<size_t>(Counter::Attempted)] == (attempted ? 1u : 0u) &&
                snapshot.counts[static_cast<size_t>(Counter::Unknown)] == (attempted ? 1u : 0u) &&
                snapshot.counts[static_cast<size_t>(Counter::Verified)] == 0 && snapshot.stopped,
            "throwing native boundary retains active stage and counts unknown once");
    const bool wrote = std::string(boundary) == "SetText.after";
    Require(h.a.text == (wrote ? L"prefix gsuffix" : L"prefix suffix") && h.a.caret == 7 &&
                h.a.writes == (wrote ? 1 : 0) && h.b.text == L"prefix suffix" && h.b.caret == 7,
            "exception diagnostics preserve actual whole documents without repair or replay");
    Require(!h.Key(L'h', false) && h.a.writes == (wrote ? 1 : 0),
            "unknown exception stops further writes");
  }
}
struct DiagnosticProvider final : ITfFunctionProvider,
                                  diagnostics::SnapshotReaderV2,
                                  diagnostics::SnapshotReader {
  ULONG references = 1;
  diagnostics::SnapshotV2 snapshot;
  HRESULT result = S_OK;
  bool throwing = false;
  bool legacy_only = false;
  int legacy_reads = 0;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { return --references; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITfFunctionProvider)
      *out = static_cast<ITfFunctionProvider *>(this);
    else if (id == diagnostics::snapshot_v2_iid && !legacy_only)
      *out = static_cast<diagnostics::SnapshotReaderV2 *>(this);
    else if (id == diagnostics::snapshot_iid)
      *out = static_cast<diagnostics::SnapshotReader *>(this);
    if (!*out)
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFunction(REFGUID function, REFIID id, IUnknown **out) override {
    Require(function == GUID_NULL && id == diagnostics::snapshot_v2_iid,
            "private readonly snapshot discovery");
    return QueryInterface(id, reinterpret_cast<void **>(out));
  }
  HRESULT STDMETHODCALLTYPE Copy(uint32_t size, uint32_t requested_version,
                                 diagnostics::SnapshotV2 *out) override {
    Require(size == sizeof(*out) && requested_version == diagnostics::version_v2,
            "poll bounded versioned copy");
    *out = {};
    if (throwing)
      throw 1;
    if (result == S_OK)
      *out = snapshot;
    return result;
  }
  HRESULT STDMETHODCALLTYPE Copy(uint32_t size, uint32_t requested_version,
                                 diagnostics::Snapshot *out) override {
    ++legacy_reads;
    if (size != sizeof(*out) || requested_version != diagnostics::version)
      return E_INVALIDARG;
    *out = snapshot.base;
    return S_OK;
  }
  STUB(GetType, (GUID *));
  STUB(GetDescription, (BSTR *));
};
void DiagnosticPublicationSchedules() {
  using namespace diagnostics;
  DiagnosticProvider provider;
  Harness h;
  Poller poller;
  poller.Poll(&h.manager);
  h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&provider);
  provider.snapshot.base.active = provider.snapshot.base.activation = 1;
  poller.Poll(&h.manager);
  Require(poller.value.availability == Availability::Available && poller.value.source == 2 &&
              poller.value.continuity == Continuity::Reset,
          "canonical provider change resets interval");
  for (int scenario = 0; scenario != 4; ++scenario) {
    provider.result = scenario == 0 ? E_ACCESSDENIED : scenario == 1 ? S_FALSE : S_OK;
    provider.throwing = scenario == 2;
    provider.snapshot.base.active = scenario == 3 ? 2 : 1;
    poller.Poll(&h.manager);
    Require(poller.value.availability == Availability::Error &&
                poller.value.snapshot == SnapshotV2{} &&
                poller.value.continuity == Continuity::None,
            "failed throwing incomplete or malformed reads replace cached success");
  }
  provider.throwing = false;
  provider.snapshot.base.active = 1;
  provider.legacy_only = true;
  poller.Poll(&h.manager);
  Require(poller.value.availability == Availability::UnsupportedVersion &&
              poller.value.snapshot == SnapshotV2{} && Valid(poller.value) &&
              provider.legacy_reads == 0,
          "old provider never becomes a synthetic empty lifecycle publication");
  provider.legacy_only = false;
  provider.snapshot.base.active = 0;
  poller.Poll(&h.manager);
  Require(poller.value.availability == Availability::Inactive && !poller.value.snapshot.base.active,
          "inactive publication replaces cached active snapshot");
  provider.snapshot.base.active = 1;
  poller.Poll(&h.manager);
  Require(poller.value.continuity == Continuity::Reset,
          "availability recovery begins a new interval");
  poller.Poll(&h.manager);
  Require(poller.value.continuity == Continuity::Same,
          "second available sample can share baseline");
  ++provider.snapshot.base.activation;
  poller.Poll(&h.manager);
  Require(poller.value.continuity == Continuity::Reset,
          "same identity new activation resets interval");
  h.manager.provider_result = E_FAIL;
  poller.Poll(&h.manager);
  Require(poller.value.availability == Availability::Error &&
              poller.value.result == static_cast<uint32_t>(E_FAIL) &&
              poller.value.snapshot == SnapshotV2{},
          "failed provider discovery is an error rather than absence");
  h.manager.provider_result = S_OK;
  h.manager.provider_null = true;
  poller.Poll(&h.manager);
  Require(poller.value.availability == Availability::Error && poller.value.result == 0 &&
              poller.value.snapshot == SnapshotV2{},
          "successful null provider is a predicate error");
  h.manager.provider_null = false;
  poller.value.publication = UINT32_MAX - 1;
  poller.Poll(&h.manager);
  Require(poller.value.publication == UINT32_MAX && poller.value.saturated &&
              poller.value.continuity == Continuity::Indeterminate,
          "host saturation makes continuity indeterminate");
  poller.Reset();
}
void DiagnosticLifetimeSchedules() {
  using diagnostics::Availability;
  using diagnostics::Cause;
  using diagnostics::Continuity;
  using diagnostics::Counter;
  {
    Harness h;
    Require(h.manager.diagnostic_advises == 1 && h.manager.diagnostic_sink,
            "proof process registers actual provider once");
    diagnostics::Poller poller;
    poller.Poll(&h.manager);
    Require(poller.value.availability == Availability::Available &&
                poller.value.continuity == Continuity::Reset && poller.value.source == 1,
            "actual registered provider begins a new interval");
    const auto activation = poller.value.snapshot.base.activation;
    h.service->OnSetFocus(FALSE);
    h.service->OnActivated(0, 0, service_id, GUID_NULL, profiles[0].id, nullptr,
                           TF_IPSINK_FLAG_ACTIVE);
    poller.Poll(&h.manager);
    Require(poller.value.availability == Availability::Available &&
                poller.value.continuity == Continuity::Same && poller.value.source == 1 &&
                poller.value.snapshot.base.activation == activation,
            "editing generations do not break service activation continuity");
    BOOL interest = FALSE;
    h.service->OnTestKeyDown(&h.a, 0, 0, &interest);
    Require(interest, "actual service test interest observed");
    ComPtr<diagnostics::SnapshotReaderV2> retained;
    Require(h.manager.diagnostic_sink->QueryInterface(
                diagnostics::snapshot_v2_iid, reinterpret_cast<void **>(retained.GetAddressOf())) ==
                S_OK,
            "retained diagnostic interface");
    bool busy = false;
    h.a.hook = [&](const char *name) {
      if (!busy && std::string(name) == "GetSelection") {
        busy = true;
        diagnostics::SnapshotV2 rejected;
        rejected.base.active = 1;
        Require(retained->Copy(sizeof(rejected), diagnostics::version_v2, &rejected) == E_PENDING &&
                    rejected == diagnostics::SnapshotV2{},
                "busy snapshot output is cleared");
        poller.Poll(&h.manager);
        Require(poller.value.availability == Availability::Busy &&
                    poller.value.snapshot == diagnostics::SnapshotV2{},
                "busy publication replaces cached success");
      }
    };
    Require(h.Key(L'g', false) && busy, "busy observation does not affect editing");
    h.a.hook = nullptr;
    poller.Poll(&h.manager);
    Require(poller.value.continuity == Continuity::Reset && poller.value.source == 2,
            "available after failed read resets interval");
    h.manager.provider_result = TF_E_NOPROVIDER;
    poller.Poll(&h.manager);
    Require(poller.value.availability == Availability::Missing &&
                poller.value.snapshot == diagnostics::SnapshotV2{},
            "provider absence replaces cached success");
    h.manager.provider_result = S_OK;
    h.service->Deactivate();
    diagnostics::SnapshotV2 inactive;
    Require(retained->Copy(sizeof(inactive), diagnostics::version_v2, &inactive) == S_OK &&
                !inactive.base.active &&
                inactive.base.counts[static_cast<size_t>(Counter::TestEntered)] == 1,
            "retained interface reports inactive with lifetime history");
    Require(SUCCEEDED(h.service->Activate(&h.manager, 42)), "same service reactivation");
    poller.Poll(&h.manager);
    Require(poller.value.availability == Availability::Available &&
                poller.value.snapshot.base.activation == activation + 1 &&
                poller.value.continuity == Continuity::Reset &&
                poller.value.snapshot.base.counts[static_cast<size_t>(Counter::TestEntered)] == 1,
            "new activation resets baseline and preserves lifetime counters");
    poller.Reset();
  }
  {
    Harness h;
    diagnostics::SnapshotV2 output;
    output.base.active = 1;
    Require(h.service->Copy(sizeof(output), diagnostics::version_v2 + 1, &output) == E_INVALIDARG &&
                output == diagnostics::SnapshotV2{},
            "incompatible version clears output");
    output.base.active = 1;
    Require(h.service->Copy(sizeof(output) - 1, diagnostics::version_v2, &output) == E_INVALIDARG &&
                output.base.active == 0,
            "incompatible size clears supplied prefix");
    HRESULT wrong_thread = S_OK;
    output.base.active = 1;
    std::thread thread(
        [&] { wrong_thread = h.service->Copy(sizeof(output), diagnostics::version_v2, &output); });
    thread.join();
    Require(wrong_thread == RPC_E_WRONG_THREAD && output == diagnostics::SnapshotV2{},
            "wrong thread cannot read mutable service state");
    ComPtr<Service> other;
    other.Attach(new Service);
    BOOL interest = TRUE;
    other->OnTestKeyDown(nullptr, 0, 0, &interest);
    Require(!interest && DiagnosticSnapshot(*other.Get()).first.cause == Cause::Inactive &&
                DiagnosticSnapshot(*h.service.Get())
                        .counts[static_cast<size_t>(Counter::TestEntered)] == 0,
            "independent instances own independent counters");
    h.service->OnActivated(0, 0, GUID_NULL, GUID_NULL, GUID_NULL, nullptr, 0);
    h.service->OnTestKeyDown(&h.a, 0, 0, &interest);
    Require(!interest && DiagnosticSnapshot(*h.service.Get()).latest.cause == Cause::ModeUnset,
            "test refusal distinguishes missing mode");
  }
  for (int scenario = 0; scenario != 9; ++scenario) {
    Harness h;
    h.service->Deactivate();
    const int initial_advises = h.manager.diagnostic_advises;
    const int initial_unadvises = h.manager.diagnostic_unadvises;
    if (scenario == 0)
      h.manager.diagnostic_source = false;
    if (scenario == 1)
      h.manager.diagnostic_advise_result = E_FAIL;
    if (scenario == 2)
      h.manager.throw_advise = true;
    if (scenario == 3)
      h.manager.diagnostic_advise_hook = [&] { h.service->Deactivate(); };
    if (scenario == 4)
      h.manager.diagnostic_unadvise_result = E_FAIL;
    if (scenario == 5)
      h.manager.throw_unadvise = true;
    if (scenario == 6 || scenario == 7) {
      h.manager.throw_advise = true;
      h.manager.diagnostic_unadvise_result = E_FAIL;
      h.manager.throw_unadvise = scenario == 7;
    }
    if (scenario == 8)
      h.manager.diagnostic_unadvise_hook = [&] { h.service->Deactivate(); };
    Require(h.service->Activate(&h.manager, 42) == S_OK,
            "best effort diagnostic registration preserves activation result");
    h.service->Deactivate();
    h.service->Deactivate();
    Require(
        !h.manager.diagnostic_sink &&
            h.manager.diagnostic_advises == initial_advises + (scenario == 0 ? 0 : 1) &&
            h.manager.diagnostic_unadvises == initial_unadvises + (scenario == 0 ? 0 : 1),
        "failed throwing or reentrant registration cleans captured ownership once without retry");
    h.manager.diagnostic_advise_hook = nullptr;
    h.manager.diagnostic_unadvise_hook = nullptr;
    h.manager.throw_unadvise = false;
  }
}
void DiagnosticCodecSchedules() {
  using namespace diagnostics;
  Require(SameActivation(true, 1, 1) && !SameActivation(false, 1, 1) &&
              !SameActivation(true, 2, 1) && !SameActivation(true, 0, 0) &&
              !SameActivation(true, UINT32_MAX, UINT32_MAX),
          "saturated serial cannot publish registration ownership after reentry");
  Publication publication;
  wchar_t text[status_capacity]{};
  Publication parsed;
  Require(Format(publication, text) && Parse(text, parsed),
          "absent provider has canonical bounded publication");
  publication.availability = Availability::Available;
  publication.continuity = Continuity::Reset;
  publication.source = publication.snapshot.activation = publication.snapshot.active = 1;
  Refuse(publication.snapshot, Stage::AppProperty, Cause::Native, S_FALSE);
  Require(Format(publication, text) && Parse(text, parsed) &&
              parsed.snapshot == publication.snapshot,
          "typed codec preserves native S_FALSE and exact first refusal");
  const std::wstring good(text);
  for (const auto &bad : {good + L"; unexpected=1", good.substr(1), good + L" ",
                          std::wstring(L"Keyrill service v2") + good.substr(18),
                          good.substr(0, good.size() - 1), std::wstring(status_capacity, L'x')})
    Require(!Parse(bad, parsed), "codec rejects malformed truncated extra or oversized payload");
  for (const auto &replacement :
       {std::pair{L"active=1", L"active=2"}, std::pair{L"source=1", L"source=01"},
        std::pair{L"result=0", L"result=4294967296"}, std::pair{L"/Native/1", L"/Predicate/1"},
        std::pair{L"state=Available", L"state=Fresh"}}) {
    std::wstring bad = good;
    const auto position = bad.find(replacement.first);
    Require(position != std::wstring::npos, "malformed codec target present");
    bad.replace(position, wcslen(replacement.first), replacement.second);
    Require(!Parse(bad, parsed), "codec rejects invalid enums booleans numbers and discriminants");
  }
  publication.snapshot.counts[0] = UINT32_MAX - 1;
  Count(publication.snapshot, Counter::TestEntered);
  Count(publication.snapshot, Counter::TestEntered);
  Require(publication.snapshot.saturated && publication.snapshot.counts[0] == UINT32_MAX,
          "scalar counters saturate without wrapping");
  publication.continuity = Continuity::Indeterminate;
  Require(Format(publication, text) && Parse(text, parsed), "saturation remains explicit in codec");
  publication.snapshot.saturated = 0;
  Require(!Format(publication, text), "codec rejects hidden saturation");
}

diagnostics::SnapshotV2 LifecycleSnapshotOf(Service &service) {
  diagnostics::SnapshotV2 snapshot;
  Require(service.Copy(sizeof(snapshot), diagnostics::version_v2, &snapshot) == S_OK &&
              diagnostics::Valid(snapshot),
          "valid service lifecycle snapshot");
  Require(snapshot.base == DiagnosticSnapshot(service), "v2 projects the frozen v1 payload");
  return snapshot;
}
const diagnostics::LifecycleEvent &Last(const diagnostics::SnapshotV2 &s) {
  Require(s.lifecycle.count != 0, "lifecycle event exists");
  return s.lifecycle.events[s.lifecycle.count - 1];
}
void LifecycleCodecSchedules() {
  using namespace diagnostics;
  LifecycleJournal journal;
  const auto first_serial = journal.Acquire();
  journal.Record(LifecycleKind::EndRequested, first_serial);
  journal.Record(LifecycleKind::FinishVerified, first_serial);
  const auto second_serial = journal.Acquire();
  Require(first_serial == 1 && second_serial == 2, "ownership serials are distinct and nonzero");
  PublicationV2 first;
  first.publication = first.source = first.snapshot.base.active = first.snapshot.base.activation =
      1;
  first.availability = Availability::Available;
  first.continuity = Continuity::Same;
  first.snapshot.lifecycle = journal.Copy(true, second_serial);
  auto second = first;
  ++second.publication;
  Require(Comparable(first, second), "fresh empty lifecycle interval remains comparable");
  auto changed_history = second;
  changed_history.snapshot.lifecycle.events[0].kind = LifecycleKind::EndRequested;
  Require(Valid(changed_history) && !Comparable(first, changed_history),
          "same sequence cannot rewrite retained historical events");
  for (size_t i = 0; i != lifecycle_capacity; ++i)
    journal.Record(LifecycleKind::LifecycleInvalidation, second_serial,
                   LifecycleCause::DocumentFocus);
  second.snapshot.lifecycle = journal.Copy(true, second_serial);
  Require(Comparable(first, second), "exact retained window is complete");
  journal.Record(LifecycleKind::LifecycleInvalidation, second_serial, LifecycleCause::ContextPop);
  second.snapshot.lifecycle = journal.Copy(true, second_serial);
  Require(Valid(second) && !Comparable(first, second),
          "window overrun refuses interval assertions");
  wchar_t output[status_capacity]{};
  PublicationV2 parsed;
  Require(Format(second, output) && Parse(output, parsed) && parsed.snapshot == second.snapshot,
          "v2 ordered window round trips strictly");
  const std::wstring good(output);
  Publication legacy;
  Require(!Parse(good, legacy), "old inspector rejects new wire version");
  wchar_t old_text[status_capacity]{};
  Require(Format(legacy, old_text) && !Parse(old_text, parsed),
          "new inspector rejects old wire version");
  for (int scenario = 0; scenario != 9; ++scenario) {
    auto bad = second;
    auto &l = bad.snapshot.lifecycle;
    if (scenario == 0)
      l.events[1].sequence = l.events[0].sequence;
    if (scenario == 1)
      std::swap(l.events[0], l.events[1]);
    if (scenario == 2)
      l.events[0].kind = LifecycleKind::Count;
    if (scenario == 3)
      l.events[0].cause = LifecycleCause::None;
    if (scenario == 4)
      l.events[0].unresolved = Unresolved::Exception;
    if (scenario == 5)
      l.events[0].subject = l.allocated + 1;
    if (scenario == 6)
      l.owned_serial = 0;
    if (scenario == 7)
      l.count = lifecycle_capacity + 1;
    if (scenario == 8)
      l.owned_present = 0;
    Require(!Valid(bad) && !Format(bad, output), "invalid tagged lifecycle record is rejected");
  }
  for (auto replacement :
       {std::pair{L"event_count=16", L"event_count=17"},
        std::pair{L"owned_serial=2", L"owned_serial=0"}, std::pair{L"v2;", L"v3;"},
        std::pair{L"event=6/8/2/2/0", L"event=6/99/2/2/0"}}) {
    auto bad = good;
    const auto at = bad.find(replacement.first);
    Require(at != std::wstring::npos, "malformed lifecycle field present");
    bad.replace(at, wcslen(replacement.first), replacement.second);
    const auto untouched = parsed;
    Require(!Parse(bad, parsed) && parsed.snapshot == untouched.snapshot,
            "malformed wire cannot publish partial data");
  }
  Require(!Parse(good + L"; event=0/0/0/0/0", parsed) &&
              !Parse(good.substr(0, good.size() - 1), parsed) &&
              !Parse(std::wstring(status_capacity, L'x'), parsed),
          "oversized extra and truncated lifecycle records are rejected");
  auto sparse = first;
  sparse.snapshot.lifecycle.events[4].subject = 1;
  Require(!Valid(sparse), "unused event tail must be exactly zero");
  auto zero_subject = first;
  zero_subject.snapshot.lifecycle.events[0].subject = 0;
  Require(!Valid(zero_subject), "owned transitions require an allocated subject");
  auto changed = first;
  Require(!Comparable(first, changed), "same publication is stale");
  ++changed.publication;
  ++changed.source;
  Require(!Comparable(first, changed), "new provider source needs a baseline");
  changed = first;
  ++changed.publication;
  ++changed.snapshot.base.activation;
  Require(!Comparable(first, changed), "new activation needs a baseline");
  LifecycleJournal exhausted(0, UINT32_MAX - 1);
  Require(exhausted.Acquire() == UINT32_MAX && exhausted.Acquire() == 0,
          "mark allocator saturates without reusing an identity");
  const auto exhausted_state = exhausted.Copy(true, 0);
  Require(Valid(exhausted_state) && exhausted_state.owned_present && exhausted_state.saturated,
          "exhausted identity still reports owned presence");
  LifecycleJournal sequence_exhausted(UINT32_MAX - 1);
  sequence_exhausted.Record(LifecycleKind::TerminationWithoutOwned, 0);
  const auto frozen = sequence_exhausted.Copy(false, 0);
  sequence_exhausted.Record(LifecycleKind::TerminationWithoutOwned, 0);
  Require(sequence_exhausted.Copy(false, 0) == frozen && frozen.saturated,
          "event sequence saturation freezes the history without wrapping");
  auto maximum = second;
  maximum.publication = maximum.source = maximum.snapshot.base.activation = UINT32_MAX;
  maximum.saturated = maximum.snapshot.base.saturated = maximum.snapshot.lifecycle.saturated = 1;
  maximum.continuity = Continuity::Indeterminate;
  maximum.snapshot.base.counts.fill(UINT32_MAX);
  maximum.snapshot.base.refusals.fill(UINT32_MAX);
  maximum.snapshot.base.first =
      maximum.snapshot.base.latest = {1, Stage::Acknowledge, Cause::Native, UINT32_MAX};
  maximum.snapshot.base.request_present = maximum.snapshot.base.session_present = 1;
  maximum.snapshot.base.request_result = maximum.snapshot.base.session_result = UINT32_MAX;
  auto &l = maximum.snapshot.lifecycle;
  l.sequence = l.allocated = l.owned_serial = UINT32_MAX;
  for (size_t i = 0; i != lifecycle_capacity; ++i)
    l.events[i] = {UINT32_MAX - static_cast<uint32_t>(lifecycle_capacity) + 1 +
                       static_cast<uint32_t>(i),
                   LifecycleKind::TerminationUnresolved, UINT32_MAX, LifecycleCause::None,
                   Unresolved::Exception};
  struct {
    uint32_t before = 0xaabbccdd;
    wchar_t text[status_capacity]{};
    uint32_t after = 0xddccbbaa;
  } guarded;
  Require(Format(maximum, guarded.text) && Parse(guarded.text, parsed) &&
              parsed.snapshot == maximum.snapshot && guarded.before == 0xaabbccdd &&
              guarded.after == 0xddccbbaa,
          "maximum scalar publication fits its bounded formatter and preserves canaries");
}
struct TerminationAlias final : ITfComposition {
  ITfComposition &canonical;
  explicit TerminationAlias(ITfComposition &value) : canonical(value) {}
  ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    return canonical.QueryInterface(id, out);
  }
  STUB(GetRange, (ITfRange **));
  STUB(EndComposition, (TfEditCookie));
  STUB(ShiftStart, (TfEditCookie, ITfRange *));
  STUB(ShiftEnd, (TfEditCookie, ITfRange *));
};
void LifecycleServiceSchedules() {
  using namespace diagnostics;
  {
    Harness h;
    h.Type(L"g");
    const auto before = LifecycleSnapshotOf(*h.service.Get());
    const auto calls = h.a.calls;
    HRESULT result = S_OK;
    std::thread foreign(
        [&] { result = h.service->OnCompositionTerminated(123, h.a.composition.Get()); });
    foreign.join();
    Require(result == RPC_E_WRONG_THREAD && LifecycleSnapshotOf(*h.service.Get()) == before &&
                h.a.calls == calls,
            "wrong-thread termination neither samples native identity nor touches STA state");
  }
  for (bool alias : {false, true}) {
    Harness h;
    h.Type(L"g");
    const auto before = LifecycleSnapshotOf(*h.service.Get());
    Require(before.lifecycle.owned_present && Last(before).kind == LifecycleKind::Acquired,
            "actual acquisition publishes current serial");
    TerminationAlias other(*h.a.composition.Get());
    h.a.trace.clear();
    h.a.active_composition = false;
    Require(h.service->OnCompositionTerminated(123, alias ? static_cast<ITfComposition *>(&other)
                                                          : h.a.composition.Get()) == S_OK,
            "canonical termination succeeds through either interface pointer");
    const auto after = LifecycleSnapshotOf(*h.service.Get());
    Require(!after.lifecycle.owned_present && Last(after).kind == LifecycleKind::ExactTermination &&
                Last(after).subject == before.lifecycle.owned_serial &&
                h.a.trace == std::vector<std::string>{"composition.QI"},
            "identity checked termination performs only the existing native query");
    h.Oracle(L"g", 8);
  }
  {
    Harness h;
    h.Type(L"g");
    ComPtr<Composition> old = h.a.composition;
    h.a.active_composition = false;
    h.service->OnCompositionTerminated(123, old.Get());
    h.a.caret = 7;
    h.Type(L"x");
    const auto current = LifecycleSnapshotOf(*h.service.Get()).lifecycle.owned_serial;
    h.a.trace.clear();
    h.service->OnCompositionTerminated(123, old.Get());
    const auto after = LifecycleSnapshotOf(*h.service.Get());
    Require(Last(after).kind == LifecycleKind::DifferentTermination &&
                Last(after).subject == current && after.lifecycle.owned_serial == current &&
                h.a.trace == std::vector<std::string>{"composition.QI"},
            "late callback records protection of current ownership with no extra query");
    Require(h.Key(L'y'), "late callback preserves new mark continuation");
    h.Oracle(L"xyg", 9);
  }
  std::vector<std::string> continuation_trace;
  for (bool reentry : {false, true}) {
    Harness h;
    h.Type(L"ghbdtn ");
    const auto before = LifecycleSnapshotOf(*h.service.Get());
    ComPtr<Composition> old = h.a.composition;
    const auto writes = h.a.writes;
    const auto starts = h.a.starts;
    const auto ends = h.a.ends;
    bool injected = false;
    h.a.hook = [&](const char *name) {
      if (std::string(name) != "composition.QI" || injected)
        return;
      injected = true;
      Require(h.Key(L'x'), "identity query continuation consumes its key");
    };
    h.a.trace.clear();
    if (reentry) {
      Require(h.service->OnCompositionTerminated(123, old.Get()) == S_OK && injected,
              "identity query can finish and replace ownership without lifecycle invalidation");
    } else {
      h.a.hook = nullptr;
      Require(h.Key(L'x'), "ordinary continuation consumes its key");
      continuation_trace = h.a.trace;
      continuation_trace.insert(continuation_trace.begin(), "composition.QI");
    }
    h.a.hook = nullptr;
    const auto after = LifecycleSnapshotOf(*h.service.Get());
    Require(after.lifecycle.owned_present &&
                after.lifecycle.owned_serial == before.lifecycle.owned_serial + 1 &&
                !after.base.stopped && h.a.composition.Get() != old.Get() &&
                h.a.active_composition && h.a.writes == writes + 1 && h.a.starts == starts + 1 &&
                h.a.ends == ends + 1 && h.a.max_request_writes == 1,
            "identity query replacement preserves new ownership and ordinary continuation effects");
    if (reentry) {
      Require(h.a.trace == continuation_trace,
              "replacement termination adds only its original native identity query");
      Require(Last(after).kind == LifecycleKind::TerminationUnresolved &&
                  Last(after).unresolved == Unresolved::Stale &&
                  Last(after).subject == before.lifecycle.owned_serial,
              "replacement termination records TerminationUnresolved/Stale for captured serial");
    }
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 x", 15);
    Require(h.Key(L'y'), "replacement ownership permits the next key");
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 xy", 16);
  }
  for (int scenario = 0; scenario != 5; ++scenario) {
    Harness h;
    h.Type(L"g");
    const auto serial = LifecycleSnapshotOf(*h.service.Get()).lifecycle.owned_serial;
    bool injected = false;
    bool release_seen = false;
    h.a.composition->release_hook = [&] {
      SnapshotV2 busy;
      Require(h.service->Copy(sizeof(busy), version_v2, &busy) == E_PENDING && busy == SnapshotV2{},
              "termination guard covers identity release and exception unwinding");
      release_seen = true;
    };
    h.a.hook = [&](const char *name) {
      if (std::string(name) != "composition.QI" || injected)
        return;
      injected = true;
      SnapshotV2 busy;
      Require(h.service->Copy(sizeof(busy), version_v2, &busy) == E_PENDING,
              "identity query cannot publish partial lifecycle state");
      if (scenario == 0)
        h.service->OnSetFocus(FALSE);
      if (scenario == 1)
        h.service->OnCompositionTerminated(123, h.a.composition.Get());
    };
    if (scenario == 2)
      h.a.composition->identity_result = E_FAIL;
    if (scenario == 3)
      h.a.composition->throw_after_identity = true;
    h.a.trace.clear();
    Require(
        h.service->OnCompositionTerminated(123, scenario == 4 ? nullptr : h.a.composition.Get()) ==
            (scenario == 3 ? E_FAIL : S_OK),
        "termination classification preserves HRESULT");
    h.a.composition->release_hook = nullptr;
    h.a.composition->throw_after_identity = false;
    h.a.hook = nullptr;
    const auto after = LifecycleSnapshotOf(*h.service.Get());
    Require(Last(after).kind == LifecycleKind::TerminationUnresolved &&
                Last(after).subject == serial &&
                Last(after).unresolved == (scenario == 2   ? Unresolved::IdentityFailure
                                           : scenario == 3 ? Unresolved::Exception
                                           : scenario == 4 ? Unresolved::NullArgument
                                                           : Unresolved::Stale),
            "failed stale null and throwing identity queries stay unresolved");
    Require(h.a.trace == (scenario == 4 ? std::vector<std::string>{}
                          : scenario == 1
                              ? std::vector<std::string>{"composition.QI", "composition.QI"}
                              : std::vector<std::string>{"composition.QI"}),
            "unresolved schedules have exactly their preexisting native calls");
    Require((scenario == 0 || scenario == 3) == h.service->stopped(),
            "termination preserves sticky stop decisions");
    Require((scenario == 2 || scenario == 4) || release_seen,
            "identity release boundary exercised");
    h.Oracle(L"g", 8);
  }
  {
    Harness h;
    h.service->OnCompositionTerminated(123, nullptr);
    Require(Last(LifecycleSnapshotOf(*h.service.Get())).unresolved == Unresolved::NullArgument,
            "null callback without ownership is explicit");
    ComPtr<Composition> unowned;
    unowned.Attach(new Composition(h.a, std::make_shared<Span>(Span{7, 0})));
    h.a.trace.clear();
    h.service->OnCompositionTerminated(123, unowned.Get());
    Require(Last(LifecycleSnapshotOf(*h.service.Get())).kind ==
                    LifecycleKind::TerminationWithoutOwned &&
                h.a.trace.empty(),
            "unowned callback performs no identity query");
  }
  for (bool reentry : {false, true}) {
    Harness h;
    h.Type(L"ghbdtn ");
    const auto before = LifecycleSnapshotOf(*h.service.Get());
    h.a.hook = [&](const char *name) {
      if (reentry && std::string(name) == "EndComposition")
        h.service->OnCompositionTerminated(123, h.a.composition.Get());
    };
    const bool eaten = h.Key(L'x', false);
    h.a.hook = nullptr;
    const auto after = LifecycleSnapshotOf(*h.service.Get());
    std::vector<LifecycleKind> kinds;
    for (size_t i = before.lifecycle.count; i < after.lifecycle.count; ++i)
      kinds.push_back(after.lifecycle.events[i].kind);
    Require(kinds == (reentry ? std::vector<LifecycleKind>{LifecycleKind::EndRequested,
                                                           LifecycleKind::ExactTermination}
                              : std::vector<LifecycleKind>{LifecycleKind::EndRequested,
                                                           LifecycleKind::FinishVerified,
                                                           LifecycleKind::Acquired}),
            "finish is verified only after original final Alive and ordinary continuation acquires "
            "a distinct serial");
    Require(eaten == !reentry && h.a.ends == 1 && h.a.max_request_writes == 1,
            "ordinary end reentry preserves key disposition and one write bound");
    h.Oracle(reentry ? L"\x043f\x0440\x0438\x0432\x0435\x0442 "
                     : L"\x043f\x0440\x0438\x0432\x0435\x0442 x",
             reentry ? 14 : 15);
  }
  {
    Harness h;
    h.Type(L"g");
    const auto serial = LifecycleSnapshotOf(*h.service.Get()).lifecycle.owned_serial;
    h.service->OnSetFocus(FALSE);
    h.service->OnPopContext(&h.a);
    const auto invalidated = LifecycleSnapshotOf(*h.service.Get());
    Require(invalidated.lifecycle.owned_serial == serial && invalidated.base.stopped &&
                Last(invalidated).cause == LifecycleCause::ContextPop &&
                invalidated.lifecycle.events[invalidated.lifecycle.count - 2].cause ==
                    LifecycleCause::ForegroundLost,
            "repeated lifecycle invalidation retains ownership and records each cause");
    h.service->OnCompositionTerminated(123, h.a.composition.Get());
    Require(LifecycleSnapshotOf(*h.service.Get()).base.stopped,
            "exact termination never clears sticky stop");
    h.Oracle(L"g", 8);
  }
  {
    Harness h;
    h.Type(L"g");
    bool busy_seen = false;
    h.manager.diagnostic_unadvise_hook = [&] {
      SnapshotV2 busy;
      busy_seen = h.service->Copy(sizeof(busy), version_v2, &busy) == E_PENDING;
    };
    h.service->Deactivate();
    h.manager.diagnostic_unadvise_hook = nullptr;
    const auto detached = LifecycleSnapshotOf(*h.service.Get());
    Require(busy_seen && !detached.lifecycle.owned_present &&
                Last(detached).kind == LifecycleKind::Detached &&
                detached.lifecycle.events[detached.lifecycle.count - 2].cause ==
                    LifecycleCause::Deactivate,
            "local detach is distinct from termination and teardown cannot publish partial state");
    h.Oracle(L"g", 8);
  }
  {
    Harness h;
    h.Type(L"g");
    struct {
      uint32_t before = 17;
      Snapshot value;
      uint32_t after = 23;
    } legacy;
    ComPtr<SnapshotReader> reader;
    Require(h.service->QueryInterface(snapshot_iid,
                                      reinterpret_cast<void **>(reader.GetAddressOf())) == S_OK &&
                reader->Copy(sizeof(legacy.value), version, &legacy.value) == S_OK &&
                legacy.value == LifecycleSnapshotOf(*h.service.Get()).base && legacy.before == 17 &&
                legacy.after == 23,
            "new DLL preserves old reader exact size and surrounding canaries");
    Require(reader->Copy(sizeof(legacy.value) + 4, version, &legacy.value) == E_INVALIDARG &&
                legacy.after == 23,
            "legacy reader never clears beyond its frozen payload");
    struct {
      uint32_t before = 17;
      SnapshotV2 value;
      uint32_t after = 23;
    } current;
    Require(h.service->Copy(sizeof(current.value) + 4, version_v2, &current.value) ==
                    E_INVALIDARG &&
                current.after == 23,
            "v2 rejected oversize never overwrites its payload boundary");
  }
  std::vector<std::string> trace;
  for (bool observe : {false, true}) {
    Harness h;
    h.a.trace.clear();
    h.a.hook = [&](const char *) {
      if (observe) {
        SnapshotV2 busy;
        Require(h.service->Copy(sizeof(busy), version_v2, &busy) == E_PENDING,
                "every actual edit boundary rejects diagnostic reads");
      }
    };
    h.Type(L"ghbdtn x");
    h.a.hook = nullptr;
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 x", 15);
    if (!observe)
      trace = h.a.trace;
    else
      Require(trace == h.a.trace,
              "v2 reads preserve the complete ordered actual service COM trace");
  }
}
void ApplicationInputScope(InputScope scope) {
  Harness h;
  h.a.scope = scope;
  for (const wchar_t *p = L"ghbdtn "; *p; ++p) {
    bool eaten = h.Key(*p, false);
    if (!eaten) {
      fprintf(stderr, "FAIL: application input scope %d expected consumed=true, actual=false\n",
              static_cast<int>(scope));
      std::exit(1);
    }
  }
  h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 ", 14);
  Require(h.a.active_composition && h.a.starts == 1 && h.a.max_request_writes == 1,
          "application input scope supports one owned mark with one write per request");
}
void UncertainApplicationInputScope() {
  for (int scenario = 0; scenario != 22; ++scenario) {
    Harness h;
    auto &property = h.a.application_scope;
    if (scenario == 0) {
      property.result = S_FALSE;
      property.present = false;
    }
    if (scenario == 1)
      property.result = S_FALSE;
    if (scenario == 2)
      property.result = E_FAIL;
    if (scenario == 3)
      property.present = false;
    if (scenario == 4) {
      property.value_result = S_FALSE;
      property.value_type = VT_EMPTY;
    }
    if (scenario == 5)
      property.value_result = E_FAIL;
    if (scenario == 6)
      property.value_type = VT_EMPTY;
    if (scenario == 7)
      property.value_present = false;
    if (scenario == 8)
      property.value_type = VT_I4;
    if (scenario == 9)
      property.scopes_result = E_FAIL;
    if (scenario == 10)
      property.scopes_count = 0;
    if (scenario == 11)
      property.scopes_present = false;
    if (scenario == 12)
      h.a.scope = static_cast<InputScope>(-1);
    if (scenario == 13)
      h.a.scope = IS_EMAIL_SMTPEMAILADDRESS;
    if (scenario == 14)
      property.value_result = S_FALSE;
    if (scenario == 15)
      h.a.scope = IS_PRIVATE;
    if (scenario == 16)
      h.a.scope = IS_PASSWORD;
    if (scenario == 17)
      h.a.scope = static_cast<InputScope>(0x7fffffff);
    if (scenario == 18) {
      h.a.scope = IS_TEXT;
      property.scopes_count = 2;
      property.second_scope = IS_PRIVATE;
    }
    if (scenario == 19)
      property.scopes_count = 2;
    if (scenario == 20) {
      h.a.scope = IS_TEXT;
      property.scopes_count = 2;
    }
    if (scenario == 21)
      property.scopes_result = S_FALSE;
    Require(!h.Key(L'g', false), "uncertain application input scope passes key unchanged");
    Require(h.a.reads == 0 && h.a.starts == 0 && h.a.writes == 0,
            "uncertain application input scope has no text reads, composition starts or writes");
    h.Oracle(L"", 7);
  }
}
void CorrectionAndReversal() {
  Harness h;
  h.Type(L"ghbdtn ");
  h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 ", 14);
  kr_event escape{KR_ESCAPE, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
  Require(h.service->HandleEvent(&h.a, escape), "Escape receipt consumed");
  h.Oracle(L"ghbdtn ", 14);
  Require(h.a.max_request_writes <= 1, "at most one text write per ordinary plan");
  Require(!h.a.active_composition, "Escape ends exact composition");
}
void ChangingFinishCaret() {
  using diagnostics::Counter;
  constexpr auto verified = static_cast<size_t>(Counter::Verified);
  constexpr auto unknown = static_cast<size_t>(Counter::Unknown);
  for (int scenario = 0; scenario != 6; ++scenario) {
    Harness h;
    h.Type(L"ghbdtn ");
    const int writes = h.a.writes;
    const auto before = DiagnosticSnapshot(*h.service.Get());
    h.a.hook = [&](const char *name) {
      const std::string call(name);
      if (call == "SetText.after")
        h.a.caret = h.a.composition->span->start;
      if (scenario == 2 && call == "SetSelection.after")
        h.a.caret = 7;
      if ((scenario == 3 || scenario == 4) && call == "EndComposition.after") {
        h.a.caret = scenario == 4 ? 14 : 7;
        h.a.selection_length = scenario == 4 ? 1 : 0;
      }
      if (scenario == 5 && call == "SetSelection.after")
        Require(!h.Key(L'x', false), "reentrant key passes without replay during reversal");
    };
    if (scenario == 1)
      h.a.selection_result = E_FAIL;
    kr_event escape{KR_ESCAPE, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
    Require(h.service->HandleEvent(&h.a, escape), "changing finish consumes Escape once");
    const auto after = DiagnosticSnapshot(*h.service.Get());
    Require(h.a.text == L"prefix ghbdtn suffix" && h.b.text == L"prefix suffix" && h.b.caret == 7,
            "changing finish preserves whole documents and unrelated caret");
    Require(h.a.writes == writes + 1 && h.a.max_request_writes == 1,
            "changing finish performs exactly one owned text write");
    if (scenario == 0) {
      if (h.a.caret != 14 || h.a.selection_length != 0) {
        fprintf(stderr, "FAIL: changing finish caret expected=14/0 actual=%ld/%ld\n", h.a.caret,
                h.a.selection_length);
        std::exit(1);
      }
      Require(!h.a.active_composition && !h.service->stopped() &&
                  after.counts[verified] == before.counts[verified] + 1 &&
                  after.counts[unknown] == before.counts[unknown],
              "restored text and caret permit verified finish");
    } else {
      Require(h.service->stopped() && after.counts[verified] == before.counts[verified] &&
                  after.counts[unknown] == before.counts[unknown] + 1,
              "unverified reversal caret stops without verified acknowledgement");
      const auto document = h.a.text;
      const LONG caret = h.a.caret, selection = h.a.selection_length;
      Require(!h.Key(L'x', false) && h.a.text == document && h.a.caret == caret &&
                  h.a.selection_length == selection && h.a.writes == writes + 1,
              "unknown changing finish makes no later text or selection repair");
    }
  }
}
void Continuation() {
  Harness h;
  h.Type(L"ghbdtn x");
  h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 x", 15);
  Require(h.a.ends == 1 && h.a.starts == 2, "bounded finish and replan starts next mark");
  Require(h.a.max_request_writes == 1, "unchanged finish performs no duplicate text write");
}
void Denials() {
  for (int scenario = 0; scenario != 4; ++scenario) {
    Harness h;
    h.a.callback = false;
    if (scenario == 0)
      h.a.request_result = E_FAIL;
    if (scenario == 1)
      h.a.session_result = TF_E_SYNCHRONOUS;
    if (scenario == 2)
      h.a.session_result = TF_S_ASYNC;
    Require(!h.Key(L'g'), "uncompleted or denied request passes");
    h.Oracle(L"g", 8);
    Require(h.a.writes == 0 && h.a.reads == 0, "no callback means no text inspection or write");
    Require(h.a.retained_request && FAILED(h.a.retained_request->DoEditSession(123)),
            "late retained request expires even when no mark existed");
    Require(h.a.writes == 0, "expired request cannot write");
  }
  Harness h;
  h.a.reject_composition = true;
  Require(!h.Key(L'g'), "S_OK null composition passes physical key");
  h.Oracle(L"g", 8);
  Require(h.a.writes == 0, "rejected composition writes nothing");
  {
    Harness owned;
    owned.Type(L"gh");
    int writes = owned.a.writes;
    owned.a.callback = false;
    owned.a.session_result = TF_E_SYNCHRONOUS;
    Require(!owned.Key(L'b', false) && owned.a.writes == writes && owned.a.ends == 0 &&
                owned.service->stopped(),
            "denied session with owned mark passes without cleanup");
    owned.a.callback = true;
    Require(!owned.Key(L'x') && owned.a.writes == writes,
            "next ordinary key passes with no stale edit or guessed repair");
    owned.Oracle(L"ghx", 10);
  }
  for (bool continuation : {false, true}) {
    Harness throwing;
    if (continuation)
      throwing.Type(L"ghbdtn ");
    throwing.a.throw_after_callback = true;
    Require(throwing.Key(continuation ? L'x' : L'g', false),
            "outer exception after completed MARK or replan preserves key consumption");
    Require(throwing.service->stopped() && throwing.a.retained_request &&
                FAILED(throwing.a.retained_request->DoEditSession(123)),
            "outer exception expires retained request before returning");
  }
}
void Exclusions() {
  for (int scenario = 0; scenario != 8; ++scenario) {
    Harness h;
    if (scenario == 0)
      h.a.window = GetDlgItem(h.root, 103);
    if (scenario == 1)
      h.a.window = GetDlgItem(h.root, 104);
    if (scenario == 2)
      h.a.scope = IS_PASSWORD;
    if (scenario == 3)
      h.a.disabled = true;
    if (scenario == 4)
      h.a.status = TF_SD_READONLY;
    if (scenario == 5)
      h.a.window = h.root;
    if (scenario == 6)
      h.a.scope = IS_EMAIL_SMTPEMAILADDRESS;
    if (scenario == 7)
      h.manager.disabled = true;
    ::SetFocus(h.a.window);
    Require(!h.Key(L'g', false), "excluded control passes");
    Require(h.a.reads == 0 && h.a.starts == 0 && h.a.writes == 0,
            "excluded control has no text reads or mark");
    h.Oracle(L"", 7);
  }
  Harness h;
  h.a.foreign = true;
  Require(!h.Key(L'g', false) && h.a.writes == 0 && h.a.reads == 0,
          "foreign composition excluded before text read");
  ComPtr<Service> secure;
  secure.Attach(new Service);
  Require(secure->ActivateEx(&h.manager, 42, TF_TMAE_SECUREMODE) == E_ACCESSDENIED &&
              !secure->HandleEvent(&h.a, Text(L'g')) && h.a.reads == 0 && h.a.writes == 0,
          "secure activation cannot inspect or compose");
}
void DriftAndUnknown() {
  for (int scenario = 0; scenario != 5; ++scenario) {
    Harness h;
    h.Type(L"ghbdtn");
    int before = h.a.writes;
    if (scenario == 0)
      --h.a.caret;
    if (scenario == 1)
      ++h.a.composition->span->start;
    if (scenario == 2)
      h.a.text[7] = L'!';
    if (scenario == 3)
      h.a.active_composition = false;
    if (scenario == 4)
      h.a.foreign = true;
    std::wstring snapshot = h.a.text;
    Require(!h.Key(L' ', false), "drift refuses before write");
    Require(h.a.text == snapshot && h.a.writes == before, "drift preserves whole document");
    Require(h.service->stopped(), "unfinishable mark blocks activation");
  }
  for (int scenario = 0; scenario != 3; ++scenario) {
    Harness h;
    h.Type(L"ghbdtn");
    int before = h.a.writes;
    if (scenario == 0)
      h.a.write_result = E_FAIL;
    if (scenario == 1)
      h.a.hook = [&](const char *name) {
        if (std::string(name) == "SetText.after")
          h.a.text[7] = L'!';
      };
    if (scenario == 2)
      h.a.hook = [&](const char *name) {
        if (std::string(name) == "SetText.after")
          ++h.a.composition->span->length;
      };
    Require(h.Key(L' ', false), "unknown attempted MARK consumes without replay");
    Require(h.a.writes == before + 1 && h.service->stopped(), "unknown stops after one write");
    std::wstring snapshot = h.a.text;
    Require(!h.Key(L'x', false) && h.a.text == snapshot && h.a.writes == before + 1,
            "stopped service never repairs or repeats unknown write");
  }
}
void FinishOutcomes() {
  for (int scenario = 0; scenario != 5; ++scenario) {
    Harness h;
    h.Type(L"ghbdtn ");
    int before = h.a.writes;
    if (scenario == 0)
      h.a.end_result = E_FAIL;
    if (scenario == 1)
      h.a.keep_composition = true;
    if (scenario == 2)
      h.a.request_result = E_FAIL;
    if (scenario == 3)
      h.a.session_result = E_FAIL;
    if (scenario == 4)
      h.a.hook = [&](const char *name) {
        if (std::string(name) == "EndComposition.after")
          h.a.text[7] = L'!';
      };
    kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
    bool eaten = (scenario == 2 || scenario == 3) ? h.service->HandleEvent(&h.a, finish)
                                                  : h.Key(L'x', false);
    Require(!eaten, "unknown FINISH passes ordinary key without replan");
    Require(h.a.writes == before && h.service->stopped(), "finish unknown makes no text repair");
  }
  Harness h;
  h.Type(L"hello");
  --h.a.caret;
  kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
  Require(!h.service->HandleEvent(&h.a, finish) && !h.a.active_composition,
          "unchanged finish ignores caret drift and ends exact owned range");
  h.Oracle(L"hello", 11);
  Harness selected;
  selected.Type(L"hello");
  selected.a.caret = 0;
  selected.a.selection_length = static_cast<LONG>(selected.a.text.size());
  Require(!selected.service->HandleEvent(&selected.a, finish) && !selected.a.active_composition &&
              selected.a.selection_length == static_cast<LONG>(selected.a.text.size()),
          "unchanged finish ignores a noncollapsed whole-document selection");
  selected.Oracle(L"hello", 0);
  Harness moved;
  moved.Type(L"ghbdtn ");
  moved.a.caret = 3;
  int before = moved.a.writes;
  Require(!moved.Key(L'x', false) && moved.a.writes == before && !moved.a.active_composition,
          "finish with caret drift cannot replan inside unrelated prefix token");
  for (int scenario = 0; scenario != 3; ++scenario) {
    Harness reserved;
    reserved.Type(L"ghbdtn ");
    if (scenario == 0)
      reserved.a.write_result = E_FAIL;
    if (scenario == 1)
      reserved.a.end_result = E_FAIL;
    if (scenario == 2)
      reserved.a.request_result = E_FAIL;
    kr_event escape{KR_ESCAPE, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
    Require(reserved.service->HandleEvent(&reserved.a, escape),
            "unknown reserved Escape remains consumed");
    Require(reserved.service->stopped(), "unknown reserved command stops activation");
  }
  for (const char *boundary : {"GetRange", "SetText.before", "SetText.after"}) {
    Harness throwing;
    throwing.Type(L"gh");
    int before_writes = throwing.a.writes;
    throwing.a.hook = [&](const char *name) {
      if (std::string(name) == boundary)
        throw 1;
    };
    bool eaten = throwing.Key(L'b', false);
    Require(eaten == (std::string(boundary) != "GetRange") && throwing.service->stopped(),
            "COM exception is contained and disposition distinguishes attempted effect");
    Require(throwing.a.writes <= before_writes + 1, "exception path never retries");
  }
}
void FocusAndTermination() {
  Harness h;
  h.Type(L"gh");
  ComPtr<Composition> old = h.a.composition;
  h.a.active_composition = false;
  h.service->OnCompositionTerminated(123, old.Get());
  h.a.caret = 7;
  h.Type(L"x");
  auto *newer = h.a.composition.Get();
  int before = h.a.writes;
  h.service->OnCompositionTerminated(123, old.Get());
  Require(h.a.composition.Get() == newer && h.Key(L'y'),
          "late old termination cannot end newer mark");
  Require(h.a.writes == before + 1, "newer mark still owns continuation");
  h.manager.top = &h.b;
  ::SetFocus(h.b.window);
  before = h.a.writes;
  Require(!h.Key(L'z', false) && h.a.writes == before,
          "native and TSF focus mismatch denies writes");
  h.service->OnSetFocus(static_cast<ITfDocumentMgr *>(&h.manager), nullptr);
  Require(h.service->stopped(), "focus loss with live mark blocks unproven cleanup");
  h.manager.top = &h.a;
  ::SetFocus(h.a.window);
  Require(!h.Key(L'z', false) && h.a.writes == before, "A B A never revives old authority");
}
void ReentrantBoundaries() {
  std::vector<std::string> boundaries;
  {
    Harness h;
    h.Type(L"ghbdtn");
    h.a.calls.clear();
    h.Key(L' ');
    for (const auto &[name, count] : h.a.calls)
      if (count && name != "SetText.after")
        boundaries.push_back(name);
  }
  for (const auto &boundary : boundaries) {
    Harness h;
    h.Type(L"ghbdtn");
    int before = h.a.writes;
    bool injected = false;
    int writes_at_invalidation = 0;
    h.a.hook = [&](const char *name) {
      if (!injected && boundary == name) {
        injected = true;
        writes_at_invalidation = h.a.writes + (boundary == "SetText.before" ? 1 : 0);
        bool nested = h.service->HandleEvent(&h.a, Text(L'x'));
        Require(!nested, "nested physical key passes unchanged");
      }
    };
    bool eaten = h.Key(L' ', false);
    Require(injected && h.a.writes == writes_at_invalidation,
            "zero writes after invalidation at every exercised COM boundary");
    Require(eaten == (h.a.writes > before), "reentry disposition matches attempted text write");
    Require(h.b.text == L"prefix suffix", "reentry preserves other document");
  }
}
void Keyboard() {
  BYTE state[256]{};
  kr_event event{};
  HKL en = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04090409));
  HKL ru = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04190419));
  Require(TranslateKey('G', 0x220001, en, state, event) && event.units[0] == L'g',
          "EN actual layout translation");
  state[VK_SHIFT] = 0x80;
  Require(TranslateKey('G', 0x220001, en, state, event) && event.units[0] == L'G',
          "Shift preserved");
  state[VK_SHIFT] = 0;
  state[VK_CAPITAL] = 1;
  Require(TranslateKey('G', 0x220001, en, state, event) && event.units[0] == L'G',
          "Caps Lock preserved");
  state[VK_CAPITAL] = 0;
  // RU may not be installed. No utility loads or selects it during tests.
  HKL layouts[64]{};
  int count = GetKeyboardLayoutList(64, layouts);
  if (std::find(layouts, layouts + count, ru) != layouts + count)
    Require(TranslateKey('H', 0x230001, ru, state, event) && event.units[0] == L'\x0440',
            "RU actual layout translation");
  else
    puts("SKIP: RU layout translation runtime check. Layout is not already loaded.");
  state[VK_CONTROL] = 0x80;
  Require(!TranslateKey('G', 0x220001, en, state, event), "shortcut not interpreted as text");
  state[VK_CONTROL] = 0;
  Harness repeat;
  repeat.Type(L"gh");
  Require(!TranslateKey('G', 0x220003, en, state, event) && event.role == KR_FINISH_REQUEST &&
              !repeat.service->HandleEvent(&repeat.a, event) && !repeat.a.active_composition,
          "coalesced repeat finishes owned mark and passes original message intact");
  Require(!TranslateKey('G', 0x220000, en, state, event), "malformed zero repeat count passes");
  TF_INPUTPROCESSORPROFILE profile{};
  profile.dwProfileType = TF_PROFILETYPE_INPUTPROCESSOR;
  profile.clsid = service_id;
  profile.guidProfile = profiles[0].id;
  profile.langid = profiles[0].language;
  profile.dwFlags = TF_IPP_FLAG_ACTIVE;
  profile.hkl = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0xe0200409));
  profile.hklSubstitute = en;
  Require(ProfileLayout(profile, KR_EN, en) == en,
          "synthetic TIP handle uses validated actual substitute");
  Require(!ProfileLayout(profile, KR_RU, en), "mismatched selected profile mode passes");
  profile.hklSubstitute = ru;
  Require(!ProfileLayout(profile, KR_EN, ru), "mismatched substitute cannot translate keys");
}
void TeardownAndConstructorExceptions() {
  long before_objects = object_count;
  {
    Harness h;
    int references = h.a.references;
    h.a.hook = [&](const char *name) {
      if (std::string(name) == "context.QI")
        throw 1;
    };
    Require(!h.Key(L'g', false) && h.a.references == references,
            "constructor QI exception releases context capture");
  }
  Require(object_count == before_objects,
          "constructor exception cannot leak owner or module reference");
  for (const char *boundary : {"UnadviseKeySink", "UnadviseThreadSink", "UnadviseProfileSink"}) {
    for (bool throwing : {false, true}) {
      Harness h;
      bool injected = false;
      h.a.hook = [&](const char *name) {
        if (!injected && std::string(name) == boundary) {
          injected = true;
          Require(SUCCEEDED(h.service->Deactivate()),
                  "nested teardown sees no published resources");
          if (throwing)
            throw 1;
        }
      };
      HRESULT result = h.service->Deactivate();
      Require(injected && (throwing ? FAILED(result) : SUCCEEDED(result)) &&
                  h.manager.key_unadvises == 1 && h.manager.thread_unadvises == 1 &&
                  h.manager.profile_unadvises == 1,
              "teardown detaches each captured resource once despite reentry or exception");
      Require(!h.Key(L'g', false), "torn down service has no editing authority");
    }
  }
}
} // namespace
int wmain() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  puts("RUN native denial fixture gates");
  NativeDenialGateSchedules();
  Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "STA initialized");
  puts("RUN fixture input scope lifecycle");
  FixtureLifecycle();
  controlled_root = CreateFixture(GetModuleHandleW(nullptr), FixtureProc, false);
  Require(controlled_root != nullptr, "controlled native metadata fixture");
  puts("RUN dedicated substitute layout regression");
  DedicatedLayoutRegression();
  puts("RUN dedicated substitute layout negatives");
  DedicatedLayoutNegatives();
  puts("RUN dedicated substitute layout reentry");
  DedicatedLayoutReentry();
  puts("RUN actual service diagnostic codec");
  DiagnosticCodecSchedules();
  LifecycleCodecSchedules();
  LifecycleServiceSchedules();
  puts("RUN actual service diagnostic refusal schedules");
  DiagnosticRefusalSchedules();
  puts("RUN actual service diagnostic exception schedules");
  DiagnosticExceptionSchedules();
  puts("RUN actual service diagnostic publication schedules");
  DiagnosticPublicationSchedules();
  puts("RUN actual service diagnostic lifetime schedules");
  DiagnosticLifetimeSchedules();
  puts("RUN default application input scope correction");
  ApplicationInputScope(IS_DEFAULT);
  puts("RUN text application input scope correction");
  ApplicationInputScope(IS_TEXT);
  puts("RUN uncertain application input scope");
  UncertainApplicationInputScope();
  puts("RUN correction and reversal");
  CorrectionAndReversal();
  puts("RUN changing finish caret");
  ChangingFinishCaret();
  puts("RUN rapid continuation");
  Continuation();
  puts("RUN denial and expired requests");
  Denials();
  puts("RUN exclusions");
  Exclusions();
  puts("RUN drift and unknown");
  DriftAndUnknown();
  puts("RUN finish outcomes");
  FinishOutcomes();
  puts("RUN focus and termination");
  FocusAndTermination();
  puts("RUN reentry boundaries");
  ReentrantBoundaries();
  puts("RUN keyboard translation");
  Keyboard();
  puts("RUN teardown and constructor exceptions");
  TeardownAndConstructorExceptions();
  DestroyWindow(controlled_root);
  CoUninitialize();
  puts("Controlled same-service COM schedules passed. No registration or physical TSF routing "
       "tested.");
  return 0;
}
