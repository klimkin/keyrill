#include "denial_fixture.h"
#include "editor_window.h"
#include "fixture.h"
#include "fixture_mode.h"
#include "keys.h"
#include "service.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <olectl.h>
#include <thread>
#include <vector>
#include <winioctl.h>

using namespace keyrill;
namespace keyrill::source_proof {
struct ProbeTestAccess {
  struct Dependencies {
    ITfInputProcessorProfiles *languages = nullptr;
    ITfInputProcessorProfileMgr *profiles = nullptr;
    detail::PublicWord word;
    HRESULT acquired = S_OK;
    std::function<void()> acquire, read;
    std::function<uint64_t(uint64_t)> clock;
  };
  static void Configure(Probe &probe, Dependencies &dependencies) {
    probe.test_context_ = &dependencies;
    probe.acquire_ = [](void *context, Probe::Native &native) {
      auto &d = *static_cast<Dependencies *>(context);
      if (d.acquire)
        d.acquire();
      native.languages = d.languages;
      native.profiles = d.profiles;
      return d.acquired;
    };
    probe.clock_ = [](void *context, uint64_t entry) {
      auto &d = *static_cast<Dependencies *>(context);
      return d.clock ? d.clock(entry) : entry;
    };
    probe.read_ = [](void *context, const Probe &) {
      auto &d = *static_cast<Dependencies *>(context);
      if (d.read)
        d.read();
      return d.word;
    };
  }
};
} // namespace keyrill::source_proof
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
  const wchar_t *shutdown[] = {normal[0], L"--prove-owned-shutdown"};
  const wchar_t *combined[] = {normal[0], denied[1], shutdown[1]};
  const wchar_t *repeated_shutdown[] = {normal[0], shutdown[1], shutdown[1]};
  const wchar_t *source[] = {normal[0], L"--prove-owned-en-to-ru"};
  const wchar_t *source_extra[] = {normal[0], source[1], L"ru"};
  const wchar_t *source_repeat[] = {normal[0], source[1], source[1]};
  const wchar_t *source_shutdown[] = {normal[0], source[1], shutdown[1]};
  const wchar_t *source_denial[] = {normal[0], source[1], denied[1]};
  Require(fixture::ParseMode(1, normal) == fixture::Mode::Normal &&
              fixture::ParseMode(2, denied) == fixture::Mode::DenyTsfLocks &&
              fixture::ParseMode(2, shutdown) == fixture::Mode::ProveOwnedShutdown &&
              fixture::ParseMode(2, source) == fixture::Mode::ProveOwnedEnToRu &&
              fixture::ParseMode(3, source_extra) == fixture::Mode::Invalid &&
              fixture::ParseMode(3, source_repeat) == fixture::Mode::Invalid &&
              fixture::ParseMode(3, source_shutdown) == fixture::Mode::Invalid &&
              fixture::ParseMode(3, source_denial) == fixture::Mode::Invalid &&
              fixture::ParseMode(2, unknown) == fixture::Mode::Invalid &&
              fixture::ParseMode(3, duplicate) == fixture::Mode::Invalid &&
              fixture::ParseMode(3, extra) == fixture::Mode::Invalid &&
              fixture::ParseMode(3, combined) == fixture::Mode::Invalid &&
              fixture::ParseMode(3, repeated_shutdown) == fixture::Mode::Invalid &&
              fixture::ParseMode(0, nullptr) == fixture::Mode::Invalid,
          "startup accepts only no arguments or exactly one known fixture mode");
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
enum class NativeOperation { Start, SetText, End };
struct OperationTrace {
  int request, composition;
  NativeOperation operation;
  LONG start, length, replacement_length;
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
  HRESULT STDMETHODCALLTYPE ShiftEnd(TfEditCookie, LONG, LONG *, const TF_HALTCOND *) override;
  HRESULT STDMETHODCALLTYPE Collapse(TfEditCookie, TfAnchor) override;
  HRESULT STDMETHODCALLTYPE Clone(ITfRange **) override;
  HRESULT STDMETHODCALLTYPE GetContext(ITfContext **) override;
  HRESULT STDMETHODCALLTYPE GetExtent(LONG *, LONG *) override;
  STUB(SetExtent, (LONG, LONG));
  STUB(GetFormattedText, (TfEditCookie, IDataObject **));
  STUB(GetEmbedded, (TfEditCookie, REFGUID, REFIID, IUnknown **));
  STUB(InsertEmbedded, (TfEditCookie, DWORD, IDataObject *));
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
  HRESULT status_result = S_OK;
  HRESULT selection_query_result = S_OK;
  ULONG selection_fetched = 1;
  bool selection_present = true;
  TF_SELECTIONSTYLE selection_style{TF_AE_NONE, FALSE};
  Document *range_context = nullptr;
  InputScope scope = IS_DEFAULT;
  ApplicationScopeProperty application_scope;
  std::optional<Span> excluded_scope;
  bool disabled = false;
  bool empty_disabled = false;
  bool queried_empty = false;
  HRESULT compartment_result[2] = {S_OK, S_OK};
  VARTYPE compartment_type[2] = {VT_I4, VT_I4};
  HRESULT compartment_manager_result = S_OK, compartment_query_result = S_OK;
  bool compartment_manager_present = true, compartment_present = true;
  bool view_present = true;
  HRESULT scope_interface_result = S_OK;
  bool foreign = false;
  bool reject_composition = false;
  bool callback = true;
  bool retain_callback = false;
  DWORD expected_request_flags = TF_ES_SYNC | TF_ES_READWRITE;
  bool acp_available = true;
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
  int request_ordinal = 0;
  struct FinishSelection {
    int request;
    Span span;
    TF_SELECTIONSTYLE style;
  };
  std::optional<FinishSelection> finish_selection;
  std::vector<std::weak_ptr<Span>> clones;
  std::vector<Span> text_reads;
  bool move_clones = false;
  LONG shift_start_limit = std::numeric_limits<LONG>::max();
  LONG shift_end_limit = std::numeric_limits<LONG>::max();
  HRESULT shift_start_result = S_OK, shift_end_result = S_OK;
  std::vector<OperationTrace> operations;
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
    } else if (id == IID_ITfCompartmentMgr) {
      if (compartment_manager_present) {
        *out = static_cast<ITfCompartmentMgr *>(this);
        AddRef();
      }
      return compartment_manager_result;
    } else if (id == IID_ITfCompartment)
      *out = static_cast<ITfCompartment *>(this);
    if (!*out)
      return E_NOINTERFACE;
    AddRef();
    return id == IID_ITfInputScope ? scope_interface_result : S_OK;
  }
  HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId client, ITfEditSession *request,
                                               DWORD flags, HRESULT *result) override {
    Require(client == 42 && flags == expected_request_flags, "sync granted-cookie protocol");
    if (retain_callback)
      retained_request = request;
    int before = writes;
    ++request_ordinal;
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
    out->range = selection_present ? new Range(*this, caret, selection_length) : nullptr;
    out->style = selection_style;
    *fetched = selection_fetched;
    return selection_query_result;
  }
  HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie, ULONG, const TF_SELECTION *value) override {
    Call("SetSelection");
    auto *range = static_cast<Range *>(value->range);
    if (!active_composition) {
      Require(finish_selection && finish_selection->request == request_ordinal &&
                  !operations.empty() && operations.back().operation == NativeOperation::End &&
                  operations.back().request == request_ordinal &&
                  range->span->start == finish_selection->span.start &&
                  range->span->length == finish_selection->span.length &&
                  value->style.ase == finish_selection->style.ase &&
                  value->style.fInterimChar == finish_selection->style.fInterimChar,
              "only exact unchanged FINISH selection restoration after EndComposition");
    } else {
      Require(range->span->length == 0, "no temporary selection of document text");
    }
    caret = range->span->start;
    selection_length = range->span->length;
    selection_style = value->style;
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
    return status_result;
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
  HRESULT STDMETHODCALLTYPE GetValue(TfEditCookie, ITfRange *range, VARIANT *out) override {
    Call("scope.GetValue");
    VariantInit(out);
    const auto span = static_cast<Range *>(range)->span;
    if (excluded_scope && span->start == excluded_scope->start &&
        span->length == excluded_scope->length)
      return E_ACCESSDENIED;
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
    *out = compartment_present ? static_cast<ITfCompartment *>(this) : nullptr;
    if (*out)
      AddRef();
    return compartment_query_result;
  }
  HRESULT STDMETHODCALLTYPE GetValue(VARIANT *out) override {
    Call("compartment.GetValue");
    VariantInit(out);
    out->vt = compartment_type[queried_empty];
    if (out->vt == VT_I4)
      out->lVal = queried_empty ? empty_disabled : disabled;
    return compartment_result[queried_empty];
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
    const auto &span = static_cast<Range *>(range)->span;
    operations.push_back(
        {request_ordinal, starts, NativeOperation::Start, span->start, span->length, 0});
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
  if (id == IID_ITfRangeACP) {
    document.Call("range.ACP");
    if (!document.acp_available)
      return E_NOINTERFACE;
  }
  if (id != IID_IUnknown && id != IID_ITfRange && id != IID_ITfRangeACP)
    return E_NOINTERFACE;
  *out = static_cast<ITfRangeACP *>(this);
  AddRef();
  return S_OK;
}
HRESULT Range::GetText(TfEditCookie, DWORD, WCHAR *text, ULONG capacity, ULONG *count) {
  document.Call("GetText");
  ++document.reads;
  document.text_reads.push_back(*span);
  if (span->start < 0 || span->length < 0 ||
      span->start + span->length > static_cast<LONG>(document.text.size()))
    return E_FAIL;
  *count = std::min(capacity, static_cast<ULONG>(span->length));
  std::copy_n(document.text.data() + span->start, *count, text);
  document.Call("GetText.after");
  return S_OK;
}
HRESULT Range::SetText(TfEditCookie, DWORD, const WCHAR *text, LONG length) {
  document.Call("SetText.before");
  ++document.writes;
  Require(document.active_composition && document.composition->span == span,
          "text writes address only the owned composition");
  Require(std::none_of(document.operations.begin(), document.operations.end(),
                       [&](const OperationTrace &operation) {
                         return operation.operation == NativeOperation::SetText &&
                                operation.request == document.request_ordinal &&
                                operation.composition == document.starts;
                       }),
          "each request writes a composition at most once per plan");
  document.operations.push_back({document.request_ordinal, document.starts,
                                 NativeOperation::SetText, span->start, span->length, length});
  document.finish_selection.reset();
  if (length == span->length && document.text.compare(span->start, span->length, text, length) == 0)
    document.finish_selection =
        Document::FinishSelection{document.request_ordinal,
                                  {document.caret, document.selection_length},
                                  document.selection_style};
  document.text.replace(span->start, span->length, text, length);
  span->length = length;
  if (document.move_clones)
    for (auto &clone : document.clones)
      if (auto moved = clone.lock())
        *moved = {static_cast<LONG>(document.text.size()), 0};
  document.Call("SetText.after");
  return document.write_result;
}
HRESULT Range::GetContext(ITfContext **out) {
  document.Call("range.GetContext");
  *out = document.range_context ? document.range_context : &document;
  (*out)->AddRef();
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
  const LONG end = span->start + span->length;
  *shift = std::clamp(
      amount, -std::min(span->start, document.shift_start_limit),
      std::min(static_cast<LONG>(document.text.size()) - span->start, document.shift_start_limit));
  span->start += *shift;
  span->length = std::max(end - span->start, 0L);
  document.Call("ShiftStart.after");
  return document.shift_start_result;
}
HRESULT Range::ShiftEnd(TfEditCookie, LONG amount, LONG *shift, const TF_HALTCOND *) {
  document.Call("ShiftEnd");
  const LONG end = span->start + span->length;
  *shift =
      std::clamp(amount, -std::min(end, document.shift_end_limit),
                 std::min(static_cast<LONG>(document.text.size()) - end, document.shift_end_limit));
  const LONG next_end = end + *shift;
  span->start = std::min(span->start, next_end);
  span->length = next_end - span->start;
  document.Call("ShiftEnd.after");
  return document.shift_end_result;
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
  auto *clone = new Range(document, span->start, span->length);
  document.clones.emplace_back(clone->span);
  *out = clone;
  document.Call("Clone.after");
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
  document.operations.push_back({document.request_ordinal, document.starts, NativeOperation::End,
                                 span->start, span->length, 0});
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
  HRESULT compartment_result = S_OK;
  VARTYPE compartment_type = VT_I4;
  HRESULT compartment_manager_result = S_OK, compartment_query_result = S_OK;
  bool compartment_manager_present = true, compartment_present = true;
  bool diagnostic_source = true;
  bool throw_advise = false;
  bool throw_unadvise = false;
  HRESULT diagnostic_advise_result = S_OK;
  HRESULT diagnostic_unadvise_result = S_OK;
  HRESULT provider_result = S_OK;
  bool provider_null = false;
  std::function<void()> diagnostic_advise_hook;
  std::function<void()> diagnostic_unadvise_hook;
  std::function<void()> provider_hook;
  ComPtr<IUnknown> diagnostic_sink;
  int diagnostic_advises = 0, diagnostic_unadvises = 0, provider_queries = 0;
  int references = 1;
  int key_unadvises = 0, thread_unadvises = 0, profile_unadvises = 0;
  struct CommandKey {
    GUID command;
    TF_PRESERVEDKEY key;
    bool operator==(const CommandKey &other) const {
      return command == other.command && key.uVKey == other.key.uVKey &&
             key.uModifiers == other.key.uModifiers;
    }
  };
  struct Registration {
    TfClientId client;
    CommandKey binding;
  };
  std::vector<Registration> preserved_keys;
  std::vector<Registration> preserve_calls;
  std::vector<CommandKey> unpreserved_keys;
  HRESULT preserve_result[2]{S_OK, S_OK};
  HRESULT unpreserve_result[2]{S_OK, S_OK};
  int throw_preserve = -1, throw_unpreserve = -1;
  std::function<void(size_t)> preserve_hook, unpreserve_hook;
  std::function<void()> release_hook;
  std::function<void(int)> advise_hook;
  HRESULT sink_result[3]{S_OK, S_OK, S_OK};
  HRESULT unsink_result[3]{S_OK, S_OK, S_OK};
  int throw_sink = -1;
  int thread_advises = 0, profile_advises = 0;
  // Harness retains the service until Deactivate clears this borrowed sink.
  ITfKeyEventSink *key_sink = nullptr;
  TfClientId key_client = TF_CLIENTID_NULL;
  BOOL key_foreground = FALSE;
  int key_advises = 0;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (release_hook)
      release_hook();
    return --references;
  }
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
    else if (id == IID_ITfCompartmentMgr) {
      if (compartment_manager_present) {
        *out = static_cast<ITfCompartmentMgr *>(this);
        AddRef();
      }
      return compartment_manager_result;
    }
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
    const int index = id == IID_ITfThreadMgrEventSink ? 0 : 1;
    ++(index == 0 ? thread_advises : profile_advises);
    *cookie = sink_result[index] == S_OK ? static_cast<DWORD>(index + 1) : TF_INVALID_COOKIE;
    if (advise_hook)
      advise_hook(index);
    if (throw_sink == index)
      throw 1;
    return sink_result[index];
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
    return unsink_result[cookie - 1];
  }
  HRESULT STDMETHODCALLTYPE AdviseKeyEventSink(TfClientId client, ITfKeyEventSink *sink,
                                               BOOL foreground) override {
    ++key_advises;
    key_client = client;
    key_sink = sink_result[2] == S_OK ? sink : nullptr;
    key_foreground = foreground;
    if (advise_hook)
      advise_hook(2);
    if (throw_sink == 2)
      throw 1;
    return sink_result[2];
  }
  HRESULT STDMETHODCALLTYPE UnadviseKeyEventSink(TfClientId client) override {
    Require(client == 42, "captured teardown client");
    ++key_unadvises;
    key_sink = nullptr;
    top->Call("UnadviseKeySink");
    return unsink_result[2];
  }
  HRESULT STDMETHODCALLTYPE GetCompartment(REFGUID, ITfCompartment **out) override {
    top->Call("thread.GetCompartment");
    *out = compartment_present ? static_cast<ITfCompartment *>(this) : nullptr;
    if (*out)
      AddRef();
    return compartment_query_result;
  }
  HRESULT STDMETHODCALLTYPE GetValue(VARIANT *out) override {
    top->Call("thread.GetValue");
    VariantInit(out);
    out->vt = compartment_type;
    if (out->vt == VT_I4)
      out->lVal = disabled;
    return compartment_result;
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
    if (provider_hook)
      provider_hook();
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
  HRESULT STDMETHODCALLTYPE PreserveKey(TfClientId client, REFGUID command,
                                        const TF_PRESERVEDKEY *key, const WCHAR *, ULONG) override {
    if (!key)
      return E_INVALIDARG;
    const size_t index = key->uVKey == 'K' ? 0 : 1;
    preserve_calls.push_back({client, {command, *key}});
    if (preserve_result[index] == S_OK)
      preserved_keys.push_back({client, {command, *key}});
    if (preserve_hook)
      preserve_hook(index);
    if (throw_preserve == static_cast<int>(index))
      throw 1;
    return preserve_result[index];
  }
  HRESULT STDMETHODCALLTYPE UnpreserveKey(REFGUID command, const TF_PRESERVEDKEY *key) override {
    if (!key)
      return E_INVALIDARG;
    unpreserved_keys.push_back({command, *key});
    const size_t index = key->uVKey == 'K' ? 0 : 1;
    if (unpreserve_hook)
      unpreserve_hook(index);
    if (throw_unpreserve == static_cast<int>(index))
      throw 1;
    return unpreserve_result[index];
  }
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
  std::function<void()> release_hook;
  ProfileManager() {
    profile.dwProfileType = TF_PROFILETYPE_INPUTPROCESSOR;
    profile.clsid = service_id;
    profile.guidProfile = profiles[0].id;
    profile.langid = profiles[0].language;
    profile.dwFlags = TF_IPP_FLAG_ACTIVE;
    profile.hkl = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0xe0200409));
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (release_hook)
      release_hook();
    return --references;
  }
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
          decltype(&GetKeyboardLayoutList) layouts = GetKeyboardLayoutList,
          const native_field::Source *field_source = nullptr) {
    root = controlled_root;
    Require(root != nullptr, "controlled RichEdit metadata fixture created");
    a.window = GetDlgItem(root, 101);
    b.window = GetDlgItem(root, 102);
    manager.top = &a;
    ::SetActiveWindow(root);
    ::SetFocus(a.window);
    Require(::GetFocus() == a.window, "fixture native focus established");
    service.Attach(
        new Service(profile_api, layouts, vocabulary::ModuleOwner::Process, field_source));
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
struct KeyboardState {
  BYTE previous[256]{};
  BYTE state[256]{};
  KeyboardState() {
    Require(GetKeyboardState(previous) && SetKeyboardState(state),
            "test thread keyboard state captured and cleared");
  }
  ~KeyboardState() { Require(SetKeyboardState(previous), "test thread keyboard state restored"); }
  void Set(int key, BYTE value) {
    state[key] = value;
    state[VK_CONTROL] = state[VK_LCONTROL] | state[VK_RCONTROL];
    state[VK_MENU] = state[VK_LMENU] | state[VK_RMENU];
    state[VK_SHIFT] = state[VK_LSHIFT] | state[VK_RSHIFT];
    Require(SetKeyboardState(state), "controlled keyboard transition applied to this thread");
  }
  bool Down(Harness &h, WPARAM key, LPARAM repeat = 1) {
    LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC));
    BOOL eaten = TRUE;
    Require(h.service->OnKeyDown(&h.a, key, (scan << 16) | repeat, &eaten) == S_OK,
            "actual keydown entrypoint completes");
    return eaten != FALSE;
  }
  void Up(Harness &h, WPARAM key) {
    BOOL eaten = TRUE;
    Require(h.service->OnKeyUp(&h.a, key, 0xc0000001, &eaten) == S_OK && !eaten,
            "actual keyup entrypoint passes");
  }
  bool Chord(Harness &h, WPARAM key) {
    int reads = h.a.reads, writes = h.a.writes;
    int requests = h.a.calls["RequestEditSession"];
    Set(VK_LCONTROL, 0x80);
    Require(!Down(h, VK_CONTROL), "Ctrl down passes");
    Set(VK_LMENU, 0x80);
    Require(!Down(h, VK_MENU), "Alt down passes");
    Require(h.a.reads == reads && h.a.writes == writes &&
                h.a.calls["RequestEditSession"] == requests,
            "modifier downs neither inspect text nor request an edit");
    bool eaten = Down(h, key);
    Up(h, key);
    Set(VK_LMENU, 0);
    Up(h, VK_MENU);
    Set(VK_LCONTROL, 0);
    Up(h, VK_CONTROL);
    return eaten;
  }
  void Type(Harness &h, const char *keys) {
    for (const char *p = keys; *p; ++p) {
      Require(Down(h, static_cast<WPARAM>(*p)), "physical letter is owned");
      Up(h, static_cast<WPARAM>(*p));
    }
  }
};
bool PreservedCommandSequences() {
  using diagnostics::Counter;
  bool passed = true;
  auto check = [&](bool condition, const char *label) {
    if (!condition) {
      fprintf(stderr, "FAIL: %s\n", label);
      passed = false;
    }
  };
  std::vector<Manager::CommandKey> stable_bindings;
  for (int activation = 0; activation != 2; ++activation) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    auto &manager = h.manager;
    const bool advised = manager.key_advises == 1 && manager.key_sink && manager.key_client == 42 &&
                         manager.key_foreground;
    check(advised, "preserved commands use the actual advised foreground sink and client");
    if (manager.preserved_keys.size() != 2) {
      fprintf(stderr, "FAIL: preserved command registrations activation=%d expected=2 actual=%zu\n",
              activation + 1, manager.preserved_keys.size());
      passed = false;
    }
    std::vector<Manager::CommandKey> bindings;
    for (UINT key : {static_cast<UINT>('K'), static_cast<UINT>('Z')}) {
      auto found = std::find_if(
          manager.preserved_keys.begin(), manager.preserved_keys.end(),
          [&](const Manager::Registration &entry) { return entry.binding.key.uVKey == key; });
      if (found == manager.preserved_keys.end())
        continue;
      check(found->client == manager.key_client && found->binding.command != GUID_NULL &&
                found->binding.key.uModifiers == (TF_MOD_LCONTROL | TF_MOD_LALT) &&
                found->binding.key.uModifiers == 0xc0,
            "preserved command binds its activation client and exact left Ctrl Alt keydown pair");
      bindings.push_back(found->binding);
    }
    check(bindings.size() == 2, "activation registers both K and Z command bindings");
    if (bindings.size() == 2) {
      check(bindings[0].command != bindings[1].command,
            "manual and reverse use distinct nonnull command GUIDs");
      if (activation == 0)
        stable_bindings = bindings;
      else
        check(bindings == stable_bindings,
              "command GUID and key bindings are stable across activations");
    }
    if (advised) {
      BOOL eaten = TRUE;
      check(manager.key_sink->OnPreservedKey(&h.a, GUID_NULL, &eaten) == S_OK && !eaten &&
                h.a.reads == 0 && h.a.writes == 0 && h.a.starts == 0 &&
                h.a.calls["RequestEditSession"] == 0,
            "unknown preserved GUID passes without text inspection or an edit request");
    }
    if (advised && manager.preserved_keys.size() == 2 && bindings.size() == 2) {
      keyboard.Type(h, "GHBDTN");
      h.Oracle(L"ghbdtn", 13);
      auto *composition = h.a.composition.Get();
      const int starts = h.a.starts, ends = h.a.ends;
      keyboard.Set(VK_LCONTROL, 0x80);
      keyboard.Set(VK_LMENU, 0x80);
      for (const auto &binding : bindings) {
        const int writes = h.a.writes, requests = h.a.calls["RequestEditSession"];
        const auto before = DiagnosticSnapshot(*h.service.Get());
        BOOL eaten = FALSE;
        check(manager.key_sink->OnPreservedKey(&h.a, binding.command, &eaten) == S_OK && eaten,
              "registered preserved command completes through the advised sink and consumes");
        const auto after = DiagnosticSnapshot(*h.service.Get());
        check(h.a.writes == writes + 1 && h.a.calls["RequestEditSession"] == requests + 1 &&
                  h.a.max_request_writes == 1 &&
                  after.counts[static_cast<size_t>(Counter::Attempted)] ==
                      before.counts[static_cast<size_t>(Counter::Attempted)] + 1 &&
                  after.counts[static_cast<size_t>(Counter::Verified)] ==
                      before.counts[static_cast<size_t>(Counter::Verified)] + 1 &&
                  after.counts[static_cast<size_t>(Counter::Unknown)] ==
                      before.counts[static_cast<size_t>(Counter::Unknown)],
              "each preserved command makes one requested verified write without unknown effects");
        const wchar_t *word = binding.key.uVKey == 'K' ? L"привет" : L"ghbdtn";
        check(h.a.text == std::wstring(L"prefix ") + word + L"suffix" && h.a.caret == 13 &&
                  h.b.text == L"prefix suffix" && h.b.caret == 7 && h.b.writes == 0,
              "preserved conversion and reversal preserve exact whole documents and end caret");
        check(
            h.a.composition.Get() == composition && h.a.active_composition &&
                h.a.starts == starts && h.a.ends == ends,
            "preserved commands retain the same owned composition without commit or reacquisition");
      }
    }
    check(h.service->Deactivate() == S_OK && manager.key_sink == nullptr,
          "deactivation clears the captured preserved-command sink");
    check(manager.unpreserved_keys.size() == 2 &&
              std::all_of(manager.preserved_keys.begin(), manager.preserved_keys.end(),
                          [&](const Manager::Registration &entry) {
                            return std::count(manager.unpreserved_keys.begin(),
                                              manager.unpreserved_keys.end(), entry.binding) == 1;
                          }),
          "deactivation unregisters exactly the two acquired command pairs once");
    const auto cleanup = manager.unpreserved_keys;
    check(h.service->Deactivate() == S_OK && manager.unpreserved_keys == cleanup &&
              manager.key_unadvises == 1,
          "repeated deactivation does not unregister command pairs or sink twice");
  }
  return passed;
}
void PreservedActivationSchedules() {
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    h.service->Deactivate();
    int callbacks = 0;
    h.manager.preserve_hook = [&](size_t) {
      ++callbacks;
      const GUID id = h.manager.preserve_calls.back().binding.command;
      BOOL eaten = TRUE;
      Require(h.manager.key_sink->OnPreservedKey(&h.a, id, &eaten) == S_OK && !eaten &&
                  !h.a.reads && !h.a.writes && !h.a.starts && !h.a.calls["RequestEditSession"],
              "native callbacks during provisional registration have no editing authority");
    };
    Require(h.service->Activate(&h.manager, 42) == S_OK && callbacks == 2,
            "editing generation changes cannot cancel or duplicate activation registrations");
    h.manager.preserve_hook = nullptr;
    h.Oracle(L"", 7);
  }
  for (int position : {0, 1}) {
    for (HRESULT failure : {E_FAIL, S_FALSE, TF_E_ALREADY_EXISTS}) {
      ProfileManager profile;
      Harness h(&profile, ProofLayouts);
      h.service->Deactivate();
      auto &manager = h.manager;
      manager.preserve_calls.clear();
      manager.unpreserved_keys.clear();
      manager.preserve_result[position] = failure;
      const int keys = manager.key_unadvises, threads = manager.thread_unadvises,
                profile_unadvises = manager.profile_unadvises;
      Require(FAILED(h.service->Activate(&manager, 42)) &&
                  manager.preserve_calls.size() == static_cast<size_t>(position + 1) &&
                  manager.unpreserved_keys.size() == static_cast<size_t>(position) &&
                  manager.key_unadvises == keys + 1 && manager.thread_unadvises == threads + 1 &&
                  manager.profile_unadvises == profile_unadvises + 1 && manager.references == 1,
              "registration failure drains only the exact successful prefix and every sink");
      if (position)
        Require(manager.unpreserved_keys.front() == manager.preserve_calls.front().binding,
                "failed or duplicate second registration never becomes cleanup ownership");
      const auto cleanup = manager.unpreserved_keys;
      Require(h.service->Deactivate() == S_OK && manager.unpreserved_keys == cleanup,
              "failed activation cannot unregister anything twice");
      manager.preserve_result[position] = S_OK;
      Require(h.service->Activate(&manager, 42) == S_OK,
              "known registration failure permits fresh activation after complete rollback");
      h.Oracle(L"", 7);
      Require(!h.a.reads && !h.a.writes && !h.a.starts, "registration never inspects text");
    }
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    h.service->Deactivate();
    h.manager.unpreserved_keys.clear();
    h.manager.throw_preserve = position;
    Require(FAILED(h.service->Activate(&h.manager, 42)) &&
                h.manager.unpreserved_keys.size() == static_cast<size_t>(position),
            "throwing registration drains known successes without claiming the unknown effect");
    h.manager.throw_preserve = -1;
    const auto attempts = h.manager.preserve_calls.size();
    Require(FAILED(h.service->Activate(&h.manager, 42)) &&
                h.manager.preserve_calls.size() == attempts && h.service->Deactivate() == S_OK,
            "uncertain acquisition faults the instance without retry or guessed unregister");
  }
  for (int boundary = 0; boundary != 5; ++boundary) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    h.service->Deactivate();
    auto &manager = h.manager;
    manager.unpreserved_keys.clear();
    const int keys = manager.key_unadvises, threads = manager.thread_unadvises,
              profile_unadvises = manager.profile_unadvises;
    bool cancelled = false;
    auto cancel = [&] {
      cancelled = true;
      Require(h.service->Deactivate() == S_OK && h.service->Activate(&manager, 42) == E_INVALIDARG,
              "provisional cancellation leaves rollback with the outer activation owner");
    };
    manager.advise_hook = [&](int position) {
      if (position == boundary)
        cancel();
    };
    manager.preserve_hook = [&](size_t position) {
      if (static_cast<int>(position) + 3 == boundary)
        cancel();
    };
    int releases = 0;
    manager.release_hook = [&] {
      ++releases;
      Require(h.service->Deactivate() == S_OK && h.service->Activate(&manager, 42) == E_INVALIDARG,
              "rollback retains the activation barrier through captured COM releases");
    };
    Require(FAILED(h.service->Activate(&manager, 42)) && cancelled && releases == 3 &&
                manager.unpreserved_keys.size() ==
                    static_cast<size_t>(boundary < 3 ? 0 : boundary - 2) &&
                manager.thread_unadvises == threads + 1 &&
                manager.profile_unadvises == profile_unadvises + (boundary >= 1 ? 1 : 0) &&
                manager.key_unadvises == keys + (boundary >= 2 ? 1 : 0),
            "late exact sink and command successes are recorded before cancellation rollback");
    manager.release_hook = nullptr;
    manager.advise_hook = nullptr;
    manager.preserve_hook = nullptr;
    Require(h.service->Activate(&manager, 42) == S_OK,
            "cancelled activation can restart after every captured release returns");
    h.Oracle(L"", 7);
  }
  for (int position : {0, 1, 2}) {
    for (bool throwing : {false, true}) {
      ProfileManager profile;
      Harness h(&profile, ProofLayouts);
      h.service->Deactivate();
      auto &manager = h.manager;
      const int keys = manager.key_unadvises, threads = manager.thread_unadvises,
                profile_unadvises = manager.profile_unadvises;
      if (throwing)
        manager.throw_sink = position;
      else
        manager.sink_result[position] = S_FALSE;
      Require(FAILED(h.service->Activate(&manager, 42)) &&
                  manager.thread_unadvises == threads + (position > 0 ? 1 : 0) &&
                  manager.profile_unadvises == profile_unadvises + (position > 1 ? 1 : 0) &&
                  manager.key_unadvises == keys,
              "failed and throwing sink acquisitions never claim an unproven sink");
      manager.throw_sink = -1;
      manager.sink_result[position] = S_OK;
      Require(throwing ? FAILED(h.service->Activate(&manager, 42))
                       : h.service->Activate(&manager, 42) == S_OK,
              "only unknown sink acquisition faults the activation instance");
    }
  }
  for (int position : {0, 1}) {
    for (int outcome = 0; outcome != 4; ++outcome) {
      ProfileManager profile;
      Harness h(&profile, ProofLayouts);
      auto &manager = h.manager;
      if (outcome == 0)
        manager.unpreserve_result[position] = E_FAIL;
      if (outcome == 1)
        manager.unpreserve_result[position] = S_FALSE;
      if (outcome == 2)
        manager.throw_unpreserve = position;
      if (outcome == 3)
        manager.unpreserve_result[position] = CONNECT_E_NOCONNECTION;
      manager.unpreserve_hook = [&](size_t) {
        Require(h.service->Deactivate() == S_OK &&
                    h.service->Activate(&manager, 42) == E_INVALIDARG,
                "detached pair ledger prevents nested cleanup and overlapping registration");
      };
      int releases = 0;
      manager.release_hook = [&] {
        ++releases;
        Require(h.service->Activate(&manager, 42) == E_INVALIDARG,
                "active teardown holds its barrier through COM releases");
      };
      Require(FAILED(h.service->Deactivate()) && manager.unpreserved_keys.size() == 2 &&
                  manager.unpreserved_keys[0] == manager.preserved_keys[1].binding &&
                  manager.unpreserved_keys[1] == manager.preserved_keys[0].binding &&
                  manager.key_unadvises == 1 && manager.thread_unadvises == 1 &&
                  manager.profile_unadvises == 1 && manager.diagnostic_unadvises == 1 &&
                  releases >= 4,
              "each failed pair cleanup still drains both pairs and every independent sink");
      manager.release_hook = nullptr;
      manager.unpreserve_hook = nullptr;
      manager.unpreserve_result[position] = S_OK;
      manager.throw_unpreserve = -1;
      const auto attempts = manager.preserve_calls.size();
      const auto cleanup = manager.unpreserved_keys;
      Require(h.service->Deactivate() == S_OK && manager.unpreserved_keys == cleanup,
              "completed cleanup never retries a failed pair");
      const auto activated = h.service->Activate(&manager, 42);
      Require(outcome == 3 ? activated == S_OK
                           : FAILED(activated) && manager.preserve_calls.size() == attempts,
              "documented pair absence permits reuse while uncertain cleanup faults permanently");
      h.Oracle(L"", 7);
    }
  }
  for (int position : {0, 1, 2}) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    h.manager.unsink_result[position] = S_FALSE;
    Require(FAILED(h.service->Deactivate()) && h.manager.unpreserved_keys.size() == 2 &&
                h.manager.key_unadvises == 1 && h.manager.thread_unadvises == 1 &&
                h.manager.profile_unadvises == 1 && h.manager.diagnostic_unadvises == 1 &&
                FAILED(h.service->Activate(&h.manager, 42)),
            "nonexact sink cleanup drains independent resources then faults the instance");
  }
  for (bool cancel : {false, true}) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    h.service->Deactivate();
    auto &manager = h.manager;
    manager.unpreserved_keys.clear();
    manager.unpreserve_result[0] = E_FAIL;
    if (cancel)
      manager.preserve_hook = [&](size_t index) {
        if (index == 1)
          h.service->Deactivate();
      };
    else
      manager.preserve_result[1] = E_FAIL;
    Require(FAILED(h.service->Activate(&manager, 42)) &&
                manager.unpreserved_keys.size() == (cancel ? 2u : 1u) &&
                manager.key_unadvises == 2 && manager.thread_unadvises == 2 &&
                manager.profile_unadvises == 2,
            "failed and cancelled acquisition propagate rollback uncertainty after independent "
            "cleanup");
    manager.preserve_hook = nullptr;
    manager.unpreserve_result[0] = manager.preserve_result[1] = S_OK;
    Require(FAILED(h.service->Activate(&manager, 42)),
            "rollback uncertainty cannot flatten into a reusable inactive service");
  }
  for (bool during_query : {false, true}) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    h.service->Deactivate();
    auto notify = [&] {
      h.service->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, profiles[1].language, service_id,
                             GUID_TFCAT_TIP_KEYBOARD, profiles[1].id, nullptr,
                             TF_IPSINK_FLAG_ACTIVE);
    };
    if (during_query)
      profile.hook = [&](const char *name) {
        if (std::string(name) == "GetActiveProfile")
          notify();
      };
    else
      h.manager.preserve_hook = [&](size_t) {
        notify();
        profile.profile.guidProfile = profiles[1].id;
        profile.profile.langid = profiles[1].language;
      };
    Require(h.service->Activate(&h.manager, 42) == S_OK &&
                DiagnosticSnapshot(*h.service.Get()).mode == KR_RU &&
                h.manager.preserve_calls.size() == 4,
            "profile notifications do not cancel registration or get overwritten by older query "
            "results");
    profile.hook = nullptr;
    h.manager.preserve_hook = nullptr;
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    h.service->Deactivate();
    HRESULT nested = S_OK;
    h.manager.diagnostic_advise_hook = [&] {
      Require(h.service->Deactivate() == S_OK, "optional startup revokes the published activation");
      nested = h.service->Activate(&h.manager, 42);
    };
    h.manager.release_hook = [&] {
      Require(h.service->Activate(&h.manager, 42) == E_INVALIDARG,
              "optional startup captured releases cannot reopen the outer activation frame");
    };
    Require(FAILED(h.service->Activate(&h.manager, 42)) && nested == E_INVALIDARG &&
                !DiagnosticSnapshot(*h.service.Get()).active,
            "cancelled optional startup cannot report activation success");
    h.manager.release_hook = nullptr;
    h.manager.diagnostic_advise_hook = nullptr;
    Require(h.service->Activate(&h.manager, 42) == S_OK,
            "optional startup cancellation permits activation after the whole frame drains");
  }
}

bool Preserved(Harness &h, UINT key) {
  const auto found =
      std::find_if(h.manager.preserved_keys.begin(), h.manager.preserved_keys.end(),
                   [&](const auto &entry) { return entry.binding.key.uVKey == key; });
  Require(found != h.manager.preserved_keys.end() && h.manager.key_sink,
          "dispatch uses the command actually registered with the advised sink");
  const GUID id = found->binding.command;
  BOOL eaten = FALSE;
  Require(h.manager.key_sink->OnPreservedKey(&h.a, id, &eaten) == S_OK,
          "preserved command callback contains native failures");
  return eaten != FALSE;
}
void PreservedDispatchSchedules() {
  for (const auto &entry : profiles) {
    ProfileManager profile;
    profile.profile.guidProfile = entry.id;
    profile.profile.langid = entry.language;
    profile.substitute =
        reinterpret_cast<HKL>(static_cast<ULONG_PTR>(entry.language) << 16 | entry.language);
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    const bool english = entry.mode == KR_EN;
    keyboard.Type(h, english ? "GHBDTN" : "HELLO");
    const wchar_t *original = english ? L"ghbdtn" : L"руддщ";
    const wchar_t *converted = english ? L"привет" : L"hello";
    const LONG caret = english ? 13 : 12;
    h.Oracle(original, caret);
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    keyboard.Set(VK_CAPITAL, 1);
    const auto ordinary_callbacks =
        DiagnosticSnapshot(*h.service.Get())
            .counts[static_cast<size_t>(diagnostics::Counter::KeyEntered)];
    const auto *composition = h.a.composition.Get();
    const int writes = h.a.writes;
    Require(Preserved(h, 'K'), "Caps Lock permits an admitted manual command in both modes");
    h.Oracle(converted, caret);
    Require(h.a.writes == writes + 1 && h.a.composition.Get() == composition && h.a.starts == 1 &&
                h.a.ends == 0 && h.a.max_request_writes == 1,
            "manual conversion retains exact composition ownership with one write");
    for (int repeat = 0; repeat != 2; ++repeat) {
      Require(Preserved(h, 'K') && h.a.writes == writes + repeat + 2 && h.a.max_request_writes == 1,
              "each repeated native manual callback maps the owned text once without physical "
              "deduplication");
      h.Oracle(repeat == 0 ? original : converted, caret);
    }
    Require(Preserved(h, 'Z') && h.a.writes == writes + 4,
            "preserved reversal writes its first owned receipt once");
    h.Oracle(original, caret);
    for (int repeat = 0; repeat != 3; ++repeat)
      Require(Preserved(h, 'Z') && h.a.writes == writes + 4,
              "held reversal callbacks cannot reuse an expired receipt");
    Require(DiagnosticSnapshot(*h.service.Get())
                    .counts[static_cast<size_t>(diagnostics::Counter::KeyEntered)] ==
                ordinary_callbacks,
            "preserved callbacks never impersonate ordinary-key diagnostic callbacks");
  }
  for (int extra : {VK_LSHIFT, VK_RSHIFT, VK_RCONTROL, VK_RMENU, VK_LWIN, VK_RWIN, 0, 1, 2}) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    if (extra > 2)
      keyboard.Set(extra, 0x80);
    if (extra == 0)
      keyboard.Set(VK_LCONTROL, 0);
    if (extra == 1)
      keyboard.Set(VK_LMENU, 0);
    if (extra == 2) {
      keyboard.Set(VK_LMENU, 0);
      keyboard.Set(VK_RMENU, 0x80);
    }
    const auto snapshot = DiagnosticSnapshot(*h.service.Get());
    const int reads = h.a.reads, writes = h.a.writes, requests = h.a.calls["RequestEditSession"];
    auto *composition = h.a.composition.Get();
    Require(!Preserved(h, 'K') && !Preserved(h, 'Z') && h.a.reads == reads &&
                h.a.writes == writes && h.a.calls["RequestEditSession"] == requests &&
                h.a.composition.Get() == composition && h.a.active_composition &&
                DiagnosticSnapshot(*h.service.Get()) == snapshot,
            "wrong real modifier state passes without finishing or inspecting an owned word");
    h.Oracle(L"ghbdtn", 13);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    const auto snapshot = DiagnosticSnapshot(*h.service.Get());
    const auto profile_calls = profile.active_calls;
    const GUID id = h.manager.preserved_keys.front().binding.command;
    BOOL eaten = TRUE;
    Require(h.service->OnPreservedKey(&h.a, id, nullptr) == E_POINTER &&
                h.service->OnPreservedKey(nullptr, id, &eaten) == S_OK && !eaten,
            "missing preserved output or context cannot begin admission");
    const auto after_missing = DiagnosticSnapshot(*h.service.Get());
    Require(h.service->OnPreservedKey(nullptr, GUID_NULL, &eaten) == S_OK && !eaten &&
                DiagnosticSnapshot(*h.service.Get()) == after_missing &&
                profile.active_calls == profile_calls,
            "unknown GUID is a pure pass before profile and keyboard admission");
    HRESULT wrong_thread = S_OK;
    eaten = TRUE;
    std::thread other([&] { wrong_thread = h.service->OnPreservedKey(&h.a, id, &eaten); });
    other.join();
    Require(wrong_thread == RPC_E_WRONG_THREAD && !eaten &&
                DiagnosticSnapshot(*h.service.Get()) == after_missing && h.a.reads == 0 &&
                h.a.writes == 0 && h.a.starts == 0 && snapshot.counts == after_missing.counts,
            "foreign-thread command cannot inspect mutable service state or document text");
    h.Oracle(L"", 7);
  }
  for (int scenario = 0; scenario != 10; ++scenario) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
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
    if (scenario == 8)
      h.a.foreign = true;
    if (scenario == 9)
      h.manager.top = &h.b;
    ::SetFocus(h.a.window);
    Require(!Preserved(h, 'K') && !Preserved(h, 'Z') && !h.a.reads && !h.a.writes && !h.a.starts &&
                !h.b.reads && !h.b.writes && !h.b.starts,
            "excluded foreign and unfocused preserved commands have no text access or effect");
    h.Oracle(L"", 7);
  }
  for (int scenario = 0; scenario != 8; ++scenario) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    const int writes = h.a.writes, reads = h.a.reads;
    if (scenario < 3) {
      h.a.callback = false;
      if (scenario == 0)
        h.a.request_result = E_ACCESSDENIED;
      if (scenario == 1)
        h.a.session_result = TF_E_SYNCHRONOUS;
      if (scenario == 2)
        h.a.session_result = TF_S_ASYNC;
    }
    if (scenario == 3)
      --h.a.caret;
    if (scenario == 4)
      h.a.composition->span->start++;
    if (scenario == 5)
      h.a.text[7] = L'X';
    if (scenario == 6)
      h.a.foreign = true;
    if (scenario == 7)
      h.a.write_result = E_FAIL;
    const bool attempted = scenario == 7;
    Require(Preserved(h, 'K') == attempted && h.a.writes == writes + (attempted ? 1 : 0) &&
                h.service->stopped() && h.a.starts == 1 && h.a.ends == 0,
            "preserved refusal passes before write and attempted unknown consumes without replay");
    if (scenario < 3)
      Require(h.a.reads == reads && h.a.retained_request &&
                  FAILED(h.a.retained_request->DoEditSession(123)) && h.a.writes == writes,
              "denied and late preserved edit sessions cannot inspect or write owned text");
    Require(!Preserved(h, 'K') && !Preserved(h, 'Z') && h.a.writes == writes + (attempted ? 1 : 0),
            "stopped command authority cannot retry an uncertain or stale edit");
    h.Oracle(attempted       ? L"привет"
             : scenario == 5 ? L"Xhbdtn"
                             : L"ghbdtn",
             scenario == 3 ? 12 : 13);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    Require(keyboard.Down(h, VK_SPACE), "ordinary fixture correction retains its boundary receipt");
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    Require(Preserved(h, 'Z'), "preserved command reverses an ordinary retained boundary receipt");
    h.Oracle(L"ghbdtn ", 14);
    keyboard.Set(VK_LCONTROL, 0);
    keyboard.Set(VK_LMENU, 0);
    Require(keyboard.Down(h, 'X'), "ordinary continuation follows a preserved reversal");
    const int writes = h.a.writes;
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    Require(Preserved(h, 'Z') && h.a.writes == writes,
            "preserved command cannot revive a receipt after ordinary continuation");
    h.Oracle(L"ghbdtn x", 15);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    bool released = false;
    profile.release_hook = [&] {
      if (released)
        return;
      released = true;
      Require(h.service->Deactivate() == S_OK && h.service->Activate(&h.manager, 42) == S_OK,
              "profile release can change activation after admission facts are copied");
    };
    const int writes = h.a.writes;
    Require(!Preserved(h, 'K') && released && h.a.writes == writes && h.a.starts == 1,
            "single-use dispatch rejects stale facts after callback-capable admission releases");
    profile.release_hook = nullptr;
    h.Oracle(L"ghbdtn", 13);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    Require(h.service->OnCompositionTerminated(123, h.a.composition.Get()) == S_OK,
            "host terminates the exact owned word before command delivery");
    const int writes = h.a.writes, starts = h.a.starts;
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    Preserved(h, 'K');
    Preserved(h, 'Z');
    Require(h.a.writes == writes && h.a.starts == starts,
            "delivery after exact termination cannot recover committed text or acquire a "
            "replacement mark");
    h.Oracle(L"ghbdtn", 13);
    ComPtr<ITfKeyEventSink> retained = h.manager.key_sink;
    const GUID id = h.manager.preserved_keys.front().binding.command;
    h.service->Deactivate();
    const int reads = h.a.reads;
    BOOL eaten = TRUE;
    Require(retained->OnPreservedKey(&h.a, id, &eaten) == S_OK && !eaten && h.a.reads == reads &&
                h.a.writes == writes && h.a.starts == starts,
            "retained callback after deactivation cannot restore old editing authority");
  }
}

void CommandKeySequences() {
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    auto *composition = h.a.composition.Get();
    Require(keyboard.Chord(h, 'K') && h.a.composition.Get() == composition,
            "full manual chord converts the same owned composition");
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442", 13);
    Require(keyboard.Chord(h, 'Z') && h.a.composition.Get() == composition,
            "full reverse chord restores the same owned receipt");
    h.Oracle(L"ghbdtn", 13);
    Require(h.a.max_request_writes == 1, "each command plan performs at most one write");
  }
  for (WPARAM key : {static_cast<WPARAM>('K'), static_cast<WPARAM>('Z')}) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    Require(keyboard.Chord(h, key), "acknowledged idle command reserves its key without writing");
    h.Oracle(L"", 7);
    Require(h.a.writes == 0 && h.a.starts == 0, "idle command never creates a composition");
    keyboard.Type(h, "HELLO");
    int writes = h.a.writes;
    if (key == 'Z') {
      Require(keyboard.Chord(h, key) && h.a.writes == writes && h.a.active_composition,
              "absent reversal receipt consumes without ending the owned word");
      h.Oracle(L"hello", 12);
    }
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    Require(keyboard.Down(h, VK_SPACE), "fixture correction retains boundary receipt");
    Require(keyboard.Chord(h, 'Z'), "reverse chord restores retained fixture boundary");
    h.Oracle(L"ghbdtn ", 14);
    Require(keyboard.Down(h, 'X'), "continuation expires boundary receipt and starts next word");
    int writes = h.a.writes;
    Require(keyboard.Chord(h, 'Z') && h.a.writes == writes,
            "expired reversal command consumes but does not rewrite committed text");
    h.Oracle(L"ghbdtn x", 15);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "G");
    auto *composition = h.a.composition.Get();
    int requests = h.a.calls["RequestEditSession"];
    keyboard.Set(VK_LSHIFT, 0x80);
    Require(!keyboard.Down(h, VK_SHIFT) && h.a.calls["RequestEditSession"] == requests &&
                h.a.composition.Get() == composition,
            "Shift down preserves the owned word without an edit session");
    Require(keyboard.Down(h, 'H'), "shifted letter stays in the owned word");
    keyboard.Set(VK_LSHIFT, 0);
    keyboard.Up(h, VK_SHIFT);
    keyboard.Type(h, "B");
    h.Oracle(L"gHb", 10);
    keyboard.Set(VK_LWIN, 0x80);
    Require(!keyboard.Down(h, VK_LWIN) && h.a.active_composition,
            "Windows modifier down preserves the owned word");
    keyboard.Set(VK_LWIN, 0);
    keyboard.Up(h, VK_LWIN);
    Require(h.a.composition.Get() == composition && h.a.starts == 1,
            "modifier transitions do not replace composition identity");
  }
  for (int extra : {VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN, VK_RMENU, VK_RCONTROL}) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    keyboard.Set(extra, 0x80);
    Require(!keyboard.Chord(h, 'K') && !h.a.active_composition,
            "extra modifier refuses manual chord and finishes ordinary shortcut");
    h.Oracle(L"ghbdtn", 13);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    keyboard.Set(VK_LCONTROL, 0x80);
    Require(!keyboard.Down(h, VK_CONTROL), "AltGr Ctrl prefix passes");
    keyboard.Set(VK_RMENU, 0x80);
    Require(!keyboard.Down(h, VK_MENU) && h.a.active_composition,
            "AltGr modifier prefix preserves the owned word");
    Require(!keyboard.Down(h, 'K') && !h.a.active_composition,
            "AltGr K finishes and passes without manual conversion");
    h.Oracle(L"ghbdtn", 13);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    Require(!keyboard.Down(h, 'K', 2) && !h.a.active_composition,
            "coalesced command repeat finishes and passes original message intact");
    h.Oracle(L"ghbdtn", 13);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    int reads = h.a.reads, writes = h.a.writes;
    profile.substitute = nullptr;
    keyboard.Set(VK_LCONTROL, 0x80);
    Require(!keyboard.Down(h, VK_CONTROL) && h.service->stopped() && h.a.reads == reads &&
                h.a.writes == writes,
            "modifier down still refuses a lost substitute layout before document inspection");
    h.Oracle(L"ghbdtn", 13);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    keyboard.Set(VK_CAPITAL, 1);
    Require(keyboard.Chord(h, 'K'), "Caps Lock does not reject reserved physical chord");
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442", 13);
  }
  for (WPARAM special : {static_cast<WPARAM>(VK_BACK), static_cast<WPARAM>(VK_ESCAPE)}) {
    for (bool shift : {false, true}) {
      ProfileManager profile;
      Harness h(&profile, ProofLayouts);
      KeyboardState keyboard;
      keyboard.Type(h, "GHBDTN");
      keyboard.Set(VK_LSHIFT, shift ? 0x80 : 0);
      bool eaten = keyboard.Down(h, special);
      Require(eaten == (!shift && special == VK_BACK),
              "Backspace edits only without Shift and unreserved Escape passes");
      h.Oracle(!shift && special == VK_BACK ? L"ghbdt" : L"ghbdtn",
               !shift && special == VK_BACK ? 12 : 13);
      Require(h.a.active_composition == (!shift && special == VK_BACK),
              "shifted special keys finish and pass");
    }
  }
  for (int failure = 0; failure != 5; ++failure) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    if (failure == 0) {
      h.a.callback = false;
      h.a.request_result = TF_E_SYNCHRONOUS;
    }
    if (failure == 1)
      h.a.request_result = E_FAIL;
    if (failure == 2)
      h.a.session_result = E_FAIL;
    if (failure == 3)
      h.a.throw_after_callback = true;
    if (failure == 4)
      h.a.hook = [&](const char *name) {
        if (std::string(name) == "GetSelection")
          h.service->OnSetFocus(FALSE);
      };
    Require(!keyboard.Chord(h, 'K'),
            "denied, failed, throwing or stale nonwriting command passes without reservation");
    h.a.hook = nullptr;
    h.Oracle(L"", 7);
    Require(h.a.writes == 0 && h.a.starts == 0, "failed idle command has no native effect");
  }
  for (int failure = 0; failure != 4; ++failure) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    int writes = h.a.writes;
    if (failure == 0)
      --h.a.caret;
    if (failure == 1)
      h.a.write_result = E_FAIL;
    bool injected = false;
    if (failure >= 2)
      h.a.hook = [&](const char *name) {
        if (!injected && std::string(name) == (failure == 2 ? "GetRange" : "SetText.after")) {
          injected = true;
          BOOL nested = TRUE;
          Require(h.service->OnKeyDown(&h.a, VK_SHIFT, 1, &nested) == S_OK && !nested,
                  "reentrant modifier passes and invalidates command authority");
        }
      };
    bool eaten = keyboard.Chord(h, 'K');
    h.a.hook = nullptr;
    bool attempted = failure == 1 || failure == 3;
    Require(eaten == attempted && h.a.writes == writes + (attempted ? 1 : 0) &&
                h.service->stopped(),
            "manual refusal passes and attempted unknown consumes with no replay");
    h.Oracle(attempted ? L"\x043f\x0440\x0438\x0432\x0435\x0442" : L"ghbdtn",
             failure == 0 ? 12 : 13);
    Require(!keyboard.Down(h, 'X') && h.a.writes == writes + (attempted ? 1 : 0),
            "stopped unknown command never retries or repairs");
  }
  for (bool secure : {false, true}) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    h.a.scope = secure ? IS_PASSWORD : IS_EMAIL_SMTPEMAILADDRESS;
    Require(!keyboard.Chord(h, 'K') && h.a.reads == 0 && h.a.writes == 0 && h.a.starts == 0,
            "commands do not inspect or compose in excluded or uncertain controls");
    h.Oracle(L"", 7);
  }
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
HRESULT AdmissionProbe(Harness &h, bool preserved, BOOL *eaten) {
  return preserved ? h.manager.key_sink->OnPreservedKey(
                         &h.a, h.manager.preserved_keys.front().binding.command, eaten)
                   : h.service->OnKeyDown(&h.a, VK_F12, 1, eaten);
}
void DedicatedLayoutNegatives(bool preserved = false) {
  KeyboardState keyboard;
  if (preserved) {
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
  }
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
    Require(AdmissionProbe(h, preserved, &eaten) == S_OK && !eaten,
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
    Require(AdmissionProbe(h, preserved, &eaten) == S_OK && !eaten,
            "mismatched profile language or selected mode passes unchanged");
    LayoutRefusalOracle(h, before);
    Require(profile_api.query_calls == 0 && profile_api.substitute_calls == 0,
            "mismatched profile identity cannot query a substitute");
  }
}
void DedicatedLayoutReentry(bool preserved = false) {
  KeyboardState keyboard;
  if (preserved) {
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
  }
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
      const HRESULT result = AdmissionProbe(h, preserved, &eaten);
      Require(injected && !eaten && result == (schedule == 3 ? E_FAIL : S_OK),
              "each reentrant native boundary refuses the outer key before handling");
      LayoutRefusalOracle(h, before);
      profile_api.hook = nullptr;
      if (schedule < 3) {
        const auto resumed = DiagnosticSnapshot(*h.service.Get());
        Require(AdmissionProbe(h, preserved, &eaten) == S_OK && (eaten != FALSE) == preserved,
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
      Require(AdmissionProbe(h, preserved, &eaten) == (schedule == 3 ? E_FAIL : S_OK) && !eaten &&
                  layouts.calls == boundary,
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
  Require(AdmissionProbe(h, preserved, &eaten) == S_OK && !eaten && layouts.calls == 3,
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
  int reads = 0;
  IUnknown *foreign_function = nullptr;
  std::function<void(const char *)> hook;
  ULONG STDMETHODCALLTYPE AddRef() override {
    if (hook)
      hook("addref");
    return ++references;
  }
  ULONG STDMETHODCALLTYPE Release() override {
    if (hook)
      hook("release");
    return --references;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (hook)
      hook(id == IID_IUnknown ? "identity" : "interface");
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
    if (hook)
      hook("function");
    if (foreign_function)
      return foreign_function->QueryInterface(id, reinterpret_cast<void **>(out));
    return QueryInterface(id, reinterpret_cast<void **>(out));
  }
  HRESULT STDMETHODCALLTYPE Copy(uint32_t size, uint32_t requested_version,
                                 diagnostics::SnapshotV2 *out) override {
    Require(size == sizeof(*out) && requested_version == diagnostics::version_v2,
            "poll bounded versioned copy");
    ++reads;
    if (hook)
      hook("copy");
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
struct SourceProfiles final : ITfInputProcessorProfiles, ITfInputProcessorProfileMgr {
  ULONG references = 1;
  LANGID language = profiles[0].language;
  TF_INPUTPROCESSORPROFILE active = Profile(0);
  HRESULT language_result = S_OK, activation_result = S_OK, read_result = S_OK;
  HRESULT target_result = S_OK, enabled_result = S_OK;
  BOOL enabled = TRUE;
  int language_calls = 0, activation_calls = 0;
  std::function<void(const char *)> hook;
  static TF_INPUTPROCESSORPROFILE Profile(size_t index) {
    TF_INPUTPROCESSORPROFILE result{};
    result.dwProfileType = TF_PROFILETYPE_INPUTPROCESSOR;
    result.clsid = service_id;
    result.guidProfile = profiles[index].id;
    result.langid = profiles[index].language;
    result.catid = GUID_TFCAT_TIP_KEYBOARD;
    result.dwFlags = TF_IPP_FLAG_ACTIVE;
    return result;
  }
  void Call(const char *name) {
    if (hook)
      hook(name);
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    Call("native.addref");
    return ++references;
  }
  ULONG STDMETHODCALLTYPE Release() override {
    Call("native.release");
    return --references;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    Call("native.identity");
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITfInputProcessorProfiles)
      *out = static_cast<ITfInputProcessorProfiles *>(this);
    else if (id == IID_ITfInputProcessorProfileMgr)
      *out = static_cast<ITfInputProcessorProfileMgr *>(this);
    if (!*out)
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCurrentLanguage(LANGID *out) override {
    Call("language.read");
    *out = language;
    return read_result;
  }
  HRESULT STDMETHODCALLTYPE GetActiveProfile(REFGUID category,
                                             TF_INPUTPROCESSORPROFILE *out) override {
    Require(category == GUID_TFCAT_TIP_KEYBOARD, "source reads only active keyboard profile");
    Call("profile.read");
    *out = active;
    return read_result;
  }
  HRESULT STDMETHODCALLTYPE GetProfile(DWORD type, LANGID lang, REFCLSID clsid, REFGUID id,
                                       HKL layout, TF_INPUTPROCESSORPROFILE *out) override {
    Fixed(type, lang, clsid, id, layout, 0);
    Call("target.read");
    *out = Profile(1);
    return target_result;
  }
  HRESULT STDMETHODCALLTYPE IsEnabledLanguageProfile(REFCLSID clsid, LANGID lang, REFGUID id,
                                                     BOOL *out) override {
    Fixed(TF_PROFILETYPE_INPUTPROCESSOR, lang, clsid, id, nullptr, 0);
    Call("enabled.read");
    *out = enabled;
    return enabled_result;
  }
  HRESULT STDMETHODCALLTYPE ChangeCurrentLanguage(LANGID lang) override {
    Require(lang == profiles[1].language && language_calls++ == 0,
            "source dispatches exactly one fixed RU language selection");
    language = lang;
    active = Profile(1);
    active.clsid = GUID_NULL;
    active.guidProfile = GUID_NULL;
    Call("language.change");
    return language_result;
  }
  HRESULT STDMETHODCALLTYPE ActivateProfile(DWORD type, LANGID lang, REFCLSID clsid, REFGUID id,
                                            HKL layout, DWORD flags) override {
    Fixed(type, lang, clsid, id, layout, flags);
    Require(language == profiles[1].language && activation_calls++ == 0,
            "source profile activation is once and only after RU language readback");
    active = Profile(1);
    Call("profile.activate");
    return activation_result;
  }
  static void Fixed(DWORD type, LANGID lang, REFCLSID clsid, REFGUID id, HKL layout, DWORD flags) {
    Require(type == TF_PROFILETYPE_INPUTPROCESSOR && lang == profiles[1].language &&
                clsid == service_id && id == profiles[1].id && !layout && flags == 0,
            "source uses only the fixed RU TIP with null layout and zero flags");
  }
  STUB(Register, (REFCLSID));
  STUB(Unregister, (REFCLSID));
  STUB(AddLanguageProfile,
       (REFCLSID, LANGID, REFGUID, const WCHAR *, ULONG, const WCHAR *, ULONG, ULONG));
  STUB(RemoveLanguageProfile, (REFCLSID, LANGID, REFGUID));
  STUB(EnumInputProcessorInfo, (IEnumGUID **));
  STUB(GetDefaultLanguageProfile, (LANGID, REFGUID, CLSID *, GUID *));
  STUB(SetDefaultLanguageProfile, (LANGID, REFCLSID, REFGUID));
  STUB(ActivateLanguageProfile, (REFCLSID, LANGID, REFGUID));
  STUB(GetActiveLanguageProfile, (REFCLSID, LANGID *, GUID *));
  STUB(GetLanguageProfileDescription, (REFCLSID, LANGID, REFGUID, BSTR *));
  STUB(GetLanguageList, (LANGID **, ULONG *));
  STUB(EnumLanguageProfiles, (LANGID, IEnumTfLanguageProfiles **));
  STUB(EnableLanguageProfile, (REFCLSID, LANGID, REFGUID, BOOL));
  STUB(EnableLanguageProfileByDefault, (REFCLSID, LANGID, REFGUID, BOOL));
  STUB(SubstituteKeyboardLayout, (REFCLSID, LANGID, REFGUID, HKL));
  STUB(DeactivateProfile, (DWORD, LANGID, REFCLSID, REFGUID, HKL, DWORD));
  STUB(EnumProfiles, (LANGID, IEnumTfInputProcessorProfiles **));
  STUB(ReleaseInputProcessor, (REFCLSID, DWORD));
  STUB(RegisterProfile, (REFCLSID, LANGID, REFGUID, const WCHAR *, ULONG, const WCHAR *, ULONG,
                         ULONG, HKL, DWORD, BOOL, DWORD));
  STUB(UnregisterProfile, (REFCLSID, LANGID, REFGUID, DWORD));
};
struct SourceFixture {
  DiagnosticProvider provider;
  SourceProfiles native;
  Manager manager;
  diagnostics::LifecycleJournal journal;
  source_proof::ProbeTestAccess::Dependencies dependencies;
  source_proof::Probe probe{nullptr, manager, GetCurrentThreadId()};
  std::vector<std::string> calls;
  SourceFixture() {
    provider.snapshot.base.activation = 3;
    provider.snapshot.base.active = 1;
    provider.snapshot.base.mode = KR_EN;
    manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&provider);
    dependencies.languages = &native;
    dependencies.profiles = &native;
    source_proof::ProbeTestAccess::Configure(probe, dependencies);
    provider.hook = [&](const char *name) { calls.emplace_back(name); };
    native.hook = [&](const char *name) {
      calls.emplace_back(name);
      if (std::string(name) == "profile.activate")
        Complete();
    };
  }
  ~SourceFixture() {
    provider.hook = nullptr;
    native.hook = nullptr;
    probe.Close();
    manager.diagnostic_sink.Reset();
    Require(provider.references == 1 && native.references == 1,
            "all source capabilities release before controlled apartment teardown");
  }
  void Record(diagnostics::LifecycleKind kind, uint32_t subject = 1,
              diagnostics::LifecycleCause cause = diagnostics::LifecycleCause::None,
              diagnostics::Unresolved unresolved = diagnostics::Unresolved::None) {
    journal.Record(kind, subject, cause, unresolved);
    provider.snapshot.lifecycle = journal.Copy(provider.snapshot.lifecycle.owned_present != 0,
                                               provider.snapshot.lifecycle.owned_serial);
  }
  void Complete(bool profile_event = true) {
    using namespace diagnostics;
    journal.Record(LifecycleKind::ExactTermination, 1);
    if (profile_event)
      journal.Record(LifecycleKind::LifecycleInvalidation, 0, LifecycleCause::ProfileChanged);
    provider.snapshot.lifecycle = journal.Copy(false, 0);
    provider.snapshot.base.mode = KR_RU;
    native.active = SourceProfiles::Profile(1);
  }
  void Bind() {
    Require(probe.Tick(0).stage == source_proof::Stage::Waiting && provider.reads == 1,
            "fresh empty service binds through actual provider discovery");
  }
  void Word(uint32_t length = 6) {
    if (!provider.snapshot.lifecycle.allocated) {
      const auto serial = journal.Acquire();
      provider.snapshot.lifecycle = journal.Copy(true, serial);
    }
    dependencies.word.prefix = length;
    diagnostics::Count(provider.snapshot.base, diagnostics::Counter::HandleEntered);
  }
  void Arm() {
    Bind();
    Word();
    Require(probe.Tick(100).stage == source_proof::Stage::Countdown,
            "only new live complete public word arms the fixed countdown");
  }
};
void SourceProbeSchedules() {
  using namespace source_proof;
  using namespace diagnostics;
  for (bool redundant : {false, true}) {
    SourceFixture f;
    Require(f.native.references == 1 && f.provider.reads == 0 && f.manager.provider_queries == 0,
            "probe construction is passive and makes no COM call");
    f.Arm();
    if (redundant)
      f.native.hook = [&](const char *name) {
        f.calls.emplace_back(name);
        if (std::string(name) == "language.change")
          f.Complete();
      };
    Require(f.probe.Tick(2099).stage == source_proof::Stage::Countdown && !f.native.language_calls,
            "countdown cannot dispatch early");
    f.calls.clear();
    const auto result = f.probe.Tick(2100);
    Require(result.stage == source_proof::Stage::Verified && result.language_calls == 1 &&
                result.activation_calls == (redundant ? 0u : 1u) && result.serial == 1 &&
                result.completion == Completion::ExactTermination &&
                result.phase == (redundant ? Phase::LanguageSelection : Phase::ProfileActivation) &&
                result.order == Order::BeforeProfile,
            "one-call and conditional two-call source results retain exact phase and order");
    for (size_t i = 0; i != f.calls.size(); ++i)
      if (f.calls[i] == "language.change" || f.calls[i] == "profile.activate")
        Require(i && f.calls[i - 1] == "copy", "actual Copy is adjacent to every source mutation");
    f.probe.Close();
    Require(f.probe.Tick(5000) == result,
            "completed native historical result stays frozen after Close");
    wchar_t encoded[source_proof::status_capacity]{};
    Status decoded;
    Require(Format(result, encoded) && Parse(encoded, decoded) && decoded == result,
            "bounded native result round trips without document content");
    const std::wstring valid = encoded;
    for (const auto &bad :
         {valid + L"; extra=1", L"unknown" + valid, valid.substr(0, valid.size() - 1)})
      Require(!Parse(bad, decoded), "source parser rejects extra, wrong, and truncated fields");
    for (int scenario = 0; scenario != 10; ++scenario) {
      auto bad = result;
      if (scenario == 0)
        bad.language_calls = 0;
      if (scenario == 1)
        bad.activation_calls = 2;
      if (scenario == 2)
        bad.serial = 0;
      if (scenario == 3)
        bad.failure = Failure::Closed;
      if (scenario == 4)
        bad.terminal = bad.before;
      if (scenario == 5)
        bad.after = bad.before + 17;
      if (scenario == 6)
        bad.completion = Completion::None;
      if (scenario == 7)
        bad.order = Order::NoProfileEvent;
      if (scenario == 8)
        bad.native_result = static_cast<uint32_t>(E_FAIL);
      if (scenario == 9)
        bad.profile = bad.terminal;
      Require(!Format(bad, encoded), "inconsistent native success cannot be published");
    }
    PublicationV2 first;
    first.publication = 1;
    first.source = 1;
    first.availability = Availability::Available;
    first.continuity = Continuity::Same;
    first.snapshot = f.provider.snapshot;
    auto second = first;
    ++second.publication;
    Require(Matches(result, first, second),
            "fresh active RU publication corroborates frozen native result");
    second.snapshot.base.mode = KR_EN;
    Require(!Matches(result, first, second), "historical native success cannot certify current EN");
    second = first;
    Require(!Matches(result, first, second), "stale publication cannot certify a source oracle");
  }
  for (int scenario = 0; scenario != 12; ++scenario) {
    SourceFixture f;
    f.Bind();
    if (scenario == 0) {
      f.dependencies.word.failure = Failure::FocusChanged;
      f.Record(LifecycleKind::LifecycleInvalidation, 0, LifecycleCause::DocumentFocus);
      Require(f.probe.Tick(20).stage == source_proof::Stage::Waiting,
              "pre-word setup focus is allowed at zero allocation");
      f.dependencies.word.failure = Failure::None;
      f.Record(LifecycleKind::LifecycleInvalidation, 0, LifecycleCause::DocumentFocus);
      Require(f.probe.Tick(40).stage == source_proof::Stage::Waiting,
              "empty setup baseline refreshes on the same endpoint");
      f.Word();
      Require(f.probe.Tick(100).stage == source_proof::Stage::Countdown &&
                  f.probe.Tick(2100).stage == source_proof::Stage::Verified,
              "setup focus does not disqualify the first subsequently acquired word");
      continue;
    }
    f.Word(scenario == 1 ? 1 : 6);
    if (scenario == 1) {
      Require(f.probe.Tick(100).stage == source_proof::Stage::Waiting &&
                  f.probe.Tick(5000).stage == source_proof::Stage::Waiting &&
                  !f.native.language_calls,
              "first letter and partial word never start a countdown");
      f.Word();
    }
    if (scenario == 2)
      f.dependencies.word.failure = Failure::WordChanged;
    if (scenario == 3)
      f.dependencies.word.failure = Failure::SetupUnavailable;
    if (scenario == 4)
      f.dependencies.word.failure = Failure::FocusChanged;
    if (scenario == 5) {
      f.Complete();
      f.dependencies.word.prefix = 0;
    }
    if (scenario == 6)
      f.Record(LifecycleKind::LifecycleInvalidation, 0, LifecycleCause::DocumentFocus);
    if (scenario == 7)
      ++f.provider.snapshot.base.activation;
    if (scenario == 8)
      Count(f.provider.snapshot.base, Counter::Unknown);
    if (scenario == 9)
      f.provider.snapshot.base.saturated = 1;
    if (scenario == 10)
      f.provider.result = E_PENDING;
    if (scenario == 11)
      f.provider.snapshot.base.mode = KR_RU;
    const auto status = f.probe.Tick(100);
    if (scenario == 1) {
      Require(status.stage == source_proof::Stage::Countdown,
              "full public word arms after proper prefixes");
    } else {
      Require(status.stage == source_proof::Stage::Failed && !f.native.language_calls &&
                  f.probe.Tick(3000) == status,
              "bad readiness or ended mark cannot rearm or select a source");
    }
  }
  for (int scenario = 0; scenario != 14; ++scenario) {
    SourceFixture f;
    f.Arm();
    DiagnosticProvider replacement;
    if (scenario < 5)
      Count(f.provider.snapshot.base, static_cast<Counter>(scenario));
    if (scenario == 5)
      f.Record(LifecycleKind::LifecycleInvalidation, 0, LifecycleCause::DocumentFocus);
    if (scenario == 6)
      f.native.enabled = FALSE;
    if (scenario == 7)
      f.native.target_result = S_FALSE;
    if (scenario == 8)
      f.provider.result = E_PENDING;
    if (scenario == 9)
      f.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&replacement);
    if (scenario == 10)
      f.dependencies.word.prefix = 5;
    if (scenario == 11)
      f.provider.hook = [&](const char *name) {
        if (std::string(name) == "copy")
          f.Complete();
      };
    if (scenario == 12)
      f.native.hook = [&](const char *name) {
        if (std::string(name) == "enabled.read")
          f.Complete();
      };
    auto status = f.probe.Tick(scenario == 13 ? 2351 : 2100);
    Require(status.stage == source_proof::Stage::Failed && !f.native.language_calls &&
                !f.native.activation_calls,
            "armed interference, stale capture, missing target, or late deadline refuses before "
            "mutation");
    f.provider.hook = nullptr;
    f.manager.diagnostic_sink.Reset();
  }
  for (int scenario = 0; scenario != 15; ++scenario) {
    SourceFixture f;
    f.Arm();
    DiagnosticProvider replacement;
    f.native.hook = [&](const char *name) {
      if (std::string(name) == "language.change") {
        if (scenario == 0)
          f.native.language_result = E_FAIL;
        if (scenario == 1)
          f.native.language_result = S_FALSE;
        if (scenario == 2)
          throw 1;
        if (scenario == 3)
          f.native.language = profiles[0].language;
        if (scenario == 4)
          f.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&replacement);
        if (scenario == 5)
          ++f.provider.snapshot.base.activation;
        if (scenario == 6)
          f.provider.snapshot.base.active = 0;
        if (scenario == 7)
          f.provider.snapshot.base.stopped = 1;
        if (scenario == 8)
          f.provider.result = E_PENDING;
        if (scenario == 9) {
          f.Complete();
          Count(f.provider.snapshot.base, Counter::HandleEntered);
        }
        if (scenario == 10)
          f.provider.snapshot.lifecycle = f.journal.Copy(false, 0);
        if (scenario == 11) {
          f.native.active = SourceProfiles::Profile(1);
          f.provider.snapshot.base.mode = KR_RU;
        }
        if (scenario == 12)
          f.Record(LifecycleKind::Detached);
        if (scenario == 13)
          f.Record(LifecycleKind::TerminationUnresolved, 1, LifecycleCause::None,
                   Unresolved::NullArgument);
        if (scenario == 14)
          Count(f.provider.snapshot.base, Counter::Unknown);
      }
    };
    const auto status = f.probe.Tick(2100);
    Require(status.stage == source_proof::Stage::Failed && f.native.language_calls == 1 &&
                !f.native.activation_calls && f.probe.Tick(9999) == status,
            "unsafe language cut never activates, repairs, retries, or polls failure into success");
    f.manager.diagnostic_sink.Reset();
  }
  for (int scenario = 0; scenario != 8; ++scenario) {
    SourceFixture f;
    f.Arm();
    f.native.hook = [&](const char *name) {
      if (std::string(name) != "profile.activate")
        return;
      f.Complete();
      if (scenario == 0)
        f.native.activation_result = E_FAIL;
      if (scenario == 1)
        f.native.activation_result = S_FALSE;
      if (scenario == 2)
        f.native.active = SourceProfiles::Profile(0);
      if (scenario == 3)
        f.native.active.guidProfile = GUID_NULL;
      if (scenario == 4)
        f.native.read_result = S_FALSE;
      if (scenario == 5)
        f.provider.snapshot.base.mode = KR_EN;
      if (scenario == 6)
        Count(f.provider.snapshot.base, Counter::KeyEntered);
      if (scenario == 7)
        f.dependencies.word.failure = Failure::FocusChanged;
    };
    const auto status = f.probe.Tick(2100);
    Require(status.stage == source_proof::Stage::Failed && status.language_calls == 1 &&
                status.activation_calls == 1 && f.probe.Tick(9999) == status,
            "bad activation or readback remains failed despite an exact terminal");
  }
}
void SourceReadinessSchedules() {
  using namespace source_proof;
  for (int scenario = 0; scenario != 14; ++scenario) {
    std::wstring a = scenario == 1 ? L"g" : scenario == 2 ? L"" : L"ghbdtn";
    std::wstring b;
    if (scenario == 3)
      a = L"ghbdtx";
    if (scenario == 4)
      a = L"ghbdtn ";
    if (scenario == 5)
      b = L"x";
    if (scenario == 6)
      a += L'\r';
    LONG caret = static_cast<LONG>(a.size());
    if (scenario == 7)
      --caret;
    int reads = 0, eligibility = 0;
    bool alive = true;
    const auto result = source_proof::detail::ReadPublicWord(
        [&] { return alive; },
        [&] {
          ++eligibility;
          return scenario == 8   ? Failure::SetupUnavailable
                 : scenario == 9 ? Failure::FocusChanged
                                 : Failure::None;
        },
        [&](int id, UINT message, WPARAM count, LPARAM destination) -> LRESULT {
          Require(id == 101 || id == 102, "native readiness never reads excluded 103 or 104");
          ++reads;
          const auto &text = id == 101 ? a : b;
          if (scenario == 10)
            alive = false;
          if (message == WM_GETTEXTLENGTH)
            return static_cast<LRESULT>(text.size());
          if (message == WM_GETTEXT) {
            Require(count == 8,
                    "public readiness text buffer is bounded independently of native length");
            auto *out = reinterpret_cast<wchar_t *>(destination);
            std::copy(text.begin(), text.end(), out);
            out[text.size()] = 0;
            return scenario == 11 ? static_cast<LRESULT>(text.size()) - 1
                                  : static_cast<LRESULT>(text.size());
          }
          Require(message == EM_EXGETSEL && id == 101, "only public A selection is inspected");
          *reinterpret_cast<CHARRANGE *>(destination) = {caret, scenario == 12 ? caret - 1 : caret};
          if (scenario == 13)
            alive = false;
          return 0;
        });
    Require(
        (result.failure == Failure::None) == (scenario <= 2),
        "native readiness distinguishes empty, prefix, full word, wrong word, B, and exact caret");
    if (scenario <= 2)
      Require(result.prefix == a.size(), "native readiness uses actual UTF-16 length");
    if (scenario == 8 || scenario == 9)
      Require(!reads && eligibility == 1,
              "ineligible or unfocused controls receive zero text queries");
    if (scenario == 10)
      Require(reads == 1, "cancellation after a native query prevents every subsequent read");
  }
  for (const char *boundary : {"target.read", "enabled.read", "provider"}) {
    SourceFixture f;
    f.Arm();
    const auto counters = f.provider.snapshot.base.counts;
    auto change = [&] { f.dependencies.word.failure = Failure::WordChanged; };
    f.native.hook = [&](const char *name) {
      if (std::string(name) == boundary)
        change();
    };
    f.manager.provider_hook = [&] {
      if (std::string(boundary) == "provider")
        change();
    };
    const auto result = f.probe.Tick(2100);
    Require(result.failure == Failure::WordChanged && !f.native.language_calls &&
                f.provider.snapshot.base.counts == counters,
            "word or caret changed during preparation is reread after COM without relying on key "
            "counters");
  }
  for (bool last : {false, true}) {
    SourceFixture f;
    f.Arm();
    f.manager.provider_hook = [&] {
      if (f.native.language_calls && (last ? f.native.activation_calls != 0 : true))
        f.dependencies.word.failure = Failure::WordChanged;
    };
    const auto result = f.probe.Tick(2100);
    Require(result.failure == Failure::WordChanged && f.native.language_calls == 1 &&
                f.native.activation_calls == (last ? 1 : 0),
            "middle and final readiness follow provider queries before their retained copy");
  }
  for (int scenario = 0; scenario != 5; ++scenario) {
    SourceFixture f;
    if (scenario == 0)
      f.native.read_result = S_FALSE;
    if (scenario == 1)
      f.native.active = SourceProfiles::Profile(1);
    if (scenario == 2)
      f.manager.provider_result = TF_E_NOPROVIDER;
    if (scenario == 3)
      f.dependencies.word.failure = Failure::FocusChanged;
    if (scenario == 4)
      f.dependencies.acquired = E_NOINTERFACE;
    const auto result = f.probe.Tick(0);
    Require(result.stage ==
                    (scenario == 4 ? source_proof::Stage::Failed : source_proof::Stage::Waiting) &&
                !f.native.language_calls,
            "incomplete initial setup waits and missing native interfaces fail without selection");
  }
}
void SourceReviewRegressionSchedules() {
  using namespace source_proof;
  int failures = 0;
  auto check = [&](bool passed, const char *label) {
    printf("REGRESSION %s %s\n", passed ? "PASS" : "FAIL", label);
    failures += !passed;
  };
  for (const char *boundary : {"target.read", "provider", "read", "copy"}) {
    SourceFixture f;
    f.Arm();
    uint64_t elapsed = 0;
    int clocks = 0;
    f.dependencies.clock = [&](uint64_t entry) {
      ++clocks;
      return entry + elapsed;
    };
    auto delay = [&](const char *name) {
      f.calls.emplace_back(name);
      if (std::string(name) == boundary)
        elapsed = 251;
      if (std::string(name) == "profile.activate")
        f.Complete();
    };
    f.native.hook = delay;
    f.provider.hook = delay;
    f.manager.provider_hook = [&] { delay("provider"); };
    f.dependencies.read = [&] { delay("read"); };
    const auto result = f.probe.Tick(2100);
    printf("deadline %s latest=2351 clocks=%d language_calls=%d activation_calls=%d\n", boundary,
           clocks, f.native.language_calls, f.native.activation_calls);
    check(result.failure == Failure::Late && clocks == 1 && !f.native.language_calls &&
              !f.native.activation_calls && f.probe.Tick(4000) == result,
          "delayed preparation or Copy must expire before first source mutation");
  }
  for (int scenario = 0; scenario != 4; ++scenario) {
    SourceFixture f;
    f.Arm();
    int clocks = 0;
    f.dependencies.clock = [&](uint64_t entry) {
      ++clocks;
      if (scenario == 1)
        return entry - 1;
      if (scenario == 2)
        f.probe.Close();
      if (scenario == 3)
        Require(f.probe.Tick(entry).stage == Stage::Running && !f.native.language_calls,
                "clock reentry cannot dispatch another source operation");
      return scenario == 0 ? uint64_t{2350} : entry;
    };
    const auto result = f.probe.Tick(2100);
    printf("clock scenario=%d clocks=%d language_calls=%d activation_calls=%d\n", scenario, clocks,
           f.native.language_calls, f.native.activation_calls);
    if (scenario == 1 || scenario == 2)
      check(result.failure == (scenario == 1 ? Failure::Late : Failure::Closed) && clocks == 1 &&
                !f.native.language_calls && !f.native.activation_calls,
            "backward clock or Close during clock must refuse before source mutation");
    else
      check(result.stage == Stage::Verified && clocks == 1 && f.native.language_calls == 1 &&
                f.native.activation_calls == 1,
            "last allowed millisecond and nested clock Tick preserve one-shot dispatch");
  }
  for (const char *change : {"password", "read-only", "disabled", "focus-lost"}) {
    SourceFixture f;
    bool changed = false;
    int text_queries = 0, selection_queries = 0;
    const auto result = detail::ReadPublicWord(
        [] { return true; },
        [&] {
          return !changed                              ? Failure::None
                 : std::string(change) == "focus-lost" ? Failure::FocusChanged
                                                       : Failure::SetupUnavailable;
        },
        [&](int id, UINT message, WPARAM count, LPARAM destination) -> LRESULT {
          Require(id == 101, "eligibility loss on A must stop before B inspection");
          if (message == WM_GETTEXTLENGTH) {
            ++text_queries;
            return 6;
          }
          if (message == WM_GETTEXT) {
            ++text_queries;
            Require(count == 8, "regression uses bounded public word read");
            wcscpy_s(reinterpret_cast<wchar_t *>(destination), count, L"ghbdtn");
            changed = true;
            return 6;
          }
          Require(message == EM_EXGETSEL, "only selection query can follow public text read");
          ++selection_queries;
          *reinterpret_cast<CHARRANGE *>(destination) = {6, 6};
          return 0;
        });
    printf("eligibility %s text_queries=%d selection_queries=%d source_calls=%d\n", change,
           text_queries, selection_queries, f.native.language_calls + f.native.activation_calls);
    check(result.failure == (std::string(change) == "focus-lost" ? Failure::FocusChanged
                                                                 : Failure::SetupUnavailable) &&
              text_queries == 2 && !selection_queries && !f.native.language_calls &&
              !f.native.activation_calls,
          "eligibility changed by text read must prevent selection inspection");
  }
  {
    const HWND ordinary_b = GetDlgItem(controlled_root, 102);
    Require(ordinary_b != nullptr, "hidden fixture has ordinary B");
    SetWindowLongPtrW(ordinary_b, GWLP_ID, 202);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(ready != nullptr, "hidden worker fixture handshake allocated");
    HWND worker_root = nullptr, worker_b = nullptr;
    DWORD worker_thread = 0;
    std::thread worker([&] {
      worker_thread = GetCurrentThreadId();
      worker_root = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 10, 10, nullptr,
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
      worker_b =
          CreateWindowExW(WS_EX_NOPARENTNOTIFY, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE, 0, 0,
                          10, 10, worker_root, reinterpret_cast<HMENU>(static_cast<INT_PTR>(102)),
                          GetModuleHandleW(nullptr), nullptr);
      MSG message{};
      PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
      SetEvent(ready);
      while (GetMessageW(&message, nullptr, 0, 0) > 0)
        DispatchMessageW(&message);
      DestroyWindow(worker_root);
    });
    Require(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0 && worker_root && worker_b,
            "hidden worker RichEdit created without desktop focus");
    Require(SetParent(worker_b, controlled_root) == worker_root &&
                GetWindowThreadProcessId(worker_b, nullptr) == worker_thread &&
                worker_thread != GetCurrentThreadId() &&
                GetDlgItem(controlled_root, 102) == worker_b,
            "hidden B has exact parent and ID but a different owning thread");
    SourceFixture f;
    int text_queries = 0;
    const auto result = detail::ReadPublicWord(
        [] { return true; },
        [&] { return detail::PublicControls(controlled_root, GetCurrentThreadId()); },
        [&](int, UINT, WPARAM, LPARAM) -> LRESULT {
          ++text_queries;
          return 0;
        });
    printf("cross-thread B failure=%u expected=%u text_queries=%d source_calls=%d\n",
           static_cast<unsigned>(result.failure), static_cast<unsigned>(Failure::SetupUnavailable),
           text_queries, f.native.language_calls + f.native.activation_calls);
    check(result.failure == Failure::SetupUnavailable && !text_queries &&
              !f.native.language_calls && !f.native.activation_calls,
          "cross-thread ordinary B is unavailable before text or selection inspection");
    Require(SetParent(worker_b, worker_root) == controlled_root,
            "hidden worker B returns to its owning thread before teardown");
    Require(PostThreadMessageW(worker_thread, WM_QUIT, 0, 0) != FALSE,
            "hidden worker teardown requested without source or desktop action");
    worker.join();
    CloseHandle(ready);
    SetWindowLongPtrW(ordinary_b, GWLP_ID, 102);
  }
  Require(!failures, "source review regression schedules have zero failing invariants");
}
void SourceHistorySchedules() {
  using namespace source_proof;
  using namespace diagnostics;
  for (int scenario = 0; scenario != 17; ++scenario) {
    SourceFixture f;
    f.Arm();
    const auto before = f.provider.snapshot;
    auto middle = before;
    if (scenario == 0 || scenario == 1) {
      for (int i = 0; i != 8; ++i)
        f.Record(LifecycleKind::DifferentTermination);
      middle = f.provider.snapshot;
      for (int i = 0; i != (scenario == 0 ? 7 : 8); ++i)
        f.Record(LifecycleKind::DifferentTermination);
      f.Complete(false);
    } else if (scenario == 2) {
      f.Record(LifecycleKind::EndRequested);
      f.Record(LifecycleKind::FinishVerified);
      f.provider.snapshot.lifecycle = f.journal.Copy(false, 0);
      f.provider.snapshot.base.mode = KR_RU;
    } else if (scenario == 3) {
      f.Record(LifecycleKind::FinishVerified);
      f.provider.snapshot.lifecycle = f.journal.Copy(false, 0);
      f.provider.snapshot.base.mode = KR_RU;
    } else {
      f.Complete();
      if (scenario == 4)
        f.provider.snapshot.lifecycle.events[0].kind = LifecycleKind::EndRequested;
      if (scenario == 5)
        Count(f.provider.snapshot.base, Counter::Unknown);
      if (scenario == 6)
        f.provider.snapshot.lifecycle.saturated = 1;
      if (scenario == 7)
        f.Record(LifecycleKind::Detached);
      if (scenario == 8)
        f.Record(LifecycleKind::TerminationUnresolved, 1, LifecycleCause::None,
                 Unresolved::IdentityFailure);
      if (scenario == 9)
        f.Record(LifecycleKind::LifecycleInvalidation, 0, LifecycleCause::Deactivate);
      if (scenario == 10) {
        f.journal.Acquire();
        f.provider.snapshot.lifecycle = f.journal.Copy(false, 0);
      }
      if (scenario >= 11 && scenario <= 15)
        Count(f.provider.snapshot.base, static_cast<Counter>(scenario - 11));
      if (scenario == 16)
        --f.provider.snapshot.base.counts[static_cast<size_t>(Counter::HandleEntered)];
    }
    Status result;
    const auto failure =
        source_proof::detail::Evaluate(before, middle, f.provider.snapshot, result);
    Require((failure == Failure::None) == (scenario == 0 || scenario == 2),
            "complete sixteen-event interval and requested finish pass; unsafe histories cannot");
    if (scenario == 1)
      Require(failure == Failure::HistoryLost,
              "combined seventeen-event interval fails even when both smaller cuts fit");
    if (scenario == 2)
      Require(result.completion == Completion::VerifiedFinish,
              "requested verified finish is classified separately from exact termination");
  }
}
void SourceReentrySchedules() {
  using namespace source_proof;
  for (bool close : {false, true}) {
    for (const char *boundary :
         {"acquire", "read", "provider", "identity", "function", "copy", "release", "language.read",
          "profile.read", "target.read", "enabled.read", "language.change", "profile.activate",
          "native.release"}) {
      SourceFixture f;
      f.Arm();
      bool injected = false;
      auto inject = [&](const char *name) {
        if (injected || std::string(name) != boundary)
          return;
        injected = true;
        if (close)
          f.probe.Close();
        else if (std::string(name) == "release" || std::string(name) == "native.release")
          Require(f.probe.Tick(2100).stage == source_proof::Stage::Failed,
                  "Release observes a terminal failure before references unwind");
        else
          Require(f.probe.Tick(2100).stage == source_proof::Stage::Running,
                  "nested timer sees an in-flight batch and cannot dispatch another operation");
      };
      f.dependencies.read = [&] { inject("read"); };
      f.manager.provider_hook = [&] { inject("provider"); };
      f.provider.hook = inject;
      f.native.hook = [&](const char *name) {
        inject(name);
        if (std::string(name) == "profile.activate")
          f.Complete();
      };
      if (std::string(boundary) == "acquire") {
        f.probe.Close();
        continue;
      }
      if (std::string(boundary) == "function")
        continue;
      if (std::string(boundary) == "release" || std::string(boundary) == "native.release") {
        f.native.language_result = E_FAIL;
        auto status = f.probe.Tick(2100);
        Require(injected && status.stage == source_proof::Stage::Failed,
                "release reentry cannot overwrite frozen failure");
      } else {
        const auto status = f.probe.Tick(2100);
        Require(injected && status.stage == (close ? source_proof::Stage::Failed
                                                   : source_proof::Stage::Verified),
                "nested Close is sticky and nested Tick cannot duplicate source calls");
        if (close)
          Require(status.failure == Failure::Closed,
                  "outer completion cannot overwrite cancellation");
      }
      f.provider.hook = nullptr;
      f.native.hook = nullptr;
    }
  }
  for (const char *boundary :
       {"acquire", "read", "provider", "identity", "function", "copy", "release"}) {
    SourceFixture f;
    bool injected = false;
    auto inject = [&](const char *name) {
      if (!injected && std::string(name) == boundary) {
        injected = true;
        f.probe.Close();
      }
    };
    f.dependencies.acquire = [&] { inject("acquire"); };
    f.dependencies.read = [&] { inject("read"); };
    f.manager.provider_hook = [&] { inject("provider"); };
    f.provider.hook = inject;
    const auto status = f.probe.Tick(0);
    Require(
        injected && status.stage == source_proof::Stage::Failed &&
            status.failure == Failure::Closed && !f.native.language_calls &&
            f.probe.Tick(3000) == status,
        "preparation and readiness are guarded before the first external call including Release");
    f.provider.hook = nullptr;
  }
}
void SourceServiceSchedules() {
  using namespace source_proof;
  for (bool input : {false, true}) {
    const auto objects = object_count;
    {
      ProfileManager service_profiles;
      Harness h(&service_profiles, ProofLayouts);
      SourceProfiles native;
      ProbeTestAccess::Dependencies deps;
      deps.languages = &native;
      deps.profiles = &native;
      Probe probe(nullptr, h.manager, GetCurrentThreadId());
      ProbeTestAccess::Configure(probe, deps);
      Require(probe.Tick(0).stage == source_proof::Stage::Waiting,
              "actual service starts from empty allocation-zero evidence");
      h.Type(L"ghbdtn");
      deps.word.prefix = 6;
      Require(probe.Tick(100).stage == source_proof::Stage::Countdown,
              "actual service owned public word arms probe");
      const auto writes = h.a.writes, reads = h.a.reads, ends = h.a.ends, starts = h.a.starts;
      native.hook = [&](const char *name) {
        if (std::string(name) != "language.change")
          return;
        if (input) {
          const kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
          h.service->HandleEvent(&h.a, finish);
        } else {
          h.a.active_composition = false;
          h.service->OnCompositionTerminated(123, h.a.composition.Get());
        }
        native.active = SourceProfiles::Profile(1);
        h.service->OnActivated(TF_PROFILETYPE_INPUTPROCESSOR, profiles[1].language, service_id,
                               GUID_TFCAT_TIP_KEYBOARD, profiles[1].id, nullptr,
                               TF_IPSINK_FLAG_ACTIVE);
      };
      const auto status = probe.Tick(2100);
      Require(input ? status.failure == Failure::InputInterference
                    : status.stage == source_proof::Stage::Verified,
              "actual service exact callback passes while a real reentrant HandleEvent finish is "
              "interference");
      Require(h.a.writes == writes + (input ? 1 : 0) && h.a.starts == starts &&
                  h.a.ends == ends + (input ? 1 : 0) && (input || h.a.reads == reads) &&
                  native.language_calls == 1 && !native.activation_calls,
              "only the interfering input finish writes once and ends its composition");
      h.Oracle(L"ghbdtn", 13);
      probe.Close();
      Require(native.references == 1,
              "source COM references drain while the actual service apartment lives");
    }
    Require(object_count == objects,
            "actual service object accounting drains after source experiment");
  }
}
struct ShutdownHistory {
  diagnostics::LifecycleJournal journal;
  diagnostics::SnapshotV2 before;
  ShutdownHistory() {
    const auto old = journal.Acquire();
    journal.Record(diagnostics::LifecycleKind::ExactTermination, old);
    const auto serial = journal.Acquire();
    before.base.activation = 3;
    before.base.active = 1;
    before.base.mode = KR_EN;
    diagnostics::Count(before.base, diagnostics::Counter::Verified);
    diagnostics::Refuse(before.base, diagnostics::Stage::Request, diagnostics::Cause::Native,
                        E_ACCESSDENIED);
    before.lifecycle = journal.Copy(true, serial);
  }
  diagnostics::SnapshotV2 After() const {
    auto after = before;
    after.base.active = 0;
    after.lifecycle = journal.Copy(false, 0);
    return after;
  }
  void Record(diagnostics::LifecycleKind kind, uint32_t subject = 2,
              diagnostics::LifecycleCause cause = diagnostics::LifecycleCause::None,
              diagnostics::Unresolved unresolved = diagnostics::Unresolved::None) {
    journal.Record(kind, subject, cause, unresolved);
  }
  void Deactivate() {
    Record(diagnostics::LifecycleKind::LifecycleInvalidation, 0,
           diagnostics::LifecycleCause::Deactivate);
  }
};
void ShutdownPredicateSchedules() {
  using namespace diagnostics;
  using shutdown::Verdict;
  Require(shutdown::ExitCode(Verdict::Verified) == 0,
          "only verified shutdown has success exit code");
  std::array<bool, 32> exit_codes{};
  for (const auto verdict :
       {Verdict::NotCaptured, Verdict::Missing, Verdict::Busy, Verdict::InvalidEvidence,
        Verdict::ProviderChanged, Verdict::ActivationChanged, Verdict::NoOwnedMark,
        Verdict::HistoryLost, Verdict::UnknownEffect, Verdict::NoTerminalEvent, Verdict::Detached,
        Verdict::DeactivationFailed}) {
    const auto code = shutdown::ExitCode(verdict);
    Require(
        code > 2 && static_cast<size_t>(code) < exit_codes.size() && !exit_codes[code],
        "each rejection has a distinct bounded exit code separate from host and argument errors");
    exit_codes[code] = true;
  }
  Verdict (*volatile evaluate)(const SnapshotV2 &, const SnapshotV2 &) noexcept =
      shutdown::detail::Evaluate;
  ShutdownHistory history;
  history.Record(LifecycleKind::ExactTermination);
  history.Deactivate();
  const auto before = history.before;
  const auto after = history.After();
  Require(evaluate(before, after) == Verdict::Verified,
          "new exact terminal before deactivation passes");
  for (int scenario = 0; scenario != 25; ++scenario) {
    auto a = before, b = after;
    if (scenario == 0)
      a.base.active = 0;
    if (scenario == 1)
      a.base.stopped = 1;
    if (scenario == 2)
      a.lifecycle.owned_present = a.lifecycle.owned_serial = 0;
    if (scenario == 3)
      a.lifecycle.owned_serial = 0;
    if (scenario == 4)
      b.base.active = 1;
    if (scenario == 5) {
      b.lifecycle.owned_present = 1;
      b.lifecycle.owned_serial = 2;
    }
    if (scenario == 6)
      ++b.base.activation;
    if (scenario == 7)
      b.base.counts[static_cast<size_t>(Counter::Unknown)] = 1;
    if (scenario == 8)
      a.base.counts[static_cast<size_t>(Counter::Unknown)] = 1;
    if (scenario == 9)
      b.base.counts[static_cast<size_t>(Counter::Verified)] = 0;
    if (scenario == 10) {
      b.base.refusals = {};
      b.base.first = b.base.latest = {};
    }
    if (scenario == 11)
      ++b.lifecycle.allocated;
    if (scenario == 12)
      b.lifecycle.events[3].kind = LifecycleKind::Acquired;
    if (scenario == 13)
      b.lifecycle.events[3].kind = LifecycleKind::Detached;
    if (scenario == 14) {
      b.lifecycle.events[3].kind = LifecycleKind::TerminationUnresolved;
      b.lifecycle.events[3].unresolved = Unresolved::IdentityFailure;
    }
    if (scenario == 15)
      b.lifecycle.events[3].subject = 1;
    if (scenario == 16)
      b.lifecycle.events[3].kind = LifecycleKind::EndRequested;
    if (scenario == 17)
      b.lifecycle.events[3].kind = LifecycleKind::FinishVerified;
    if (scenario == 18)
      b.lifecycle.events[3].kind = LifecycleKind::DifferentTermination;
    if (scenario == 19) {
      b.lifecycle.events[3].kind = LifecycleKind::TerminationWithoutOwned;
      b.lifecycle.events[3].subject = 0;
    }
    if (scenario == 20)
      b.lifecycle.events[4].cause = LifecycleCause::DocumentUninit;
    if (scenario == 21)
      b.lifecycle.events[0].kind = LifecycleKind::EndRequested;
    if (scenario == 22)
      b.lifecycle.saturated = 1;
    if (scenario == 23)
      a.base.saturated = 1;
    if (scenario == 24)
      --b.lifecycle.events[4].sequence;
    Require(evaluate(a, b) != Verdict::Verified,
            "malformed unsafe stale or contradictory shutdown evidence cannot pass");
  }
  for (auto kind : {LifecycleKind::ExactTermination, LifecycleKind::FinishVerified}) {
    ShutdownHistory late;
    late.Deactivate();
    late.Record(LifecycleKind::EndRequested);
    late.Record(kind);
    Require(evaluate(late.before, late.After()) != Verdict::Verified,
            "terminal after first deactivation is too late");
  }
  {
    ShutdownHistory finish;
    finish.Record(LifecycleKind::EndRequested);
    finish.Record(LifecycleKind::FinishVerified);
    finish.Deactivate();
    auto endpoint = finish.After();
    endpoint.base.stopped = 1;
    Require(evaluate(finish.before, endpoint) == Verdict::Verified,
            "new end request and verified finish pass with a stopped inactive endpoint");
    finish.Record(LifecycleKind::Detached);
    Require(evaluate(finish.before, finish.After()) != Verdict::Verified,
            "even detach after a proven terminal contradicts this bounded assertion");
  }
  for (auto kind : {LifecycleKind::DifferentTermination, LifecycleKind::TerminationWithoutOwned}) {
    ShutdownHistory unrelated;
    unrelated.Record(kind, kind == LifecycleKind::TerminationWithoutOwned ? 0 : 2);
    unrelated.Record(LifecycleKind::ExactTermination);
    unrelated.Deactivate();
    Require(evaluate(unrelated.before, unrelated.After()) == Verdict::Verified,
            "unrelated termination may precede a genuine exact terminal");
  }
  {
    ShutdownHistory stale;
    stale.Record(LifecycleKind::EndRequested);
    stale.before.lifecycle = stale.journal.Copy(true, 2);
    stale.Record(LifecycleKind::FinishVerified);
    stale.Deactivate();
    Require(evaluate(stale.before, stale.After()) != Verdict::Verified,
            "pre-capture end request cannot support a new verified finish");
  }
  {
    ShutdownHistory stale;
    stale.Record(LifecycleKind::ExactTermination);
    stale.before.lifecycle = stale.journal.Copy(true, 2);
    stale.Deactivate();
    Require(evaluate(stale.before, stale.After()) != Verdict::Verified,
            "stored pre-capture exact terminal cannot establish a fresh terminal");
    auto backwards = stale.After();
    backwards.lifecycle = before.lifecycle;
    backwards.lifecycle.owned_present = backwards.lifecycle.owned_serial = 0;
    Require(evaluate(stale.before, backwards) != Verdict::Verified,
            "backwards lifecycle sequence cannot establish an interval");
  }
  for (size_t count : {lifecycle_capacity, lifecycle_capacity + 1}) {
    ShutdownHistory bounded;
    for (size_t i = 0; i != lifecycle_capacity; ++i)
      bounded.Record(LifecycleKind::DifferentTermination);
    bounded.before.lifecycle = bounded.journal.Copy(true, 2);
    bounded.Record(LifecycleKind::ExactTermination);
    for (size_t i = 2; i != count; ++i)
      bounded.Record(LifecycleKind::TerminationWithoutOwned, 0);
    bounded.Deactivate();
    Require((evaluate(bounded.before, bounded.After()) == Verdict::Verified) ==
                (count == lifecycle_capacity),
            "full retained interval passes without old acquisition but one lost event fails");
  }
  for (bool serial : {false, true}) {
    auto saturated = before;
    if (serial) {
      saturated.lifecycle.owned_serial = saturated.lifecycle.allocated = UINT32_MAX;
      saturated.lifecycle.saturated = 1;
    } else {
      saturated.base.activation = UINT32_MAX;
      saturated.base.saturated = 1;
    }
    Require(evaluate(saturated, after) != Verdict::Verified,
            "saturated activation and ownership labels cannot certify close");
  }
}
void ShutdownProbeSchedules() {
  using namespace diagnostics;
  using shutdown::Verdict;
  ShutdownHistory history;
  history.Record(LifecycleKind::ExactTermination);
  history.Deactivate();
  for (int scenario = 0; scenario != 21; ++scenario) {
    DiagnosticProvider provider, other;
    Harness h;
    h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&provider);
    provider.snapshot = other.snapshot = history.before;
    shutdown::Probe probe;
    if (scenario == 1)
      probe.CaptureClose(&h.manager);
    if (scenario == 2)
      provider.legacy_only = true;
    if (scenario == 3)
      provider.foreign_function = static_cast<ITfFunctionProvider *>(&other);
    if (scenario == 4)
      h.manager.provider_result = TF_E_NOPROVIDER;
    probe.Observe(&h.manager);
    h.manager.provider_result = S_OK;
    if (scenario == 4)
      probe.Observe(&h.manager);
    if (scenario == 5)
      h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&other);
    if (scenario == 6)
      ++provider.snapshot.base.activation;
    if (scenario == 7)
      provider.result = E_PENDING;
    if (scenario == 8)
      provider.result = RPC_E_WRONG_THREAD;
    if (scenario == 9)
      provider.throwing = true;
    if (scenario == 10)
      provider.snapshot.lifecycle.owned_present = provider.snapshot.lifecycle.owned_serial = 0;
    if (scenario == 11)
      h.manager.provider_result = TF_E_NOPROVIDER;
    if (scenario == 12) {
      h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&other);
      probe.Observe(&h.manager);
      h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&provider);
    }
    if (scenario == 13) {
      ++provider.snapshot.base.activation;
      probe.Observe(&h.manager);
      --provider.snapshot.base.activation;
    }
    if (scenario == 14) {
      provider.result = E_PENDING;
      probe.Observe(&h.manager);
      provider.result = S_OK;
    }
    if (scenario != 15)
      probe.CaptureClose(&h.manager);
    if (scenario == 16)
      probe.CaptureClose(&h.manager);
    const auto queries = h.manager.provider_queries;
    h.manager.provider_result = E_FAIL;
    provider.snapshot = history.After();
    provider.throwing = false;
    provider.result = scenario == 17 ? E_PENDING : scenario == 18 ? RPC_E_WRONG_THREAD : S_OK;
    if (scenario == 19)
      provider.snapshot.base.active = 1;
    const auto verdict = probe.Finish(scenario == 20 ? E_FAIL : S_OK);
    Require((verdict == Verdict::Verified) == (scenario == 0 || scenario == 4),
            "probe requires a fresh same-provider owned capture and a usable retained endpoint");
    const auto reads = provider.reads;
    Require(probe.Finish(S_OK) == verdict && provider.reads == reads &&
                h.manager.provider_queries == queries,
            "finish caches its result and never rediscovers the removed provider");
    probe.Observe(&h.manager);
    probe.CaptureClose(&h.manager);
    Require(probe.Finish(S_OK) == verdict,
            "completed verdict cannot rearm through observation or close");
    h.manager.diagnostic_sink.Reset();
    Require(provider.references == 1 && other.references == 1,
            "every success and rejected path releases retained COM references at Finish");
  }
  for (const char *boundary :
       {"provider", "identity", "interface", "function", "copy", "addref", "release"}) {
    for (int phase = 0; phase != 4; ++phase) {
      DiagnosticProvider provider;
      Harness h;
      h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&provider);
      provider.snapshot = history.before;
      shutdown::Probe probe;
      if (phase != 0)
        probe.Observe(&h.manager);
      if (phase == 3)
        probe.CaptureClose(&h.manager);
      bool nested = false;
      auto reenter = [&](const char *name) {
        if (!nested && std::string(name) == boundary) {
          nested = true;
          probe.CaptureClose(&h.manager);
          probe.Observe(&h.manager);
        }
      };
      provider.hook = reenter;
      h.manager.provider_hook = [&] { reenter("provider"); };
      if (phase == 0 || phase == 1)
        probe.Observe(&h.manager);
      if (phase == 2)
        probe.CaptureClose(&h.manager);
      if (phase == 3)
        provider.snapshot = history.After();
      const auto verdict = probe.Finish(S_OK);
      if (nested)
        Require(verdict != Verdict::Verified,
                "nested close at any COM phase remains rejected after the outer call returns");
      provider.hook = nullptr;
      h.manager.provider_hook = nullptr;
    }
  }
  for (const auto &boundary :
       {std::pair{"identity", 2}, std::pair{"interface", 2}, std::pair{"interface", 3}}) {
    DiagnosticProvider provider;
    Harness h;
    h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&provider);
    provider.snapshot = history.before;
    shutdown::Probe probe;
    int calls = 0;
    provider.hook = [&](const char *name) {
      if (std::string(name) == boundary.first && ++calls == boundary.second)
        probe.CaptureClose(&h.manager);
    };
    probe.Observe(&h.manager);
    provider.hook = nullptr;
    probe.CaptureClose(&h.manager);
    provider.snapshot = history.After();
    Require(calls >= boundary.second && probe.Finish(S_OK) != Verdict::Verified,
            "nested close during each initial reader query cannot be overwritten by binding");
  }
  {
    DiagnosticProvider provider;
    Harness h;
    h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&provider);
    provider.snapshot = history.before;
    shutdown::Probe probe;
    probe.Observe(&h.manager);
    provider.hook = [&](const char *) { probe.Observe(&h.manager); };
    probe.CaptureClose(&h.manager);
    provider.snapshot = history.After();
    Require(probe.Finish(S_OK) == Verdict::Verified,
            "nested timer observation cannot overwrite capture or retained endpoint");
    provider.hook = nullptr;
  }
}
void ShutdownServiceSchedules() {
  using namespace diagnostics;
  using shutdown::Verdict;
  for (int scenario = 0; scenario != 4; ++scenario) {
    Harness h;
    h.Type(L"ghbdtn");
    shutdown::Probe probe;
    const auto reads = h.a.reads, writes = h.a.writes, starts = h.a.starts, ends = h.a.ends;
    probe.Observe(&h.manager);
    probe.CaptureClose(&h.manager);
    Require(h.a.reads == reads && h.a.writes == writes && h.a.starts == starts && h.a.ends == ends,
            "actual service capture performs no document inspection or editing calls");
    const auto queries = h.manager.provider_queries;
    if (scenario == 0 || scenario == 1) {
      if (scenario == 1)
        h.service->OnSetFocus(FALSE);
      h.a.active_composition = false;
      Require(h.service->OnCompositionTerminated(123, h.a.composition.Get()) == S_OK,
              "actual native exact termination callback");
    }
    if (scenario == 2) {
      kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
      Require(!h.service->HandleEvent(&h.a, finish) && !h.a.active_composition,
              "ordinary service finish passes its lifecycle trigger");
    }
    const auto deactivated = h.service->Deactivate();
    Require((probe.Finish(deactivated) == Verdict::Verified) == (scenario != 3),
            "real service exact and verified finish pass while detached-only shutdown fails");
    Require(h.manager.provider_queries == queries &&
                h.a.writes == writes + (scenario == 2 ? 1 : 0) && h.a.starts == starts &&
                h.a.ends == ends + (scenario == 2 ? 1 : 0),
            "shutdown observation adds no effects beyond the explicit ordinary finish");
    h.Oracle(L"ghbdtn", 13);
  }
  {
    Harness h;
    shutdown::Probe probe;
    probe.Observe(&h.manager);
    probe.CaptureClose(&h.manager);
    Require(probe.Finish(h.service->Deactivate()) == Verdict::NoOwnedMark,
            "empty real service close cannot certify an owned shutdown");
  }
  for (bool endpoint : {false, true}) {
    Harness h;
    h.Type(L"g");
    shutdown::Probe probe;
    probe.Observe(&h.manager);
    if (endpoint)
      probe.CaptureClose(&h.manager);
    bool observed = false;
    h.a.hook = [&](const char *name) {
      if (observed || std::string(name) != "GetSelection")
        return;
      observed = true;
      if (endpoint)
        Require(probe.Finish(S_OK) == Verdict::Busy,
                "actual busy service cannot publish a shutdown endpoint");
      else
        probe.CaptureClose(&h.manager);
    };
    h.Key(L'h');
    h.a.hook = nullptr;
    Require(observed && probe.Finish(h.service->Deactivate()) == Verdict::Busy,
            "actual busy capture or endpoint is permanently rejected");
  }
}
struct FrozenSnapshotV1 {
  uint32_t words[55]{};
};
static_assert(sizeof(FrozenSnapshotV1) == 220);
struct FrozenReaderV1 : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE Copy(uint32_t size, uint32_t version,
                                         FrozenSnapshotV1 *output) = 0;
};
void ShutdownReaderLifetimeSchedules() {
  using namespace diagnostics;
  constexpr GUID frozen_iid = {
      0xf75f909b, 0x293a, 0x4a7b, {0xa1, 0x72, 0xf5, 0xa0, 0x61, 0x88, 0x03, 0x52}};
  for (bool old_first : {false, true}) {
    const auto objects = object_count;
    ComPtr<FrozenReaderV1> old;
    ComPtr<SnapshotReaderV2> current;
    {
      Harness h;
      h.Type(L"g");
      Require(h.service->QueryInterface(frozen_iid,
                                        reinterpret_cast<void **>(old.GetAddressOf())) == S_OK &&
                  h.service->QueryInterface(
                      snapshot_v2_iid, reinterpret_cast<void **>(current.GetAddressOf())) == S_OK,
              "both frozen v1 and current v2 readers retain the actual Service");
      SnapshotV2 active;
      FrozenSnapshotV1 legacy;
      Require(old->Copy(220, 1, &legacy) == S_OK &&
                  current->Copy(sizeof(active), version_v2, &active) == S_OK &&
                  std::memcmp(&legacy, &active.base, 220) == 0,
              "independent frozen v1 payload matches the active v2 prefix");
      h.a.active_composition = false;
      h.service->OnCompositionTerminated(123, h.a.composition.Get());
    }
    Require(object_count == objects + 1,
            "retained readers extend Service and engine lifetime beyond ordinary owner release");
    struct {
      uint32_t before = 17;
      FrozenSnapshotV1 value;
      uint32_t after = 23;
    } legacy;
    SnapshotV2 inactive;
    Require(old->Copy(220, 1, &legacy.value) == S_OK &&
                current->Copy(sizeof(inactive), version_v2, &inactive) == S_OK &&
                !inactive.base.active && !inactive.lifecycle.owned_present &&
                std::memcmp(&legacy.value, &inactive.base, 220) == 0 && legacy.before == 17 &&
                legacy.after == 23 && Valid(inactive),
            "both retained readers safely copy the inactive actual Service with frozen bounds");
    if (old_first) {
      old.Reset();
      Require(object_count == objects + 1 &&
                  current->Copy(sizeof(inactive), version_v2, &inactive) == S_OK,
              "v2 remains usable after v1 release");
      current.Reset();
    } else {
      current.Reset();
      Require(object_count == objects + 1 && old->Copy(220, 1, &legacy.value) == S_OK,
              "v1 remains usable after v2 release");
      old.Reset();
    }
    Require(object_count == objects, "actual Service accounting drains only after the last reader");
  }
}
void ShutdownWindowSchedules() {
  WNDCLASSW type{};
  type.hInstance = GetModuleHandleW(nullptr);
  type.lpfnWndProc = editor::Window;
  type.lpszClassName = L"KeyrillControlledShutdownWindow";
  Require(RegisterClassW(&type) != 0, "separate hidden host-window test class");
  for (int scenario = 0; scenario != 3; ++scenario) {
    DiagnosticProvider provider;
    Harness h;
    ShutdownHistory history;
    provider.snapshot = history.before;
    h.manager.diagnostic_sink = static_cast<ITfFunctionProvider *>(&provider);
    shutdown::Probe probe;
    editor::WindowContext context{&h.manager, &probe};
    HWND window = CreateWindowW(type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                                nullptr, nullptr, type.hInstance, nullptr);
    Require(window != nullptr, "hidden test window exists");
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&context));
    probe.Observe(&h.manager);
    bool captured_live = false;
    provider.hook = [&](const char *name) {
      if (std::string(name) == "copy")
        captured_live = IsWindow(window) && !context.destroyed;
    };
    if (scenario == 2)
      provider.snapshot.lifecycle.owned_present = provider.snapshot.lifecycle.owned_serial = 0;
    if (scenario == 1)
      DestroyWindow(window);
    else
      SendMessageW(window, WM_CLOSE, 0, 0);
    provider.hook = nullptr;
    Require(!IsWindow(window) && context.destroyed && captured_live == (scenario != 1),
            "real WndProc captures sent WM_CLOSE before normal destruction, never direct destroy");
    history.Record(diagnostics::LifecycleKind::ExactTermination);
    history.Deactivate();
    provider.snapshot = history.After();
    Require((probe.Finish(S_OK) == shutdown::Verdict::Verified) == (scenario == 0),
            "host close requires fresh owned capture while direct or empty destruction fails");
    MSG quit{};
    while (PeekMessageW(&quit, nullptr, 0, 0, PM_REMOVE)) {
      if (quit.message == WM_QUIT)
        break;
      TranslateMessage(&quit);
      DispatchMessageW(&quit);
    }
    Require(quit.message == WM_QUIT, "ordinary host destruction posts the existing quit message");
  }
  Require(UnregisterClassW(type.lpszClassName, type.hInstance), "hidden test class released");
}
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
    const HRESULT activation = h.service->Activate(&h.manager, 42);
    Require(scenario == 3 ? FAILED(activation) : activation == S_OK,
            "optional diagnostics preserve success only while the activation remains active");
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
void HostMetadataSchedules() {
  using host::Reason;
  auto run = [](Document &document, Manager &manager, bool &alive) {
    document.expected_request_flags = TF_ES_SYNC | TF_ES_READ;
    return host::Observe(
        &document, &manager, 42,
        {static_cast<ITfContext *>(&document), &alive, 1,
         [](void *state, uint64_t) noexcept { return *static_cast<bool *>(state); }});
  };
  auto unchanged = [](Document &document) {
    Require(document.reads == 0 && document.writes == 0 && document.starts == 0 &&
                document.ends == 0 && document.calls["GetExtent"] == 0 &&
                document.calls["ShiftStart"] == 0 && document.calls["EnumCompositions"] == 0 &&
                document.text == L"prefix suffix" && document.caret == 7,
            "metadata has no text, boundary, composition, extent or document effect");
  };
  for (int scenario = 0; scenario != 12; ++scenario) {
    Document document;
    Manager manager;
    manager.top = &document;
    bool alive = true;
    document.window = GetDlgItem(controlled_root, 101);
    SetFocus(document.window);
    Reason expected = Reason::ProofOnly;
    switch (scenario) {
    case 1:
      document.acp_available = false;
      expected = Reason::MissingAcp;
      break;
    case 2:
      document.status = TF_SD_READONLY;
      expected = Reason::ReadOnly;
      break;
    case 3:
      document.disabled = true;
      expected = Reason::Disabled;
      break;
    case 4:
      document.scope = IS_PASSWORD;
      expected = Reason::Scope;
      break;
    case 5:
      document.application_scope.present = false;
      expected = Reason::MissingScope;
      break;
    case 6:
      document.application_scope.scopes_count = 2;
      expected = Reason::Scope;
      break;
    case 7:
      document.callback = false;
      document.request_result = E_ACCESSDENIED;
      expected = Reason::Request;
      break;
    case 8:
      document.callback = false;
      document.session_result = TF_E_SYNCHRONOUS;
      expected = Reason::Session;
      break;
    case 9:
      document.callback = false;
      expected = Reason::Absent;
      break;
    case 10:
      document.window = nullptr;
      break;
    case 11:
      document.window = GetDlgItem(controlled_root, 104);
      expected = Reason::Secure;
      break;
    }
    const auto observed = run(document, manager, alive);
    Require(observed.reason == expected, "metadata reports exact production refusal");
    Require((observed.acp != 0) == (scenario == 0 || scenario == 10),
            "ACP is queried only after ordinary singleton scope");
    if (scenario == 1)
      Require(observed.selection && observed.scope == host::Scope::Default,
              "raw range scope is observed without ACP");
    if (scenario >= 2 && scenario <= 6)
      Require(document.calls["range.ACP"] == 0, "excluded or uncertain scope never queries ACP");
    if (scenario == 2 || scenario == 3 || scenario == 11)
      Require(document.calls["GetSelection"] == 0, "known excluded target stops before selection");
    if (scenario >= 7 && scenario <= 9) {
      const auto before = document.trace;
      Require(document.retained_request && FAILED(document.retained_request->DoEditSession(123)) &&
                  document.trace == before,
              "denied absent or deferred metadata request expires without native queries");
      document.retained_request.Reset();
    }
    unchanged(document);
  }
  bool compartments_match = true;
  for (int compartment = 0; compartment != 3; ++compartment) {
    const char *names[] = {"context keyboard-disabled", "empty-context",
                           "thread keyboard-disabled"};
    struct Outcome {
      HRESULT result;
      VARTYPE type;
      Reason reason;
      bool stale = false;
    };
    for (const auto outcome :
         {Outcome{S_FALSE, VT_EMPTY, Reason::ProofOnly}, Outcome{S_OK, VT_EMPTY, Reason::ProofOnly},
          Outcome{S_FALSE, VT_I4, Reason::Native}, Outcome{E_UNEXPECTED, VT_EMPTY, Reason::Native},
          Outcome{S_OK, VT_BOOL, Reason::Disabled},
          Outcome{S_FALSE, VT_EMPTY, Reason::Stale, true}}) {
      Document document;
      Manager manager;
      manager.top = &document;
      bool alive = true;
      document.window = GetDlgItem(controlled_root, 101);
      SetFocus(document.window);
      if (compartment == 2) {
        manager.compartment_result = outcome.result;
        manager.compartment_type = outcome.type;
      } else {
        document.compartment_result[compartment] = outcome.result;
        document.compartment_type[compartment] = outcome.type;
      }
      document.hook = [&](const char *name) {
        if (outcome.stale && (compartment == 2 ? strcmp(name, "thread.GetValue") == 0
                                               : strcmp(name, "compartment.GetValue") == 0 &&
                                                     document.calls[name] == compartment + 1))
          alive = false;
      };
      const auto observed = run(document, manager, alive);
      if (observed.reason != outcome.reason) {
        fprintf(stderr, "metadata %s HRESULT=%08lx VT=%u reason=%s expected=%s result=%08lx\n",
                names[compartment], static_cast<unsigned long>(outcome.result), outcome.type,
                host::reason_labels[static_cast<uint32_t>(observed.reason)],
                host::reason_labels[static_cast<uint32_t>(outcome.reason)],
                static_cast<unsigned long>(observed.result));
        compartments_match = false;
      }
      if (observed.reason == Reason::ProofOnly)
        Require(observed.selection && observed.scope == host::Scope::Default && observed.acp,
                "unset disabling compartment permits ordinary scope and ACP observation");
      else
        Require(!observed.selection && !observed.acp && document.calls["GetSelection"] == 0,
                "invalid or stale compartment stops before selection and ACP");
      if (outcome.reason == Reason::Native)
        Require(observed.result == static_cast<uint32_t>(outcome.result),
                "invalid compartment preserves its native failure result");
      unchanged(document);
    }
  }
  Require(compartments_match,
          "all disabling compartment results report the expected metadata reason");
  const char *queries[] = {"context.QI",       "manager.GetFocus",
                           "GetTop",           "GetActiveView",
                           "GetWnd",           "GetStatus",
                           "GetCompartment",   "compartment.GetValue",
                           "manager.QI",       "thread.GetCompartment",
                           "thread.GetValue",  "GetSelection",
                           "range.GetContext", "GetAppProperty",
                           "scope.GetValue",   "GetInputScopes",
                           "range.ACP"};
  for (const char *query : queries) {
    Document document;
    Manager manager;
    manager.top = &document;
    bool alive = true;
    document.window = GetDlgItem(controlled_root, 101);
    document.hook = [&](const char *name) {
      if (strcmp(name, query) == 0)
        alive = false;
    };
    const auto observed = run(document, manager, alive);
    Require(!alive && observed.reason == Reason::Stale,
            "liveness loss at each metadata query aborts observation");
    unchanged(document);
  }
  {
    Document document;
    Manager manager;
    manager.top = &document;
    bool alive = true;
    document.retain_callback = true;
    document.window = GetDlgItem(controlled_root, 101);
    document.hook = [&](const char *name) {
      if (strcmp(name, "GetSelection") == 0) {
        const auto before = document.trace;
        Require(FAILED(document.retained_request->DoEditSession(123)) && document.trace == before,
                "metadata callback reentry performs no additional native query");
      }
    };
    const auto observed = run(document, manager, alive);
    Require(observed.reason == Reason::ProofOnly,
            "reentrant duplicate cannot overwrite observation");
    document.retained_request.Reset();
    unchanged(document);
  }
}
void HostPublicationEquivalence() {
  diagnostics::SnapshotV2 baseline;
  {
    Harness h;
    h.Type(L"ghbdtn");
    Require(h.Key(L' '), "baseline boundary consumed");
    h.Oracle(L"привет ", 14);
    Require(h.service->Copy(sizeof(baseline), diagnostics::version_v2, &baseline) == S_OK,
            "unarmed service snapshot");
  }
  host::Identity identity;
  Require(host::Capture(GetCurrentProcess(), GetCurrentProcessId(), GetCurrentThreadId(), identity),
          "proof process identity");
  probe::ArmedPage armed;
  Require(armed.Create(identity), "proof service optional mapping armed");
  {
    Harness h;
    h.Type(L"ghbdtn");
    Require(h.Key(L' '), "armed boundary consumed identically");
    h.Oracle(L"привет ", 14);
    diagnostics::SnapshotV2 current;
    Require(h.service->Copy(sizeof(current), diagnostics::version_v2, &current) == S_OK &&
                current.base == baseline.base && current.lifecycle == baseline.lifecycle,
            "diagnostics on and off preserve exact executor and lifecycle behavior");
    probe::Payload payload;
    probe::Snapshot decoded;
    Require(armed.Read(payload) == probe::Availability::Available &&
                probe::Decode(payload, decoded) && decoded.service.base == current.base &&
                decoded.service.lifecycle == current.lifecycle &&
                decoded.service.lifecycle.owned_present && decoded.service.lifecycle.owned_serial,
            "outermost production callback publishes exact service and owned serial");
    h.service->OnSetFocus(FALSE);
    Require(armed.Read(payload) == probe::Availability::Available &&
                probe::Decode(payload, decoded) &&
                decoded.metadata.reason == host::Reason::Unobserved,
            "lifecycle invalidation expires cached metadata");
    KeyboardState keyboard;
    keyboard.Set(VK_LCONTROL, 0x80);
    keyboard.Set(VK_LMENU, 0x80);
    Preserved(h, 'K');
    Require(armed.Read(payload) == probe::Availability::Available,
            "known preserved command publishes through actual Service mapping");
    const DWORD observation_count = payload.size() > 179 ? payload[179] : 0;
    if (!observation_count)
      fprintf(stderr, "command observation mismatch: expected at least 2 records, actual %lu\n",
              observation_count);
    Require(observation_count >= 2, "known command publishes arrival and progress observations");
  }
  probe::Payload payload;
  Require(armed.Read(payload) == probe::Availability::Missing,
          "service destruction releases only its publisher claim");
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
                h.a.active_composition && h.a.writes == writes + 2 && h.a.starts == starts + 1 &&
                h.a.ends == ends + 1 && h.a.max_request_writes == 2,
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
    Require(eaten == !reentry && h.a.ends == 1 && h.a.max_request_writes == (reentry ? 1 : 2),
            "ordinary end reentry preserves disposition and permits only distinct plan writes");
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
void UncertainApplicationInputScope(const native_field::Source *field_source = nullptr) {
  for (int scenario = 0; scenario != 22; ++scenario) {
    Harness h(nullptr, GetKeyboardLayoutList, field_source);
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
    Require(
        h.a.calls["range.ACP"] == 0 && h.a.reads == 0 && h.a.starts == 0 && h.a.writes == 0,
        "uncertain application input scope has no ACP, text reads, composition starts or writes");
    h.Oracle(L"", 7);
  }
}
struct NotepadFieldSource final : native_field::Source {
  native_field::Facts facts;
  mutable unsigned captures = 0;
  std::function<void(unsigned)> on_capture;
  NotepadFieldSource() {
    facts = native_field::Capture(GetDlgItem(controlled_root, 101), {});
    facts.proof_process = false;
    facts.host.process = facts.process;
    facts.host.thread = facts.thread;
    facts.host.kind = host::Kind::PackagedNotepad;
    facts.host.version = {11, 2607, 14, 0};
    facts.field.control = facts.parent.control = facts.grandparent.control = 0;
    wcscpy_s(facts.field.class_name.data(), facts.field.class_name.size(), L"RichEditD2DPT");
    wcscpy_s(facts.parent.class_name.data(), facts.parent.class_name.size(), L"NotepadTextBox");
    wcscpy_s(facts.grandparent.class_name.data(), facts.grandparent.class_name.size(), L"Notepad");
    facts.field.style = WS_CHILD | ES_MULTILINE;
    facts.parent.handle = reinterpret_cast<HWND>(2);
    facts.grandparent.handle = facts.root;
    facts.parent.process = facts.grandparent.process = facts.process;
    facts.parent.thread = facts.thread;
    facts.grandparent.thread = facts.thread + 1;
    facts.parent.style = WS_CHILD;
    facts.grandparent.style = 0;
    facts.parent.complete = facts.grandparent.complete = true;
    facts.parent.enabled = facts.grandparent.enabled = true;
    facts.foreground = facts.root;
  }
  native_field::Facts Capture(HWND view, const host::Identity &) const noexcept override {
    if (on_capture)
      on_capture(++captures);
    auto result = facts;
    if (view != result.view)
      result.focus = nullptr;
    return result;
  }
};
using FieldMutation = std::function<void(native_field::Facts &)>;
std::vector<FieldMutation> NotepadFieldRefusals() {
  using native_field::Facts;
  std::vector<FieldMutation> mutations = {[](Facts &f) { f.host.kind = host::Kind::Unknown; },
                                          [](Facts &f) { ++f.host.process; },
                                          [](Facts &f) { ++f.host.thread; },
                                          [](Facts &f) { f.process = 0; },
                                          [](Facts &f) { f.thread = 0; },
                                          [](Facts &f) { f.focus = nullptr; },
                                          [](Facts &f) { f.view = nullptr; },
                                          [](Facts &f) { f.foreground = nullptr; },
                                          [](Facts &f) { f.root = nullptr; },
                                          [](Facts &f) { f.field.style &= ~WS_CHILD; },
                                          [](Facts &f) { f.field.style &= ~ES_MULTILINE; },
                                          [](Facts &f) { f.field.style |= ES_PASSWORD; },
                                          [](Facts &f) { f.field.style |= ES_READONLY; },
                                          [](Facts &f) { ++f.field.thread; },
                                          [](Facts &f) { ++f.parent.thread; },
                                          [](Facts &f) { f.grandparent.handle = f.parent.handle; }};
  for (size_t version = 0; version != 4; ++version)
    mutations.push_back([version](Facts &f) { ++f.host.version[version]; });
  for (int node = 0; node != 3; ++node) {
    const auto select = [node](Facts &f) -> native_field::Window & {
      return node == 0 ? f.field : node == 1 ? f.parent : f.grandparent;
    };
    mutations.push_back([select](Facts &f) { select(f).complete = false; });
    mutations.push_back([select](Facts &f) { select(f).handle = nullptr; });
    mutations.push_back([select](Facts &f) { ++select(f).process; });
    mutations.push_back([select](Facts &f) { select(f).thread = 0; });
    mutations.push_back([select](Facts &f) { ++select(f).control; });
    mutations.push_back([select](Facts &f) { select(f).enabled = false; });
    mutations.push_back([select](Facts &f) { select(f).style |= WS_DISABLED; });
    mutations.push_back([select](Facts &f) { select(f).class_name[0] = L'!'; });
  }
  return mutations;
}
void NoAdmissionEffects(Harness &h) {
  Require(h.a.calls["range.ACP"] == 0 && h.a.reads == 0 && h.a.starts == 0 && h.a.writes == 0,
          "refused admission performs zero ACP, character reads, composition starts and writes");
  h.Oracle(L"", 7);
}
void NotepadNativeAdmission() {
  NotepadFieldSource source;
  const auto identity = native_field::Match(source.facts);
  Require(identity && identity->kind == native_field::Kind::Notepad,
          "exact version and direct main-field contract permits a different root thread");
  auto scrolled = source.facts;
  scrolled.field.style |= WS_VSCROLL | WS_HSCROLL;
  Require(native_field::Match(scrolled) == identity,
          "dynamic scrollbar bits are not field identity");
  for (const auto &mutate : NotepadFieldRefusals()) {
    NotepadFieldSource refused;
    mutate(refused.facts);
    Require(!native_field::Match(refused.facts), "one changed native contract fact refuses");
    Harness h(nullptr, GetKeyboardLayoutList, &refused);
    Require(!h.Key(L'g', false), "unsupported native field passes unchanged");
    NoAdmissionEffects(h);
  }
  for (int scenario = 0; scenario != 18; ++scenario) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    if (scenario == 0)
      h.a.status = TF_SD_READONLY;
    if (scenario == 1)
      h.a.disabled = true;
    if (scenario == 2)
      h.a.empty_disabled = true;
    if (scenario == 3)
      h.manager.disabled = true;
    if (scenario >= 4 && scenario < 10) {
      const int index = (scenario - 4) / 3;
      if ((scenario - 4) % 3 == 2)
        h.manager.compartment_result = index ? E_FAIL : E_ACCESSDENIED;
      else
        h.a.compartment_result[(scenario - 4) % 3] = index ? E_FAIL : E_ACCESSDENIED;
    }
    if (scenario >= 10 && scenario < 16) {
      const int index = (scenario - 10) / 3;
      if ((scenario - 10) % 3 == 2)
        h.manager.compartment_type = static_cast<VARTYPE>(index ? VT_BOOL : VT_UNKNOWN);
      else
        h.a.compartment_type[(scenario - 10) % 3] =
            static_cast<VARTYPE>(index ? VT_BOOL : VT_UNKNOWN);
    }
    if (scenario == 16)
      h.a.application_scope = {S_OK, true, E_FAIL, VT_EMPTY};
    if (scenario == 17)
      h.a.application_scope = {S_OK, true, E_ACCESSDENIED, VT_EMPTY};
    if (scenario == 16) {
      h.a.hook = [&](const char *call) {
        if (strcmp(call, "scope.GetValue") == 0)
          ordinary.facts.focus = nullptr;
      };
    }
    Require(!h.Key(L'g', false), "disabled, uncertain or stale main field passes unchanged");
    NoAdmissionEffects(h);
  }
}
void NotepadScopeAdmission() {
  NotepadFieldSource source;
  UncertainApplicationInputScope(&source);
  for (int scenario = 0; scenario != 5; ++scenario) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    auto &property = h.a.application_scope;
    property.value_type = VT_EMPTY;
    property.value_result = scenario == 0   ? S_OK
                            : scenario == 1 ? S_FALSE
                            : scenario == 2 ? E_ACCESSDENIED
                                            : E_FAIL;
    if (scenario == 3)
      property.result = S_FALSE;
    if (scenario == 4)
      h.a.hook = [&](const char *call) {
        if (strcmp(call, "scope.GetValue") == 0)
          h.service->OnPushContext(&h.b);
      };
    Require(!h.Key(L'g', false),
            "only fresh exact E_FAIL with VT_EMPTY gets exceptional authority");
    NoAdmissionEffects(h);
  }
  for (bool unavailable : {false, true}) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    if (unavailable)
      h.a.application_scope = {S_OK, true, E_FAIL, VT_EMPTY};
    h.Type(L"ghbdtn ");
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 ", 14);
    Require(h.a.starts == 1 && h.a.writes == 7 && h.a.max_request_writes == 1,
            "admitted main-field facts use the existing owned correction executor");
    Require(unavailable ? h.a.calls["GetInputScopes"] == 0 : h.a.calls["GetInputScopes"] > 0,
            "unavailable value is distinct from an explicit ordinary scope");
    const auto first_scope = std::find(h.a.trace.begin(), h.a.trace.end(), "scope.GetValue");
    const auto first_acp = std::find(h.a.trace.begin(), h.a.trace.end(), "range.ACP");
    Require(first_scope < first_acp, "scope classification precedes the first ACP query");
    Require(h.Key(L'x', false),
            "retained boundary finishes and replans through the same admission");
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 x", 15);
  }
  {
    Harness h;
    h.a.application_scope = {S_OK, true, E_FAIL, VT_EMPTY};
    Require(!h.Key(L'g', false),
            "proof control cannot use the Notepad unavailable-value authority");
    NoAdmissionEffects(h);
  }
}
void NotepadExecutorRefusals() {
  for (int scenario = 0; scenario != 8; ++scenario) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.a.application_scope = {S_OK, true, E_FAIL, VT_EMPTY};
    if (scenario == 0)
      h.a.acp_available = false;
    if (scenario == 1) {
      h.a.callback = false;
      h.a.request_result = E_ACCESSDENIED;
    }
    if (scenario == 2)
      h.a.callback = false;
    if (scenario == 3)
      h.a.foreign = true;
    if (scenario == 4)
      h.a.reject_composition = true;
    if (scenario == 5)
      ordinary.on_capture = [&](unsigned count) {
        if (count == 3)
          ordinary.facts.parent.handle = reinterpret_cast<HWND>(3);
      };
    if (scenario == 6)
      h.a.hook = [&](const char *call) {
        if (strcmp(call, "range.GetContext") == 0)
          h.service->OnPushContext(&h.b);
      };
    if (scenario == 7)
      h.a.hook = [&](const char *call) {
        if (strcmp(call, "GetInputScopes") == 0)
          h.service->OnPushContext(&h.b);
      };
    if (scenario == 7)
      h.a.application_scope = {};
    Require(!h.Key(L'g', false), "native executor refusal preserves ordinary key disposition");
    Require(h.a.writes == 0, "no native refusal before SetText writes characters");
    h.Oracle(L"", 7);
    if (h.a.retained_request) {
      const auto acp = h.a.calls["range.ACP"];
      Require(FAILED(h.a.retained_request->DoEditSession(123)) && h.a.calls["range.ACP"] == acp &&
                  h.a.writes == 0,
              "late callback cannot revive admission or inspect text");
    }
  }
  for (int scenario = 0; scenario != 11; ++scenario) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.a.application_scope = {S_OK, true, E_FAIL, VT_EMPTY};
    if (scenario == 0)
      h.a.status_result = E_FAIL;
    if (scenario == 1)
      h.a.selection_query_result = E_FAIL;
    if (scenario == 2)
      h.a.selection_query_result = S_FALSE;
    if (scenario == 3)
      h.a.selection_present = false;
    if (scenario == 4)
      h.a.selection_fetched = 0;
    if (scenario == 5)
      h.a.selection_fetched = 2;
    if (scenario == 6)
      h.a.selection_style.fInterimChar = TRUE;
    if (scenario == 7)
      h.a.selection_style.ase = static_cast<TfActiveSelEnd>(99);
    if (scenario == 8)
      h.a.range_context = &h.b;
    if (scenario == 9) {
      h.a.application_scope = {};
      h.a.scope_interface_result = S_FALSE;
    }
    if (scenario == 10)
      h.a.hook = [&](const char *call) {
        if (strcmp(call, "GetSelection") == 0)
          h.service->OnPushContext(&h.b);
      };
    Require(!h.Key(L'g', false), "raw selection, context and scope failures pass unchanged");
    NoAdmissionEffects(h);
  }
}
void NotepadFieldDriftBeforeText() {
  for (const auto *boundary : {"range.ACP", "GetExtent", "enumerator.Next", "Clone", "ShiftStart"})
    for (bool readonly : {false, true}) {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      h.a.application_scope = {S_OK, true, E_FAIL, VT_EMPTY};
      bool drifted = false;
      h.a.hook = [&](const char *call) {
        if (strcmp(call, boundary) == 0) {
          ordinary.facts.field.style |= readonly ? ES_READONLY : ES_PASSWORD;
          drifted = true;
        }
      };
      Require(!h.Key(L'g', false) && drifted && h.a.reads == 0 && h.a.starts == 0 &&
                  h.a.writes == 0,
              "native exclusion drift after ACP or range callbacks refuses before boundary text");
      h.Oracle(L"", 7);
    }
  for (const auto *boundary : {"GetRange", "GetExtent", "enumerator.Next"})
    for (bool readonly : {false, true}) {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      h.Type(L"g");
      const int reads = h.a.reads, writes = h.a.writes;
      bool drifted = false;
      h.a.hook = [&](const char *call) {
        if (strcmp(call, boundary) == 0 &&
            (strcmp(call, "GetExtent") != 0 || h.a.active_composition)) {
          ordinary.facts.field.style |= readonly ? ES_READONLY : ES_PASSWORD;
          drifted = true;
        }
      };
      Require(
          !h.Key(L'h', false) && drifted && h.a.reads == reads && h.a.writes == writes,
          "native exclusion drift before owned-mark readback makes no new character read or write");
      h.Oracle(L"g", 8);
    }
}
void FinishContractRegressions() {
  bool passed = true;
  struct FinishSelection {
    const char *name;
    LONG caret, length;
  };
  for (const auto &selection :
       {FinishSelection{"unchanged", 12, 0}, FinishSelection{"moved-caret", 11, 0},
        FinishSelection{"whole-document-selection", 0, 18}}) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.Type(L"hello");
    h.a.caret = selection.caret;
    h.a.selection_length = selection.length;
    const auto writes = h.a.writes, ends = h.a.ends;
    kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
    Require(!h.service->HandleEvent(&h.a, finish) && !h.a.active_composition &&
                !h.service->stopped() && h.a.ends == ends + 1,
            "unchanged finish passes the key and ends the owned composition once");
    h.Oracle(L"hello", selection.caret);
    Require(h.a.selection_length == selection.length,
            "unchanged finish preserves the exact selection");
    const auto additional_writes = h.a.writes - writes;
    fprintf(stderr,
            "FINISH %s owned SetText expected=1 actual=%d; EndComposition expected=1 "
            "actual=%d; document/caret/selection preserved\n",
            selection.name, additional_writes, h.a.ends - ends);
    passed = additional_writes == 1 && passed;
  }
  for (bool readonly : {false, true}) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.Type(L"ghbdtn ");
    const auto before = DiagnosticSnapshot(*h.service.Get());
    int reads_at_end = -1, writes_at_end = -1;
    const auto ends = h.a.ends;
    h.a.hook = [&](const char *call) {
      if (strcmp(call, "EndComposition.after") == 0) {
        reads_at_end = h.a.reads;
        writes_at_end = h.a.writes;
        if (readonly)
          h.a.status = TF_SD_READONLY;
        else
          h.a.application_scope.value_result = E_ACCESSDENIED;
      }
    };
    Require(!h.Key(L'x', false) && reads_at_end >= 0 && h.a.ends == ends + 1 && h.a.starts == 1 &&
                h.a.writes == writes_at_end,
            "post-end exclusion passes the next key without a new composition or text write");
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 ", 14);
    const auto post_end_reads = h.a.reads - reads_at_end;
    const auto after = DiagnosticSnapshot(*h.service.Get());
    constexpr auto unknown = static_cast<size_t>(diagnostics::Counter::Unknown);
    fprintf(stderr,
            "FINISH post-end %s GetText expected=0 actual=%d; unknown delta=%llu; "
            "stopped=%d; next key passed; document/caret preserved\n",
            readonly ? "read-only" : "scope-denied", post_end_reads,
            static_cast<unsigned long long>(after.counts[unknown] - before.counts[unknown]),
            h.service->stopped());
    passed = post_end_reads == 0 && after.counts[unknown] == before.counts[unknown] + 1 &&
             h.service->stopped() && passed;
    const auto reads = h.a.reads, writes = h.a.writes;
    Require(!h.Key(L'y', false) && h.a.reads == reads && h.a.writes == writes &&
                h.a.ends == ends + 1 && h.a.starts == 1,
            "excluded continuation makes no later read, write, replay or repair");
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 ", 14);
  }
  Require(passed, "FINISH contract regression aggregate");
}
bool UnchangedFinishSelectionEffects() {
  bool passed = true;
  const std::wstring corrected = L"\x043f\x0440\x0438\x0432\x0435\x0442 ";
  const std::wstring document = L"prefix " + corrected + L"suffix";
  struct Selection {
    const char *name;
    LONG start, length;
    TfActiveSelEnd active;
  };
  const Selection selections[] = {
      {"end-caret", 14, 0, TF_AE_NONE},         {"moved-caret", 11, 0, TF_AE_NONE},
      {"outside-caret", 3, 0, TF_AE_NONE},      {"inside-selection", 9, 3, TF_AE_START},
      {"crossing-selection", 6, 10, TF_AE_END}, {"whole-document-selection", 0, 20, TF_AE_START}};
  const auto print_document = [](const char *label, const std::wstring &text) {
    fprintf(stderr, "%s UTF-16=", label);
    for (wchar_t unit : text)
      fprintf(stderr, "%04x ", static_cast<unsigned>(unit));
    fputc('\n', stderr);
  };
  for (const char *boundary : {"none", "SetText.after", "EndComposition.after"}) {
    for (const auto &selection : selections) {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      h.Type(L"ghbdtn ");
      h.Oracle(corrected.c_str(), 14);
      h.a.caret = selection.start;
      h.a.selection_length = selection.length;
      h.a.selection_style = {selection.active, FALSE};
      const auto writes = h.a.writes, ends = h.a.ends, starts = h.a.starts;
      bool moved = false;
      h.a.hook = [&](const char *call) {
        if (!moved && strcmp(call, boundary) == 0) {
          moved = true;
          h.a.caret = 7;
          h.a.selection_length = 0;
          h.a.selection_style = {TF_AE_NONE, FALSE};
        }
      };
      const kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
      const bool eaten = h.service->HandleEvent(&h.a, finish);
      const bool matched =
          !eaten && h.a.text == document && h.a.caret == selection.start &&
          h.a.selection_length == selection.length && h.a.selection_style.ase == selection.active &&
          !h.a.selection_style.fInterimChar && h.a.writes == writes + 1 && h.a.ends == ends + 1 &&
          h.a.starts == starts && !h.a.active_composition && !h.service->stopped() &&
          moved == (strcmp(boundary, "none") != 0) && h.b.text == L"prefix suffix" &&
          h.b.caret == 7 && h.b.selection_length == 0;
      fprintf(stderr,
              "%s unchanged FINISH %s %s selection expected=%ld/%ld/%u/0 actual=%ld/%ld/%u/%d; "
              "owned SetText expected=1 actual=%d; EndComposition expected=1 actual=%d; "
              "document matched=%d; eaten expected=0 actual=%d; stopped expected=0 actual=%d\n",
              matched ? "PASS" : "FAIL", boundary, selection.name, selection.start,
              selection.length, static_cast<unsigned>(selection.active), h.a.caret,
              h.a.selection_length, static_cast<unsigned>(h.a.selection_style.ase),
              h.a.selection_style.fInterimChar, h.a.writes - writes, h.a.ends - ends,
              h.a.text == document, eaten, h.service->stopped());
      if (h.a.text != document) {
        print_document("expected", document);
        print_document("actual", h.a.text);
      }
      passed = matched && passed;
    }
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.Type(L"ghbdtn ");
    h.Oracle(corrected.c_str(), 14);
    const auto writes = h.a.writes, ends = h.a.ends, starts = h.a.starts;
    bool moved = false;
    h.a.hook = [&](const char *call) {
      if (!moved && h.a.starts == starts && strcmp(call, boundary) == 0) {
        moved = true;
        h.a.caret = 7;
        h.a.selection_length = 0;
      }
    };
    const bool eaten = h.Key(L'x', false);
    const std::wstring continued = L"prefix " + corrected + L"xsuffix";
    std::vector<OperationTrace> operations;
    std::copy_if(
        h.a.operations.begin(), h.a.operations.end(), std::back_inserter(operations),
        [&](const OperationTrace &operation) { return operation.request == h.a.request_ordinal; });
    const bool ordered =
        operations.size() == 4 && operations[0].operation == NativeOperation::SetText &&
        operations[0].composition == starts && operations[0].start == 7 &&
        operations[0].length == 7 && operations[0].replacement_length == 7 &&
        operations[1].operation == NativeOperation::End && operations[1].composition == starts &&
        operations[2].operation == NativeOperation::Start &&
        operations[2].composition == starts + 1 && operations[2].start == 14 &&
        operations[2].length == 0 && operations[3].operation == NativeOperation::SetText &&
        operations[3].composition == starts + 1 && operations[3].start == 14 &&
        operations[3].length == 0 && operations[3].replacement_length == 1;
    const bool matched = eaten && ordered && h.a.text == continued && h.a.caret == 15 &&
                         h.a.selection_length == 0 && h.a.writes == writes + 2 &&
                         h.a.ends == ends + 1 && h.a.starts == starts + 1 &&
                         h.a.active_composition && h.a.composition->span->start == 14 &&
                         h.a.composition->span->length == 1 && !h.service->stopped() &&
                         moved == (strcmp(boundary, "none") != 0) && h.b.text == L"prefix suffix" &&
                         h.b.caret == 7 && h.b.selection_length == 0;
    fprintf(stderr,
            "%s unchanged FINISH continuation %s document matched=%d; caret expected=15/0 "
            "actual=%ld/%ld; new mark expected=14/1 actual=%ld/%ld; "
            "writes expected=2 actual=%d; EndComposition expected=1 actual=%d; "
            "ordered=%d; eaten expected=1 actual=%d\n",
            matched ? "PASS" : "FAIL", boundary, h.a.text == continued, h.a.caret,
            h.a.selection_length, h.a.composition->span->start, h.a.composition->span->length,
            h.a.writes - writes, h.a.ends - ends, ordered, eaten);
    if (h.a.text != continued) {
      print_document("expected", continued);
      print_document("actual", h.a.text);
    }
    passed = matched && passed;
  }
  return passed;
}
void FinishSelectionGravity() {
  const std::wstring corrected = L"\x043f\x0440\x0438\x0432\x0435\x0442 ";
  for (bool gravity : {false, true})
    for (const auto target : {Span{14, 0}, Span{3, 0}, Span{9, 3}, Span{6, 10}, Span{0, 20}})
      for (auto active : {TF_AE_NONE, TF_AE_START, TF_AE_END}) {
        NotepadFieldSource ordinary;
        Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
        h.Type(L"ghbdtn ");
        h.a.caret = target.start;
        h.a.selection_length = target.length;
        h.a.selection_style = {active, FALSE};
        h.a.move_clones = gravity;
        const auto writes = h.a.writes, ends = h.a.ends, starts = h.a.starts;
        const auto selections = h.a.calls["SetSelection"];
        const auto reads = h.a.text_reads.size();
        h.a.hook = [&](const char *call) {
          if (!strcmp(call, "SetText.after") || !strcmp(call, "EndComposition.after")) {
            h.a.caret = 7;
            h.a.selection_length = 0;
            h.a.selection_style = {TF_AE_NONE, FALSE};
          }
        };
        const kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
        Require(!h.service->HandleEvent(&h.a, finish) && h.a.writes == writes + 1 &&
                    h.a.ends == ends + 1 && h.a.starts == starts && !h.a.active_composition &&
                    !h.service->stopped() && h.a.caret == target.start &&
                    h.a.selection_length == target.length && h.a.selection_style.ase == active &&
                    !h.a.selection_style.fInterimChar &&
                    h.a.calls["SetSelection"] == selections + 1,
                "both finish effects preserve exact selection and style despite clone gravity");
        h.Oracle(corrected.c_str(), target.start);
        Require(std::all_of(h.a.text_reads.begin() + reads, h.a.text_reads.end(),
                            [](const Span &span) { return span.start == 7 && span.length == 7; }),
                "selection preservation never reads characters outside the owned finished range");
      }
  NotepadFieldSource ordinary;
  Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
  h.Type(L"hello");
  const auto selections = h.a.calls["SetSelection"];
  Require(!h.Key(L'!', false) && h.a.calls["SetSelection"] == selections,
          "matching unchanged FINISH selection needs no selection effect");
  h.Oracle(L"hello", 12);
}
void FinishSelectionFailures() {
  struct Failure {
    const char *boundary;
    int kind;
    bool attempted;
  };
  const Failure failures[] = {{"Clone.after", 0, false},
                              {"Clone.after", 1, false},
                              {"Clone.after", 2, false},
                              {"Clone.after", 3, false},
                              {"Clone.after", 4, false},
                              {"Clone.after", 5, false},
                              {"ShiftEnd", 6, true},
                              {"ShiftStart", 7, true},
                              {"ShiftEnd", 8, true},
                              {"ShiftStart", 9, true},
                              {"ShiftEnd", 10, true},
                              {"ShiftStart", 11, true},
                              {"SetSelection.after", 12, true},
                              {"SetSelection.after", 13, true},
                              {"SetSelection.after", 14, true},
                              {"SetSelection.after", 15, true},
                              {"SetSelection.after", 16, true},
                              {"SetSelection.after", 17, true},
                              {"SetSelection.after", 18, true},
                              {"SetSelection.after", 19, true},
                              {"GetSelection", 20, false},
                              {"GetSelection", 21, false},
                              {"GetSelection", 22, false},
                              {"GetSelection", 23, false},
                              {"GetSelection", 24, false},
                              {"GetSelection", 25, false},
                              {"ShiftEnd.after", 26, true},
                              {"ShiftStart.after", 27, true},
                              {"SetSelection.after", 28, true},
                              {"SetSelection.after", 29, true},
                              {"SetSelection.after", 30, true}};
  for (const auto failure : failures) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.Type(L"ghbdtn ");
    h.a.caret = 6;
    h.a.selection_length = 10;
    h.a.selection_style = {TF_AE_START, FALSE};
    h.a.move_clones = true;
    const auto before = DiagnosticSnapshot(*h.service.Get());
    const auto writes = h.a.writes, ends = h.a.ends, starts = h.a.starts;
    const auto selections = h.a.calls["SetSelection"];
    const auto selection_queries = h.a.calls["GetSelection"];
    const auto reads = h.a.text_reads.size();
    bool injected = false;
    int excluded_reads = -1;
    h.a.hook = [&](const char *call) {
      if (!strcmp(call, "EndComposition.after")) {
        h.a.caret = 7;
        h.a.selection_length = 0;
        h.a.selection_style = {TF_AE_NONE, FALSE};
      }
      if (injected || strcmp(call, failure.boundary) ||
          (failure.kind >= 20 && failure.kind <= 25 &&
           h.a.calls["GetSelection"] != selection_queries + 2))
        return;
      injected = true;
      switch (failure.kind) {
      case 0:
        ++h.a.caret;
        break;
      case 1:
        --h.a.selection_length;
        break;
      case 2:
        h.a.selection_style.ase = TF_AE_END;
        break;
      case 3:
        h.a.clones.back().lock()->start = 5;
        break;
      case 4:
        h.service->OnPushContext(&h.b);
        break;
      case 5:
        h.a.range_context = &h.b;
        break;
      case 6:
        h.a.shift_end_limit = 1;
        break;
      case 7:
        h.a.shift_start_limit = 1;
        break;
      case 8:
        h.a.shift_end_result = E_FAIL;
        break;
      case 9:
        h.a.shift_start_result = E_FAIL;
        break;
      case 10:
        h.a.shift_end_result = S_FALSE;
        break;
      case 11:
        h.a.shift_start_result = S_FALSE;
        break;
      case 12:
        h.a.selection_result = E_FAIL;
        break;
      case 13:
        h.a.selection_style.ase = TF_AE_END;
        break;
      case 14:
        h.a.selection_style.fInterimChar = TRUE;
        break;
      case 15:
        ++h.a.caret;
        break;
      case 16:
        h.a.foreign = true;
        break;
      case 17:
        h.a.status = TF_SD_READONLY;
        excluded_reads = h.a.reads;
        break;
      case 18:
        h.a.scope = IS_PRIVATE;
        excluded_reads = h.a.reads;
        break;
      case 19:
        h.service->OnPushContext(&h.b);
        excluded_reads = h.a.reads;
        break;
      case 20:
        h.a.selection_query_result = S_FALSE;
        break;
      case 21:
        h.a.selection_query_result = E_FAIL;
        break;
      case 22:
        h.a.selection_fetched = 2;
        break;
      case 23:
        h.a.selection_present = false;
        break;
      case 24:
        h.a.selection_style.fInterimChar = TRUE;
        break;
      case 25:
        h.a.selection_style.ase = static_cast<TfActiveSelEnd>(99);
        break;
      case 26:
      case 27:
        h.a.foreign = true;
        break;
      case 28:
        h.a.selection_result = S_FALSE;
        break;
      case 29:
        ++h.a.composition->span->length;
        break;
      case 30:
        h.a.text[7] = L'!';
        break;
      }
    };
    Require(!h.Key(L'x', false) && injected && h.service->stopped() &&
                h.a.writes == writes + (failure.attempted ? 1 : 0) &&
                h.a.ends == ends + (failure.attempted ? 1 : 0) && h.a.starts == starts &&
                h.a.calls["SetSelection"] ==
                    selections +
                        ((failure.kind >= 12 && failure.kind < 20) || failure.kind >= 28 ? 1 : 0),
            "selection failure refuses before write or stops unknown without replan or retry");
    const auto after = DiagnosticSnapshot(*h.service.Get());
    constexpr auto unknown = static_cast<size_t>(diagnostics::Counter::Unknown);
    constexpr auto refused = static_cast<size_t>(diagnostics::Counter::Refused);
    Require(after.counts[unknown] == before.counts[unknown] + (failure.attempted ? 1 : 0) &&
                after.counts[refused] == before.counts[refused] + (failure.attempted ? 0 : 1),
            "selection failure acknowledges the exact attempted or refused outcome once");
    if (excluded_reads >= 0)
      Require(h.a.reads == excluded_reads,
              "restoration callback exclusion forbids character reads");
    Require(h.a.text == (failure.kind == 30
                             ? L"prefix !\x0440\x0438\x0432\x0435\x0442 suffix"
                             : L"prefix \x043f\x0440\x0438\x0432\x0435\x0442 suffix") &&
                h.b.text == L"prefix suffix" && h.b.caret == 7 && h.b.selection_length == 0 &&
                std::all_of(h.a.text_reads.begin() + reads, h.a.text_reads.end(),
                            [](const Span &span) { return span.start == 7 && span.length == 7; }),
            "failed preservation never addresses selection characters or changes either document");
    const auto final_reads = h.a.reads, final_selections = h.a.calls["SetSelection"];
    h.a.hook = nullptr;
    Require(!h.Key(L'y', false) && h.a.reads == final_reads && h.a.starts == starts &&
                h.a.writes == writes + (failure.attempted ? 1 : 0) &&
                h.a.calls["SetSelection"] == final_selections,
            "later input cannot replay or repair failed selection preservation");
  }
}
void FinishSelectionPolicyBoundaries() {
  const kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
  const auto prepare = [](Harness &h) {
    h.Type(L"ghbdtn ");
    h.a.caret = 6;
    h.a.selection_length = 10;
    h.a.selection_style = {TF_AE_START, FALSE};
    h.a.move_clones = true;
    h.a.trace.clear();
  };
  const auto move_selection = [](Document &document, const char *call) {
    if (!strcmp(call, "EndComposition.after")) {
      document.caret = 7;
      document.selection_length = 0;
      document.selection_style = {TF_AE_NONE, FALSE};
    }
  };
  for (const char *effect :
       {"SetText.before", "ShiftEnd", "ShiftStart", "SetSelection", "GetText"}) {
    size_t preparation = 0;
    {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      prepare(h);
      h.a.hook = [&](const char *call) { move_selection(h.a, call); };
      Require(!h.service->HandleEvent(&h.a, finish) && !h.service->stopped(),
              "positive selection policy schedule reaches verified FINISH");
      auto found = !strcmp(effect, "GetText")
                       ? std::find(h.a.trace.rbegin(), h.a.trace.rend(), effect).base() - 1
                       : std::find(h.a.trace.begin(), h.a.trace.end(), effect);
      Require(found != h.a.trace.end(), "selection preservation reaches protected operation");
      while (found != h.a.trace.begin() && *found != "GetExtent")
        --found;
      Require(*found == "GetExtent", "selection effect has native metadata preparation");
      preparation = static_cast<size_t>(found - h.a.trace.begin());
    }
    for (int policy = 0; policy != 8; ++policy) {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      prepare(h);
      const auto before = DiagnosticSnapshot(*h.service.Get());
      const auto initial_writes = h.a.writes;
      bool injected = false;
      int reads = -1, writes = -1, starts = -1, ends = -1, selections = -1;
      h.a.hook = [&](const char *call) {
        move_selection(h.a, call);
        if (injected || h.a.trace.size() != preparation + 1)
          return;
        injected = true;
        reads = h.a.reads;
        writes = h.a.writes;
        starts = h.a.starts;
        ends = h.a.ends;
        selections = h.a.calls["SetSelection"];
        switch (policy) {
        case 0:
          h.a.status = TF_SD_READONLY;
          break;
        case 1:
          h.a.disabled = true;
          break;
        case 2:
          h.a.empty_disabled = true;
          break;
        case 3:
          h.manager.disabled = true;
          break;
        case 4:
          h.a.scope = IS_PRIVATE;
          break;
        case 5:
          h.a.application_scope.value_result = E_ACCESSDENIED;
          break;
        case 6:
          ordinary.facts.field.style |= ES_READONLY;
          break;
        case 7:
          h.service->OnPushContext(&h.b);
          break;
        }
      };
      Require(!h.service->HandleEvent(&h.a, finish) && injected && h.service->stopped() &&
                  h.a.reads == reads && h.a.writes == writes && h.a.starts == starts &&
                  h.a.ends == ends && h.a.calls["SetSelection"] == selections,
              "fresh selection policy loss prevents every later character read or native effect");
      const auto after = DiagnosticSnapshot(*h.service.Get());
      constexpr auto unknown = static_cast<size_t>(diagnostics::Counter::Unknown);
      constexpr auto refused = static_cast<size_t>(diagnostics::Counter::Refused);
      const bool attempted = writes > initial_writes;
      Require(after.counts[unknown] == before.counts[unknown] + (attempted ? 1 : 0) &&
                  after.counts[refused] == before.counts[refused] + (attempted ? 0 : 1),
              "selection policy drift retains the exact refused or unknown acknowledgement");
      h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 ", h.a.caret);
      h.a.hook = nullptr;
      Require(!h.Key(L'y', false) && h.a.reads == reads && h.a.writes == writes &&
                  h.a.starts == starts && h.a.ends == ends &&
                  h.a.calls["SetSelection"] == selections,
              "selection policy loss forbids later readback replay repair or replan");
    }
  }
}
void FinishSelectionOwnedScope() {
  NotepadFieldSource ordinary;
  Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
  h.Type(L"ghbdtn ");
  h.a.caret = 6;
  h.a.selection_length = 10;
  const auto writes = h.a.writes, ends = h.a.ends, queries = h.a.calls["GetSelection"];
  const auto before = DiagnosticSnapshot(*h.service.Get());
  int reads = -1;
  h.a.hook = [&](const char *call) {
    if (!strcmp(call, "GetSelection") && h.a.calls["GetSelection"] == queries + 3) {
      h.a.excluded_scope = Span{7, 7};
      reads = h.a.reads;
    }
  };
  Require(!h.Key(L'x', false) && reads >= 0 && h.a.reads == reads && h.a.writes == writes &&
              h.a.ends == ends && h.a.starts == 1 && h.a.active_composition && h.service->stopped(),
          "ordinary original selection cannot authorize an excluded owned write range");
  const auto after = DiagnosticSnapshot(*h.service.Get());
  constexpr auto unknown = static_cast<size_t>(diagnostics::Counter::Unknown);
  constexpr auto refused = static_cast<size_t>(diagnostics::Counter::Refused);
  Require(after.counts[unknown] == before.counts[unknown] &&
              after.counts[refused] == before.counts[refused] + 1 && h.a.selection_length == 10,
          "owned-range scope drift refuses before write and preserves exact original selection");
  h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 ", 6);
}
void WritablePolicyResults() {
  for (const auto result : {S_FALSE, E_FAIL}) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.a.status_result = result;
    Require(!h.Key(L'g', false) && h.a.calls["GetSelection"] == 0,
            "context status requires exact S_OK before raw selection");
    NoAdmissionEffects(h);
  }
  for (bool thread : {false, true})
    for (int failure = 0; failure != 6; ++failure) {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      auto &manager_result =
          thread ? h.manager.compartment_manager_result : h.a.compartment_manager_result;
      auto &query_result =
          thread ? h.manager.compartment_query_result : h.a.compartment_query_result;
      auto &manager_present =
          thread ? h.manager.compartment_manager_present : h.a.compartment_manager_present;
      auto &present = thread ? h.manager.compartment_present : h.a.compartment_present;
      if (failure == 0)
        manager_result = S_FALSE;
      if (failure == 1)
        manager_result = E_NOINTERFACE;
      if (failure == 2)
        manager_present = false;
      if (failure == 3)
        query_result = S_FALSE;
      if (failure == 4)
        query_result = E_ACCESSDENIED;
      if (failure == 5)
        present = false;
      Require(!h.Key(L'g', false) && h.a.calls["GetSelection"] == 0,
              "missing malformed or failed compartment interfaces stop before selection");
      NoAdmissionEffects(h);
    }
  struct CompartmentValue {
    HRESULT result;
    VARTYPE type;
    bool allowed;
  };
  for (int compartment = 0; compartment != 3; ++compartment)
    for (const auto value :
         {CompartmentValue{S_OK, VT_EMPTY, true}, CompartmentValue{S_OK, VT_I4, true},
          CompartmentValue{S_FALSE, VT_EMPTY, true}, CompartmentValue{S_FALSE, VT_I4, false},
          CompartmentValue{S_OK, VT_BOOL, false}, CompartmentValue{S_OK, VT_UNKNOWN, false},
          CompartmentValue{E_FAIL, VT_EMPTY, false},
          CompartmentValue{E_ACCESSDENIED, VT_I4, false}}) {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      if (compartment == 2) {
        h.manager.compartment_result = value.result;
        h.manager.compartment_type = value.type;
      } else {
        h.a.compartment_result[compartment] = value.result;
        h.a.compartment_type[compartment] = value.type;
      }
      Require(h.Key(L'g', false) == value.allowed,
              "only exact valid zero or legitimate unset compartments permit editing");
      if (value.allowed) {
        Require(h.a.starts == 1 && h.a.writes == 1 && !h.service->stopped(),
                "legitimate compartment signatures preserve the owned executor");
        h.Oracle(L"g", 8);
      } else {
        Require(h.a.calls["GetSelection"] == 0,
                "invalid compartment signatures stop before raw selection");
        NoAdmissionEffects(h);
      }
    }
}
void FinishReplacementFailures() {
  for (int failure = 0; failure != 10; ++failure) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.Type(L"hello");
    const auto before = DiagnosticSnapshot(*h.service.Get());
    const auto writes = h.a.writes, ends = h.a.ends, starts = h.a.starts;
    int reads_at_write = -1;
    h.a.hook = [&](const char *call) {
      if (strcmp(call, "SetText.after") != 0)
        return;
      reads_at_write = h.a.reads;
      switch (failure) {
      case 0:
        h.a.write_result = E_FAIL;
        break;
      case 1:
        h.a.text[7] = L'!';
        break;
      case 2:
        ++h.a.composition->span->length;
        break;
      case 3:
        h.a.range_context = &h.b;
        break;
      case 4:
        h.a.status = TF_SD_READONLY;
        break;
      case 5:
        h.a.application_scope.value_result = E_ACCESSDENIED;
        break;
      case 6:
        h.service->OnPushContext(&h.b);
        break;
      case 7:
        h.a.active_composition = false;
        h.service->OnCompositionTerminated(123, h.a.composition.Get());
        break;
      case 8:
        Require(!h.Key(L'x', false), "nested input passes during attempted FINISH");
        break;
      case 9:
        h.a.end_result = E_FAIL;
        break;
      }
    };
    const kr_event finish{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}};
    Require(!h.service->HandleEvent(&h.a, finish) && reads_at_write >= 0 &&
                h.a.writes == writes + 1 && h.a.ends == ends + (failure == 9 ? 1 : 0) &&
                h.a.starts == starts && h.service->stopped(),
            "unknown unchanged FINISH passes without another composition or character write");
    constexpr auto unknown = static_cast<size_t>(diagnostics::Counter::Unknown);
    const auto after = DiagnosticSnapshot(*h.service.Get());
    Require(after.counts[unknown] == before.counts[unknown] + 1,
            "failed FINISH replacement or end records one unknown outcome");
    if (failure == 4 || failure == 5)
      Require(h.a.reads == reads_at_write, "post-write exclusion forbids further character reads");
    h.Oracle(failure == 1 ? L"!ello" : L"hello", 12);
    Require(h.a.selection_length == 0, "unknown unchanged FINISH preserves public selection");
    h.a.hook = nullptr;
    const auto reads = h.a.reads;
    Require(!h.Key(L'y', false) && h.a.reads == reads && h.a.writes == writes + 1 &&
                h.a.starts == starts && h.a.ends == ends + (failure == 9 ? 1 : 0),
            "unknown FINISH never performs later readback replay or repair");
    h.Oracle(failure == 1 ? L"!ello" : L"hello", 12);
  }
}
void EmptyStartExclusion() {
  for (bool readonly : {false, true}) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    const auto before = DiagnosticSnapshot(*h.service.Get());
    int reads_at_start = -1;
    h.a.hook = [&](const char *call) {
      if (reads_at_start >= 0 || strcmp(call, "composition.QI") != 0)
        return;
      reads_at_start = h.a.reads;
      if (readonly)
        h.a.status = TF_SD_READONLY;
      else
        h.a.application_scope.value_result = E_ACCESSDENIED;
    };
    Require(!h.Key(L'g', false) && reads_at_start >= 0 && h.a.reads == reads_at_start &&
                h.a.starts == 1 && h.a.active_composition && h.a.writes == 0 && h.a.ends == 0 &&
                h.service->stopped(),
            "exclusion after empty composition start stops before character access or effects");
    constexpr auto unknown = static_cast<size_t>(diagnostics::Counter::Unknown);
    constexpr auto refused = static_cast<size_t>(diagnostics::Counter::Refused);
    const auto after = DiagnosticSnapshot(*h.service.Get());
    Require(after.counts[unknown] == before.counts[unknown] &&
                after.counts[refused] == before.counts[refused] + 1,
            "empty start without a character write retains refused-before-write classification");
    h.Oracle(L"", 7);
    h.a.hook = nullptr;
    Require(!h.Key(L'x', false) && h.a.reads == reads_at_start && h.a.starts == 1 &&
                h.a.writes == 0 && h.a.ends == 0 && h.service->stopped(),
            "empty-start refusal cannot resume ownership replay or repair on later input");
    h.Oracle(L"", 7);
  }
}
void FreshPolicyBoundaries() {
  struct Boundary {
    const char *operation, *preparation;
    bool owned, finish;
  };
  for (const auto boundary : {Boundary{"GetText", "ShiftStart", false, false},
                              Boundary{"GetText", "GetExtent", true, false},
                              Boundary{"StartComposition", "enumerator.Next", false, false},
                              Boundary{"SetText.before", "GetExtent", true, false},
                              Boundary{"SetSelection", "Collapse", true, false},
                              Boundary{"EndComposition", "GetText.after", true, true}}) {
    const auto event =
        boundary.finish ? kr_event{KR_FINISH_REQUEST, KR_EN, KR_CTX_INELIGIBLE, 0, {}} : Text(L'b');
    size_t preparation = 0;
    {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      if (boundary.owned)
        h.Type(L"hello");
      h.a.trace.clear();
      h.service->HandleEvent(&h.a, event);
      const auto operation = std::find(h.a.trace.begin(), h.a.trace.end(), boundary.operation);
      Require(operation != h.a.trace.end(), "positive schedule reaches the protected boundary");
      auto found = operation;
      while (found != h.a.trace.begin() && *found != boundary.preparation)
        --found;
      Require(*found == boundary.preparation,
              "protected boundary has callback-capable preparation");
      preparation = static_cast<size_t>(found - h.a.trace.begin());
    }
    for (int policy = 0; policy != 8; ++policy) {
      NotepadFieldSource ordinary;
      Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
      if (boundary.owned)
        h.Type(L"hello");
      const auto before = DiagnosticSnapshot(*h.service.Get());
      const auto initial_writes = h.a.writes;
      bool injected = false;
      int reads = -1, writes = -1, starts = -1, ends = -1, selections = -1;
      h.a.trace.clear();
      h.a.hook = [&](const char *) {
        if (injected || h.a.trace.size() != preparation + 1)
          return;
        injected = true;
        reads = h.a.reads;
        writes = h.a.writes;
        starts = h.a.starts;
        ends = h.a.ends;
        selections = h.a.calls["SetSelection"];
        switch (policy) {
        case 0:
          h.a.status = TF_SD_READONLY;
          break;
        case 1:
          h.a.disabled = true;
          break;
        case 2:
          h.a.empty_disabled = true;
          break;
        case 3:
          h.manager.disabled = true;
          break;
        case 4:
          h.a.scope = IS_PRIVATE;
          break;
        case 5:
          h.a.application_scope.value_result = E_ACCESSDENIED;
          break;
        case 6:
          ordinary.facts.field.style |= ES_READONLY;
          break;
        case 7:
          h.service->OnPushContext(&h.b);
          break;
        }
      };
      const bool eaten = h.service->HandleEvent(&h.a, event);
      const bool attempted = writes > initial_writes;
      Require(injected && eaten == (attempted && !boundary.finish) && h.a.reads == reads &&
                  h.a.writes == writes && h.a.starts == starts && h.a.ends == ends &&
                  h.a.calls["SetSelection"] == selections,
              "fresh policy drift blocks further character access and effects after preparation");
      constexpr auto unknown = static_cast<size_t>(diagnostics::Counter::Unknown);
      const auto after = DiagnosticSnapshot(*h.service.Get());
      Require(after.counts[unknown] == before.counts[unknown] + (attempted ? 1 : 0) &&
                  (!attempted || h.service->stopped()),
              "post-effect policy exclusion is unknown and stops correction");
      h.Oracle(attempted && !boundary.finish ? L"hellob"
               : boundary.owned              ? L"hello"
                                             : L"",
               boundary.owned ? 12 : 7);
      Require(h.a.selection_length == 0, "policy drift leaves the exact public selection");
      h.a.hook = nullptr;
      if (policy != 7 || boundary.owned)
        Require(!h.Key(L'x', false) && h.a.reads == reads && h.a.writes == writes &&
                    h.a.starts == starts && h.a.ends == ends,
                "policy refusal or unknown never replays or repairs on the next key");
    }
  }
}
void NotepadReplanAdmission() {
  for (int scenario = 0; scenario != 3; ++scenario) {
    NotepadFieldSource ordinary;
    Harness h(nullptr, GetKeyboardLayoutList, &ordinary);
    h.Type(L"ghbdtn ");
    const auto writes = h.a.writes;
    bool ended = false, readmitted = false;
    int acp_at_scope = -1, reads_at_scope = -1;
    h.a.hook = [&](const char *call) {
      if (strcmp(call, "EndComposition.after") == 0) {
        ended = true;
        if (scenario == 0)
          h.a.application_scope.value_result = E_ACCESSDENIED;
        if (scenario == 1)
          h.a.status = TF_SD_READONLY;
        if (scenario == 2)
          h.manager.disabled = true;
      }
      if (ended && strcmp(call, "GetStatus") == 0)
        readmitted = true;
      if (ended && strcmp(call, "GetAppProperty") == 0) {
        acp_at_scope = h.a.calls["range.ACP"];
        reads_at_scope = h.a.reads;
      }
    };
    Require(!h.Key(L'x', false) && ended && readmitted && h.a.starts == 1 &&
                h.a.writes == writes + 1 && h.service->stopped(),
            "post-finish exclusion stops before replan and passes the next key");
    if (scenario == 0)
      Require(acp_at_scope >= 0 && h.a.calls["range.ACP"] == acp_at_scope &&
                  h.a.reads == reads_at_scope,
              "replan scope failure performs no subsequent ACP or character access");
    h.Oracle(L"\x043f\x0440\x0438\x0432\x0435\x0442 ", 14);
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
  Require(h.a.max_request_writes == 2, "continuation performs one FINISH then one MARK write");
  std::vector<OperationTrace> continuation;
  std::copy_if(
      h.a.operations.begin(), h.a.operations.end(), std::back_inserter(continuation),
      [&](const OperationTrace &operation) { return operation.request == h.a.request_ordinal; });
  Require(
      continuation.size() == 4 && continuation[0].operation == NativeOperation::SetText &&
          continuation[0].composition == 1 && continuation[0].start == 7 &&
          continuation[0].length == 7 && continuation[0].replacement_length == 7 &&
          continuation[1].operation == NativeOperation::End && continuation[1].composition == 1 &&
          continuation[2].operation == NativeOperation::Start && continuation[2].composition == 2 &&
          continuation[2].start == 14 && continuation[2].length == 0 &&
          continuation[3].operation == NativeOperation::SetText &&
          continuation[3].composition == 2 && continuation[3].start == 14 &&
          continuation[3].length == 0 && continuation[3].replacement_length == 1,
      "continuation attributes each owned write to a distinct composition plan in order");
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
void DriftAndUnknown(const native_field::Source *field_source = nullptr) {
  for (int scenario = 0; scenario != 5; ++scenario) {
    Harness h(nullptr, GetKeyboardLayoutList, field_source);
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
    Harness h(nullptr, GetKeyboardLayoutList, field_source);
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
    Require(h.a.writes == before + 1 && h.service->stopped(),
            "finish unknown makes one owned replacement without text repair");
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
  Require(!moved.Key(L'x', false) && moved.a.writes == before + 1 && !moved.a.active_composition,
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
  HKL en = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04090409));
  HKL ru = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04190419));
  auto translate = [&](WPARAM key, LPARAM flags, HKL layout) {
    auto translated = TranslateKey(key, flags, layout, state);
    Require(std::holds_alternative<kr_event>(translated), "ordinary key produces a typed event");
    return std::get<kr_event>(translated);
  };
  Require(translate('G', 0x220001, en).units[0] == L'g', "EN actual layout translation");
  state[VK_SHIFT] = 0x80;
  Require(translate('G', 0x220001, en).units[0] == L'G', "Shift preserved");
  state[VK_SHIFT] = 0;
  state[VK_CAPITAL] = 1;
  Require(translate('G', 0x220001, en).units[0] == L'G', "Caps Lock preserved");
  state[VK_CAPITAL] = 0;
  // RU may not be installed. No utility loads or selects it during tests.
  HKL layouts[64]{};
  int count = GetKeyboardLayoutList(64, layouts);
  bool russian = std::find(layouts, layouts + count, ru) != layouts + count;
  if (russian)
    Require(translate('H', 0x230001, ru).units[0] == L'\x0440', "RU actual layout translation");
  else
    puts("SKIP: RU layout translation runtime check. Layout is not already loaded.");
  state[VK_CONTROL] = 0x80;
  Require(translate('G', 0x220001, en).role == KR_FINISH_REQUEST,
          "shortcut not interpreted as text");
  state[VK_CONTROL] = 0;
  Harness repeat;
  repeat.Type(L"gh");
  auto event = translate('G', 0x220003, en);
  Require(event.role == KR_FINISH_REQUEST && !repeat.service->HandleEvent(&repeat.a, event) &&
              !repeat.a.active_composition,
          "coalesced repeat finishes owned mark and passes original message intact");
  Require(translate('G', 0x220000, en).role == KR_FINISH_REQUEST,
          "malformed zero repeat count passes");
  struct Oem {
    UINT key;
    const wchar_t *en;
    const wchar_t *ru;
  };
  const Oem oem[] = {
      {VK_OEM_1, L";:", L"жЖ"},     {VK_OEM_PLUS, L"=+", L"=+"},   {VK_OEM_COMMA, L",<", L"бБ"},
      {VK_OEM_MINUS, L"-_", L"-_"}, {VK_OEM_PERIOD, L".>", L"юЮ"}, {VK_OEM_2, L"/?", L".,"},
      {VK_OEM_3, L"`~", L"ёЁ"},     {VK_OEM_4, L"[{", L"хХ"},      {VK_OEM_5, L"\\|", L"\\/"},
      {VK_OEM_6, L"]}", L"ъЪ"},     {VK_OEM_7, L"'\"", L"эЭ"},     {VK_OEM_102, L"\\|", L"\\/"}};
  for (HKL layout : {en, ru}) {
    if (layout == ru && !russian)
      continue;
    for (const auto &entry : oem) {
      for (bool shift : {false, true}) {
        for (bool caps : {false, true}) {
          state[VK_SHIFT] = shift ? 0x80 : 0;
          state[VK_CAPITAL] = caps ? 1 : 0;
          auto translated = translate(entry.key, 1, layout);
          const auto *expected = layout == en ? entry.en : entry.ru;
          bool cased = expected[0] != expected[1] && expected[0] >= L'а' && expected[0] <= L'ё';
          auto index = layout == ru && cased ? shift != caps : shift;
          Require(translated.role == KR_TEXT && translated.len == 1 &&
                      translated.units[0] == expected[index],
                  "every PC OEM key preserves actual layout Shift and Caps translation");
        }
      }
    }
  }
  state[VK_SHIFT] = state[VK_CAPITAL] = 0;
  for (WPARAM key : {static_cast<WPARAM>(VK_TAB), static_cast<WPARAM>(VK_RETURN),
                     static_cast<WPARAM>(VK_F12), static_cast<WPARAM>(VK_DELETE)})
    Require(translate(key, 1, en).role == KR_FINISH_REQUEST,
            "control and nontext keys remain native finish requests");
  for (WPARAM key :
       {static_cast<WPARAM>(VK_CONTROL), static_cast<WPARAM>(VK_LCONTROL),
        static_cast<WPARAM>(VK_RCONTROL), static_cast<WPARAM>(VK_MENU),
        static_cast<WPARAM>(VK_LMENU), static_cast<WPARAM>(VK_RMENU), static_cast<WPARAM>(VK_SHIFT),
        static_cast<WPARAM>(VK_LSHIFT), static_cast<WPARAM>(VK_RSHIFT),
        static_cast<WPARAM>(VK_LWIN), static_cast<WPARAM>(VK_RWIN)})
    Require(std::holds_alternative<ModifierOnly>(TranslateKey(key, 1, en, state)),
            "modifier-only result has no Rust event");
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
void OemCommandSequences() {
  for (bool shifted : {false, true}) {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Set(VK_LSHIFT, shifted ? 0x80 : 0);
    Require(keyboard.Down(h, VK_OEM_4), "provisional bracket starts an owned word");
    keyboard.Set(VK_LSHIFT, 0);
    keyboard.Type(h, "JHJIJ");
    Require(keyboard.Chord(h, 'K'), "OEM provisional word reaches shared Rust manual conversion");
    h.Oracle(shifted ? L"Хорошо" : L"хорошо", 13);
    Require(keyboard.Chord(h, 'Z'), "OEM reversal restores exact original punctuation and case");
    h.Oracle(shifted ? L"{jhjij" : L"[jhjij", 13);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    Require(keyboard.Down(h, VK_OEM_3), "PC backtick starts provisional word");
    keyboard.Type(h, "KRF");
    Require(keyboard.Chord(h, 'K'), "PC backtick maps through shared Rust");
    h.Oracle(L"ёлка", 11);
    Require(keyboard.Chord(h, 'Z'), "PC backtick reversal restores exact original");
    h.Oracle(L"`krf", 11);
  }
  {
    ProfileManager profile;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    keyboard.Type(h, "GHBDTN");
    Require(!keyboard.Down(h, VK_OEM_MINUS) && !h.a.active_composition,
            "ordinary punctuation finishes and passes unchanged");
    h.a.text.insert(h.a.caret++, 1, L'-');
    h.Oracle(L"ghbdtn-", 14);
  }
  HKL ru = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04190419));
  HKL loaded[64]{};
  int count = GetKeyboardLayoutList(64, loaded);
  if (std::find(loaded, loaded + count, ru) != loaded + count) {
    ProfileManager profile;
    profile.profile.guidProfile = profiles[1].id;
    profile.profile.langid = profiles[1].language;
    profile.substitute = ru;
    Harness h(&profile, ProofLayouts);
    KeyboardState keyboard;
    Require(keyboard.Down(h, VK_OEM_4), "Russian physical bracket key types owned х");
    Require(keyboard.Chord(h, 'K'), "Russian manual conversion maps х to bracket");
    h.Oracle(L"[", 8);
    int writes = h.a.writes;
    Require(keyboard.Chord(h, 'K') && h.a.writes == writes && h.a.active_composition,
            "acknowledged manual no-op reserves command and preserves first receipt");
    Require(keyboard.Chord(h, 'Z'), "reverse after manual no-op retains original receipt");
    h.Oracle(L"х", 8);
  }
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
#include "runtime_schedules.h"
} // namespace
int wmain(int argc, wchar_t **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  puts("RUN native denial fixture gates");
  NativeDenialGateSchedules();
  Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "STA initialized");
  if (argc == 2 && wcscmp(argv[1], L"--runtime-files") == 0) {
    RuntimeFileSchedules();
    CoUninitialize();
    return 0;
  }
  if (argc == 3 && wcscmp(argv[1], L"--runtime-dll") == 0) {
    RuntimeDllSchedules(argv[2]);
    CoUninitialize();
    puts("Actual DLL runtime and unload schedules passed.");
    return 0;
  }
  Require(argc == 1, "known test mode");
  RuntimeEnvironment fixture_policy(L"KEYRILL_WINDOWS_POLICY", nullptr);
  RuntimeEnvironment fixture_en(L"KEYRILL_WINDOWS_EN_DICTIONARY", nullptr);
  RuntimeEnvironment fixture_ru(L"KEYRILL_WINDOWS_RU_DICTIONARY", nullptr);
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
  HostMetadataSchedules();
  HostPublicationEquivalence();
  DiagnosticCodecSchedules();
  LifecycleCodecSchedules();
  LifecycleServiceSchedules();
  puts("RUN retained owned shutdown proof schedules");
  ShutdownPredicateSchedules();
  ShutdownProbeSchedules();
  ShutdownServiceSchedules();
  ShutdownReaderLifetimeSchedules();
  ShutdownWindowSchedules();
  puts("RUN host-only native source proof schedules");
  SourceProbeSchedules();
  SourceReadinessSchedules();
  SourceReviewRegressionSchedules();
  SourceHistorySchedules();
  SourceReentrySchedules();
  SourceServiceSchedules();
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
  puts("RUN version-bounded Notepad native admission");
  NotepadNativeAdmission();
  puts("RUN Notepad scope admission");
  NotepadScopeAdmission();
  puts("RUN Notepad existing executor refusals");
  NotepadExecutorRefusals();
  puts("RUN fresh Notepad authority before text");
  NotepadFieldDriftBeforeText();
  puts("RUN Notepad replan admission");
  FinishContractRegressions();
  puts("RUN unchanged finish client selection effects");
  const bool unchanged_finish_selection = UnchangedFinishSelectionEffects();
  FinishSelectionGravity();
  FinishSelectionFailures();
  FinishSelectionPolicyBoundaries();
  FinishSelectionOwnedScope();
  WritablePolicyResults();
  FinishReplacementFailures();
  EmptyStartExclusion();
  FreshPolicyBoundaries();
  NotepadReplanAdmission();
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
  {
    NotepadFieldSource ordinary;
    DriftAndUnknown(&ordinary);
  }
  puts("RUN finish outcomes");
  FinishOutcomes();
  puts("RUN focus and termination");
  FocusAndTermination();
  puts("RUN reentry boundaries");
  ReentrantBoundaries();
  puts("RUN keyboard translation");
  Keyboard();
  puts("RUN actual keydown command sequences");
  CommandKeySequences();
  OemCommandSequences();
  puts("RUN teardown and constructor exceptions");
  TeardownAndConstructorExceptions();
  puts("RUN real vocabulary filesystem schedules");
  RuntimeFileSchedules();
  puts("RUN actual service model and vocabulary schedules");
  RuntimeServiceSchedules();
  Require(unchanged_finish_selection,
          "unchanged FINISH preserves selection and continuation aggregate");
  puts("Existing controlled same-service COM schedules passed.");
  puts("RUN activation-registered preserved command sequences");
  const bool preserved_commands = PreservedCommandSequences();
  PreservedActivationSchedules();
  PreservedDispatchSchedules();
  DedicatedLayoutNegatives(true);
  DedicatedLayoutReentry(true);
  DestroyWindow(controlled_root);
  CoUninitialize();
  Require(preserved_commands, "activation-registered preserved command aggregate");
  puts("Controlled same-service COM schedules passed. No installed service or physical TSF "
       "routing tested.");
  return 0;
}
