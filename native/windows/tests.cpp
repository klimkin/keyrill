#include "fixture.h"
#include "service.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <vector>

using namespace keyrill;
namespace {
void Require(bool condition, const char *label) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", label);
    std::exit(1);
  }
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
  REFCOUNT;
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
  bool foreign = false;
  bool reject_composition = false;
  bool callback = true;
  bool throw_after_callback = false;
  HRESULT request_result = S_OK;
  HRESULT session_result = S_OK;
  HRESULT write_result = S_OK;
  HRESULT end_result = S_OK;
  bool keep_composition = false;
  bool active_composition = false;
  int reads = 0, writes = 0, starts = 0, ends = 0;
  int max_request_writes = 0;
  int references = 1;
  ComPtr<Composition> composition;
  ComPtr<ITfEditSession> retained_request;
  std::map<std::string, int> calls;
  std::function<void(const char *)> hook;
  void Call(const char *name) {
    ++calls[name];
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
    else if (id == IID_ITfInputScope)
      *out = static_cast<ITfInputScope *>(this);
    else if (id == IID_ITfCompartmentMgr)
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
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView **out) override {
    Call("GetActiveView");
    *out = static_cast<ITfContextView *>(this);
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
  HRESULT STDMETHODCALLTYPE GetCompartment(REFGUID, ITfCompartment **out) override {
    Call("GetCompartment");
    *out = static_cast<ITfCompartment *>(this);
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetValue(VARIANT *out) override {
    Call("compartment.GetValue");
    VariantInit(out);
    out->vt = VT_I4;
    out->lVal = disabled;
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
  if (id == IID_IUnknown || id == IID_ITfComposition)
    *out = static_cast<ITfComposition *>(this);
  else if (id == IID_ITfCompositionView)
    *out = static_cast<ITfCompositionView *>(this);
  if (!*out)
    return E_NOINTERFACE;
  AddRef();
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
                       ITfKeystrokeMgr,
                       ITfCompartmentMgr,
                       ITfCompartment {
  Document *top = nullptr;
  bool disabled = false;
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
  STUB(GetFunctionProvider, (REFCLSID, ITfFunctionProvider **));
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
struct Harness {
  HWND root = nullptr;
  Document a, b;
  Manager manager;
  ComPtr<Service> service;
  Harness() {
    root = controlled_root;
    Require(root != nullptr, "controlled RichEdit metadata fixture created");
    a.window = GetDlgItem(root, 101);
    b.window = GetDlgItem(root, 102);
    manager.top = &a;
    ::SetActiveWindow(root);
    ::SetFocus(a.window);
    Require(::GetFocus() == a.window, "fixture native focus established");
    service.Attach(new Service);
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
  Require(ProfileLayout(profile, KR_EN) == en,
          "synthetic TIP handle uses validated actual substitute");
  Require(!ProfileLayout(profile, KR_RU), "mismatched selected profile mode passes");
  profile.hklSubstitute = ru;
  Require(!ProfileLayout(profile, KR_EN), "mismatched substitute cannot translate keys");
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
  Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "STA initialized");
  puts("RUN fixture input scope lifecycle");
  FixtureLifecycle();
  controlled_root = CreateFixture(GetModuleHandleW(nullptr), FixtureProc, false);
  Require(controlled_root != nullptr, "controlled native metadata fixture");
  puts("RUN default application input scope correction");
  ApplicationInputScope(IS_DEFAULT);
  puts("RUN text application input scope correction");
  ApplicationInputScope(IS_TEXT);
  puts("RUN uncertain application input scope");
  UncertainApplicationInputScope();
  puts("RUN correction and reversal");
  CorrectionAndReversal();
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
