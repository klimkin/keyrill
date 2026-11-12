#include "../test_api.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <msctf.h>
#include <string_view>
#include <thread>

namespace {
void Require(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "FAILED %s\n", message);
    std::exit(1);
  }
}

struct Host final : ITfContext {
  ULONG references = 1;
  ULONG queries = 0, adds = 0, releases = 0, requests = 0;
  HRESULT query_result = S_OK, request_result = S_OK, callback_result = S_OK;
  bool null_query = false, invoke = true, repeat = false;
  unsigned query_throw = 0, request_throw = 0, add_throw = 0, release_throw = 0;
  ITfEditSession *retained = nullptr;
  std::function<void()> hook;

  ~Host() {
    if (retained)
      retained->Release();
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **output) override {
    ++queries;
    *output = nullptr;
    if (hook)
      hook();
    if (query_throw == 1)
      throw 1;
    if (id != IID_IUnknown && id != IID_ITfContext)
      return E_NOINTERFACE;
    if (!null_query && query_result == S_OK) {
      *output = static_cast<ITfContext *>(this);
      AddRef();
    }
    if (query_throw == 2)
      throw 2;
    return query_result;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    ++adds;
    if (add_throw == 1)
      throw 1;
    const auto count = ++references;
    if (add_throw == 2)
      throw 2;
    return count;
  }
  ULONG STDMETHODCALLTYPE Release() override {
    ++releases;
    if (hook)
      hook();
    if (release_throw == 1)
      throw 1;
    const auto count = --references;
    if (release_throw == 2)
      throw 2;
    return count;
  }
  HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId client, ITfEditSession *session,
                                               DWORD flags, HRESULT *result) override {
    Require(client == 42 && flags == (TF_ES_SYNC | TF_ES_READWRITE), "exact request flags");
    ++requests;
    if (hook)
      hook();
    if (request_throw == 1)
      throw 1;
    if (retained)
      retained->Release();
    retained = session;
    retained->AddRef();
    if (invoke) {
      callback_result = session->DoEditSession(123);
      if (repeat)
        Require(session->DoEditSession(123) == E_FAIL, "repeated callback is refused");
    }
    *result = callback_result;
    if (request_throw == 2)
      throw 2;
    return request_result;
  }
#define STUB(name, args)                                                                           \
  HRESULT STDMETHODCALLTYPE name args override { return E_NOTIMPL; }
  STUB(InWriteSession, (TfClientId, BOOL *));
  STUB(GetSelection, (TfEditCookie, ULONG, ULONG, TF_SELECTION *, ULONG *));
  STUB(SetSelection, (TfEditCookie, ULONG, const TF_SELECTION *));
  STUB(GetStart, (TfEditCookie, ITfRange **));
  STUB(GetEnd, (TfEditCookie, ITfRange **));
  STUB(GetActiveView, (ITfContextView **));
  STUB(EnumViews, (IEnumTfContextViews **));
  STUB(GetStatus, (TF_STATUS *));
  STUB(GetProperty, (REFGUID, ITfProperty **));
  STUB(GetAppProperty, (REFGUID, ITfReadOnlyProperty **));
  STUB(TrackProperties, (const GUID **, ULONG, const GUID **, ULONG, ITfReadOnlyProperty **));
  STUB(EnumProperties, (IEnumTfProperties **));
  STUB(GetDocumentMgr, (ITfDocumentMgr **));
  STUB(CreateRangeBackup, (TfEditCookie, ITfRange *, ITfRangeBackup **));
#undef STUB
};

struct Api {
  HMODULE module;
  HRESULT(WINAPI *unload)();
  KrTestCreate create;
  KrTestRelease release;
  KrTestPanic panic;
  KrTestStopped stopped;
  KrTestProbe probe;
  KrTestEvent event;
  KrTestMaintain maintain;

  explicit Api(const wchar_t *path) : module(LoadLibraryW(path)) {
    Require(module != nullptr, "load Rust test DLL");
    unload = Load<decltype(unload)>("DllCanUnloadNow");
    create = Load<KrTestCreate>("KeyrillTestCreate");
    release = Load<KrTestRelease>("KeyrillTestRelease");
    panic = Load<KrTestPanic>("KeyrillTestPanic");
    stopped = Load<KrTestStopped>("KeyrillTestStopped");
    probe = Load<KrTestProbe>("KeyrillTestProbe");
    event = Load<KrTestEvent>("KeyrillTestEvent");
    maintain = Load<KrTestMaintain>("KeyrillTestMaintain");
  }
  template <typename T> T Load(const char *name) {
    const auto function = reinterpret_cast<T>(GetProcAddress(module, name));
    Require(function != nullptr, name);
    return function;
  }
};

struct Instance {
  Api &api;
  void *handle = nullptr;
  ITfTextInputProcessorEx *service = nullptr;
  explicit Instance(Api &value, const KrWindowsDependencies *dependencies = nullptr) : api(value) {
    Require(api.create(KR_WINDOWS_TEST_VERSION, dependencies, &handle,
                       reinterpret_cast<void **>(&service)) == S_OK,
            "versioned test creation");
  }
  ~Instance() {
    if (handle)
      Require(api.release(handle) == S_OK, "test handle release");
    if (service)
      service->Release();
  }
  KrWindowsProbe Probe(Host &host, uint32_t operation) {
    KrWindowsProbe value{};
    Require(api.probe(handle, &host, operation, sizeof(value), &value) == S_OK,
            "probe transport succeeded");
    Require(value.size == sizeof(value) && value.version == KR_WINDOWS_TEST_VERSION,
            "probe ABI size/version");
    return value;
  }
};

struct DependencyOwner {
  uint32_t references = 1;
  uint32_t retained = 0, released = 0;
  static void WINAPI Retain(void *context) {
    auto &owner = *static_cast<DependencyOwner *>(context);
    ++owner.references;
    ++owner.retained;
  }
  static void WINAPI Release(void *context) {
    auto &owner = *static_cast<DependencyOwner *>(context);
    --owner.references;
    ++owner.released;
  }
  KrWindowsDependencies Table() {
    KrWindowsDependencies result{};
    result.size = sizeof(result);
    result.version = KR_WINDOWS_TEST_VERSION;
    result.context = this;
    result.retain = Retain;
    result.release = Release;
    return result;
  }
};

void DependencyLifetime(Api &api) {
  DependencyOwner owner;
  auto dependencies = owner.Table();
  void *invalid_handle = reinterpret_cast<void *>(1);
  void *invalid_object = reinterpret_cast<void *>(1);
  dependencies.size = 8;
  Require(api.create(KR_WINDOWS_TEST_VERSION, &dependencies, &invalid_handle, &invalid_object) ==
                  E_INVALIDARG &&
              !invalid_handle && !invalid_object && owner.references == 1,
          "invalid dependency table refuses before retention and clears outputs");
  dependencies = owner.Table();
  {
    Instance instance(api, &dependencies);
    Require(owner.references == 2 && owner.retained == 1, "dependency table retained once");
    Host host;
    kr_event event{KR_TEXT, KR_EN, KR_CTX_BOUNDARY, 1, {L'g'}};
    BOOL eaten = TRUE;
    Require(api.event(instance.handle, &host, &event, &eaten) == S_OK && !eaten && !host.queries &&
                !host.requests,
            "inactive direct-event transport refuses without host inspection");
    Require(api.maintain(instance.handle) == S_OK, "maintenance enters shared service");
    const auto result = instance.Probe(host, 1);
    Require(result.entered && owner.references == 2, "frame retains dependency lifetime");
    instance.service->Release();
    instance.service = nullptr;
    Require(api.unload() == S_FALSE, "test handle and retained callback prevent unload");
    Require(api.release(instance.handle) == S_OK, "release transport owner before late callback");
    instance.handle = nullptr;
    Require(owner.references == 2 && api.unload() == S_FALSE,
            "captured frame retains service and dependency after external owners drain");
    Require(host.retained->DoEditSession(123) == E_FAIL, "expired retained callback stays refused");
    host.retained->Release();
    host.retained = nullptr;
  }
  Require(owner.references == 1 && owner.released == 1 && api.unload() == S_OK,
          "dependency context drains after last callback");
}

void PureNative(Api &api) {
  const auto translate = api.Load<KrTestTranslate>("KeyrillTestTranslate");
  const auto exact = api.Load<KrTestExactCommandModifiers>("KeyrillTestExactCommandModifiers");
  const auto binding = api.Load<KrTestBinding>("KeyrillTestBinding");
  const auto match = api.Load<KrTestMatch>("KeyrillTestMatch");
  BYTE state[256]{};
  kr_event event{};
  BOOL modifier = TRUE;
  const auto en = reinterpret_cast<HKL>(static_cast<uintptr_t>(0x04090409));
  Require(translate('G', 0x220001, en, state, &event, &modifier) == S_OK && !modifier &&
              event.role == KR_TEXT && event.mode == KR_EN && event.len == 1 &&
              event.units[0] == L'g',
          "Rust SDK key translation");
  Require(translate('G', 1, nullptr, state, &event, &modifier) == S_OK &&
              event.role == KR_FINISH_REQUEST && event.mode == 0,
          "unmapped layout preserves inert finish event");
  Require(translate(VK_LMENU, 1, en, state, &event, &modifier) == S_OK && modifier,
          "modifier translation has no text event");
  state[VK_LCONTROL] = state[VK_LMENU] = 0x80;
  BOOL command = FALSE;
  Require(exact(state, &command) == S_OK && command, "exact left command modifiers");
  Require(translate('K', 1, en, state, &event, &modifier) == S_OK && event.role == KR_MANUAL,
          "manual key role comes from Rust binding");
  GUID id{};
  TF_PRESERVEDKEY key{};
  Require(binding(0, &id, &key) == S_OK && id.Data1 == 0xd7abce42 && key.uVKey == 'K' &&
              key.uModifiers == (TF_MOD_LCONTROL | TF_MOD_LALT),
          "preserved binding metadata matches SDK");
  KrWindowsFieldFacts facts{};
  facts.host = {17, 23, 1, 2, {11, 2607, 14, 0}};
  facts.process = 17;
  facts.thread = 23;
  facts.view = facts.focus = reinterpret_cast<HWND>(1);
  facts.root = facts.foreground = reinterpret_cast<HWND>(3);
  const auto node = [](KrWindowsFieldWindow &window, HWND handle, const wchar_t *name) {
    window.handle = handle;
    window.process = 17;
    window.thread = 23;
    window.enabled = window.complete = 1;
    std::copy_n(name, wcslen(name) + 1, window.class_name);
  };
  node(facts.field, facts.view, L"RichEditD2DPT");
  facts.field.style = WS_CHILD | ES_MULTILINE;
  node(facts.parent, reinterpret_cast<HWND>(2), L"NotepadTextBox");
  node(facts.grandparent, facts.root, L"Notepad");
  KrWindowsFieldIdentity identity{};
  BOOL matched = FALSE;
  Require(match(&facts, &identity, &matched) == S_OK && matched && identity.kind == 1 &&
              identity.field == facts.view && identity.parent == facts.parent.handle &&
              identity.root == facts.root,
          "Rust exact admitted-version Notepad field facts");
  facts.field.style |= ES_PASSWORD;
  Require(match(&facts, &identity, &matched) == S_OK && !matched && !identity.field,
          "secure field facts refuse with empty identity");
}

void Basic(Api &api) {
  Require(api.unload() == S_OK, "initially unloadable");
  PureNative(api);
  DependencyLifetime(api);
  {
    Instance instance(api);
    LONG objects = 0, workers = 0;
    Require(api.Load<KrTestCounts>("KeyrillTestCounts")(&objects, &workers) == S_OK &&
                objects == 1 && workers == 0,
            "one shared service counts once across COM and test owners");
    Require(api.unload() == S_FALSE, "live object prevents unload");
    ITfKeyEventSink *keys = nullptr;
    ITfFunctionProvider *provider = nullptr;
    Require(instance.service->QueryInterface(IID_PPV_ARGS(&keys)) == S_OK, "SDK key sink QI");
    Require(instance.service->QueryInterface(IID_PPV_ARGS(&provider)) == S_OK, "SDK provider QI");
    IUnknown *identity[3]{};
    Require(instance.service->QueryInterface(IID_PPV_ARGS(&identity[0])) == S_OK,
            "service identity");
    Require(keys->QueryInterface(IID_PPV_ARGS(&identity[1])) == S_OK, "key sink identity");
    Require(provider->QueryInterface(IID_PPV_ARGS(&identity[2])) == S_OK, "provider identity");
    Require(identity[0] == identity[1] && identity[1] == identity[2], "one canonical IUnknown");
    for (auto *entry : identity)
      entry->Release();
    void *agile = reinterpret_cast<void *>(1);
    Require(keys->QueryInterface(IID_IAgileObject, &agile) == E_NOINTERFACE && !agile,
            "apartment object rejects IAgileObject");
    const auto check_keys = [&](HRESULT expected_down, HRESULT expected_up) {
      for (auto method : {&ITfKeyEventSink::OnTestKeyDown, &ITfKeyEventSink::OnKeyDown,
                          &ITfKeyEventSink::OnTestKeyUp, &ITfKeyEventSink::OnKeyUp}) {
        Require((keys->*method)(nullptr, 0, 0, nullptr) == E_POINTER, "null key output refused");
        BOOL eaten = TRUE;
        const bool up =
            method == &ITfKeyEventSink::OnTestKeyUp || method == &ITfKeyEventSink::OnKeyUp;
        Require((keys->*method)(nullptr, 0, 0, &eaten) == (up ? expected_up : expected_down) &&
                    !eaten,
                "key output initialized and exact thread result");
      }
      Require(keys->OnPreservedKey(nullptr, GUID_NULL, nullptr) == E_POINTER,
              "null preserved output refused");
      BOOL eaten = TRUE;
      Require(keys->OnPreservedKey(nullptr, GUID_NULL, &eaten) == expected_down && !eaten,
              "preserved output initialized and exact thread result");
    };
    check_keys(S_OK, S_OK);
    std::thread wrong_thread([&] { check_keys(RPC_E_WRONG_THREAD, S_OK); });
    wrong_thread.join();
    Require(provider->GetType(nullptr) == E_POINTER &&
                provider->GetDescription(nullptr) == E_POINTER,
            "provider null outputs");
    GUID type{};
    Require(provider->GetType(&type) == S_OK && type.Data1 == 0x1a746212, "service provider type");
    BSTR description = reinterpret_cast<BSTR>(1);
    Require(provider->GetDescription(&description) == S_OK && description &&
                std::wstring_view(description) == L"Keyrill proof service diagnostics",
            "BSTR transfers ownership");
    SysFreeString(description);
    IUnknown *function = reinterpret_cast<IUnknown *>(1);
    Require(provider->GetFunction(GUID_NULL, IID_IUnknown, &function) == E_NOINTERFACE && !function,
            "failed GetFunction clears output");
    Require(provider->GetFunction(GUID_NULL, IID_IUnknown, nullptr) == E_POINTER,
            "null GetFunction output");
    provider->Release();
    keys->Release();

    Host host;
    auto result = instance.Probe(host, 0);
    Require(result.outer == S_OK && result.identity == reinterpret_cast<uintptr_t>(&host) &&
                host.references == 1 && host.queries == 1 && host.adds == 1 && host.releases == 1,
            "foreign QI reference balanced");
    host.null_query = true;
    Require(instance.Probe(host, 0).outer == E_UNEXPECTED, "S_OK plus null is invalid");
    for (HRESULT status : {S_FALSE, static_cast<HRESULT>(2), E_FAIL}) {
      host.query_result = status;
      Require(instance.Probe(host, 0).outer == status, "exact HRESULT survives QI");
    }
    host.hook = [&] { Require(instance.service->Deactivate() == S_OK, "QI and Release reentry"); };
    host.query_result = S_OK;
    host.null_query = false;
    Require(instance.Probe(host, 0).outer == S_OK, "native reentry does not retain a state borrow");
    host.hook = {};
    host.repeat = true;
    result = instance.Probe(host, 1);
    Require(result.outer == S_OK && result.callback == S_OK && result.entered && result.completed &&
                result.effect == 2,
            "generated edit session completes once");
    Require(host.retained->DoEditSession(123) == E_FAIL, "expired callback is refused");
    host.invoke = false;
    result = instance.Probe(host, 1);
    Require(!result.entered && !result.completed && result.effect == 0,
            "unentered callback grants no effect");
    Require(host.retained->DoEditSession(123) == E_FAIL, "late callback is refused");
    host.retained->Release();
    host.retained = nullptr;
    for (HRESULT status : {S_FALSE, static_cast<HRESULT>(2), E_FAIL}) {
      host.request_result = status;
      result = instance.Probe(host, 1);
      Require(result.outer == status, "exact outer RequestEditSession HRESULT survives");
    }
  }
  Require(api.unload() == S_OK, "all boundary objects drain");
  for (uint32_t at : {1U, 2U, 3U}) {
    Instance instance(api);
    Require(api.panic(instance.handle, at) == S_OK, "arm contained Rust panic");
    if (at != 3) {
      Host host;
      const auto result = instance.Probe(host, 1);
      Require(result.callback == E_FAIL && result.effect == at - 1,
              "panic retains attempted facts");
      BOOL stopped = FALSE;
      Require(api.stopped(instance.handle, &stopped) == S_OK && stopped,
              "panic permanently poisons");
      Require(instance.service->Deactivate() == E_FAIL, "poison refuses later callbacks");
    }
  }
  Require(api.unload() == S_OK, "final generated Release contains destructor panic");
}

void Throw(Api &api, std::wstring_view mode) {
  Instance instance(api);
  Host host;
  if (mode == L"query-before")
    host.query_throw = 1;
  else if (mode == L"query-after")
    host.query_throw = 2;
  else if (mode == L"request-before")
    host.request_throw = 1;
  else if (mode == L"request-after")
    host.request_throw = 2;
  else if (mode == L"add-before")
    host.add_throw = 1;
  else if (mode == L"add-after")
    host.add_throw = 2;
  else if (mode == L"release-before")
    host.release_throw = 1;
  else if (mode == L"release-after")
    host.release_throw = 2;
  else
    Require(false, "recognized exception schedule");
  const uint32_t operation = mode.starts_with(L"request") || mode.starts_with(L"add") ? 1 : 0;
  const auto result = instance.Probe(host, operation);
  if (mode.starts_with(L"release")) {
    Require(result.outer == S_OK && host.releases == 1, "ambiguous Release is never retried");
  } else {
    Require(result.outer == E_UNEXPECTED && result.foreign_exception == 1,
            "host exception caught entirely inside C++ fence");
    if (mode == L"request-before")
      Require(result.effect == 0 && !result.entered,
              "throw before callback has no attempted effect");
    if (mode == L"request-after")
      Require(result.effect == 2 && result.completed, "throw after callback preserves completion");
  }
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  Require(argc == 3, "DLL and schedule arguments");
  Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization");
  Api api(argv[1]);
  if (std::wstring_view(argv[2]) == L"basic")
    Basic(api);
  else
    Throw(api, argv[2]);
  CoUninitialize();
  std::puts("Rust DLL boundary schedule passed");
  return 0;
}
