#include "test_service.h"
#include "host_build.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace keyrill {
namespace {
[[noreturn]] void TransportFailure(const char *name) {
  std::fprintf(stderr, "FAIL: Rust test transport %s\n", name);
  std::exit(1);
}

KrWindowsHostIdentity TransportHost(const host::Identity &value) {
  KrWindowsHostIdentity out{};
  out.process = value.process;
  out.thread = value.thread;
  out.started = value.started;
  out.kind = static_cast<uint32_t>(value.kind);
  std::copy(value.version.begin(), value.version.end(), out.version);
  return out;
}

KrWindowsFieldWindow TransportWindow(const native_field::Window &value) {
  KrWindowsFieldWindow out{};
  out.handle = value.handle;
  out.process = value.process;
  out.thread = value.thread;
  out.control = value.control;
  out.style = value.style;
  std::copy(value.class_name.begin(), value.class_name.end(), out.class_name);
  out.enabled = value.enabled;
  out.complete = value.complete;
  return out;
}

HRESULT WINAPI Capture(void *context, HWND window, const KrWindowsHostIdentity *identity,
                       KrWindowsFieldFacts *output) {
  host::Identity host{};
  host.process = identity->process;
  host.thread = identity->thread;
  host.started = identity->started;
  host.kind = static_cast<host::Kind>(identity->kind);
  std::copy(std::begin(identity->version), std::end(identity->version), host.version.begin());
  const auto facts = static_cast<const native_field::Source *>(context)->Capture(window, host);
  *output = {};
  output->host = TransportHost(facts.host);
  output->process = facts.process;
  output->thread = facts.thread;
  output->view = facts.view;
  output->focus = facts.focus;
  output->foreground = facts.foreground;
  output->root = facts.root;
  output->field = TransportWindow(facts.field);
  output->parent = TransportWindow(facts.parent);
  output->grandparent = TransportWindow(facts.grandparent);
  output->proof_root = TransportWindow(facts.proof_root);
  output->proof_process = facts.proof_process;
  return S_OK;
}
} // namespace

FARPROC TestExport(const char *name) {
  static const HMODULE module = [] {
    HMODULE loaded = LoadLibraryW(KEYRILL_TEST_DLL_LOCATION);
    if (!loaded)
      TransportFailure("load KeyrillTsfTest.dll");
    auto digest =
        reinterpret_cast<KrTestBuildDigest>(GetProcAddress(loaded, "KeyrillTestBuildDigest"));
    std::array<uint32_t, 8> actual{};
    if (!digest || digest(static_cast<uint32_t>(actual.size()), actual.data()) != S_OK ||
        actual != probe::test_build_digest)
      TransportFailure("test DLL build fingerprint");
    return loaded;
  }();
  const auto function = GetProcAddress(module, name);
  if (!function)
    TransportFailure(name);
  return function;
}

long TestObjectCount() {
  LONG objects = 0, workers = 0;
  auto counts = reinterpret_cast<KrTestCounts>(TestExport("KeyrillTestCounts"));
  if (counts(&objects, &workers) != S_OK)
    TransportFailure("object counts");
  return objects;
}
long TestWorkerCount() {
  LONG objects = 0, workers = 0;
  auto counts = reinterpret_cast<KrTestCounts>(TestExport("KeyrillTestCounts"));
  if (counts(&objects, &workers) != S_OK)
    TransportFailure("worker counts");
  return workers;
}

TestService::TestService(ITfInputProcessorProfileMgr *profile_api,
                         decltype(&GetKeyboardLayoutList) layouts,
                         const native_field::Source *source) {
  KrWindowsDependencies dependencies{};
  dependencies.size = sizeof(dependencies);
  dependencies.version = KR_WINDOWS_TEST_VERSION;
  dependencies.context = const_cast<native_field::Source *>(source);
  dependencies.profiles = profile_api;
  dependencies.layouts = layouts;
  dependencies.capture = source ? Capture : nullptr;
  auto create = reinterpret_cast<KrTestCreate>(TestExport("KeyrillTestCreate"));
  if (create(KR_WINDOWS_TEST_VERSION, &dependencies, &handle_,
             reinterpret_cast<void **>(object_.GetAddressOf())) != S_OK ||
      !handle_ || !object_)
    TransportFailure("create service");
}
TestService::~TestService() {
  object_.Reset();
  auto release = reinterpret_cast<KrTestRelease>(TestExport("KeyrillTestRelease"));
  if (release(handle_) != S_OK)
    TransportFailure("release service");
}
ULONG TestService::AddRef() { return ++references_; }
host::Metadata TestService::ObserveMetadata(ITfContext *context, ITfThreadMgr *manager,
                                            TfClientId client, const uint32_t *current) {
  auto observe = reinterpret_cast<KrTestMetadata>(TestExport("KeyrillTestMetadata"));
  KrWindowsMetadata output{};
  if (observe(handle_, context, manager, client, current, &output) != S_OK)
    TransportFailure("observe metadata");
  return {static_cast<host::Reason>(output.reason),
          output.result,
          static_cast<host::Focus>(output.focus),
          static_cast<host::Scope>(output.scope),
          output.request,
          output.session,
          output.entered,
          output.completed,
          output.selection,
          output.acp};
}
ULONG TestService::Release() {
  ULONG remaining = --references_;
  if (!remaining)
    delete this;
  return remaining;
}
HRESULT TestService::QueryInterface(REFIID id, void **output) {
  return object_->QueryInterface(id, output);
}

#define FORWARD(interface_type, method, parameters, arguments)                                     \
  HRESULT TestService::method parameters {                                                         \
    return Forward<interface_type>(__uuidof(interface_type),                                       \
                                   [&](auto *target) { return target->method arguments; });        \
  }
FORWARD(ITfTextInputProcessorEx, Activate, (ITfThreadMgr * manager, TfClientId client),
        (manager, client))
FORWARD(ITfTextInputProcessorEx, ActivateEx,
        (ITfThreadMgr * manager, TfClientId client, DWORD flags), (manager, client, flags))
FORWARD(ITfTextInputProcessorEx, Deactivate, (), ())
FORWARD(ITfFunctionProvider, GetType, (GUID * id), (id))
FORWARD(ITfFunctionProvider, GetDescription, (BSTR * description), (description))
FORWARD(ITfFunctionProvider, GetFunction, (REFGUID function, REFIID id, IUnknown **output),
        (function, id, output))
FORWARD(ITfKeyEventSink, OnSetFocus, (BOOL foreground), (foreground))
FORWARD(ITfKeyEventSink, OnTestKeyDown,
        (ITfContext * context, WPARAM key, LPARAM flags, BOOL *eaten), (context, key, flags, eaten))
FORWARD(ITfKeyEventSink, OnTestKeyUp, (ITfContext * context, WPARAM key, LPARAM flags, BOOL *eaten),
        (context, key, flags, eaten))
FORWARD(ITfKeyEventSink, OnKeyDown, (ITfContext * context, WPARAM key, LPARAM flags, BOOL *eaten),
        (context, key, flags, eaten))
FORWARD(ITfKeyEventSink, OnKeyUp, (ITfContext * context, WPARAM key, LPARAM flags, BOOL *eaten),
        (context, key, flags, eaten))
FORWARD(ITfKeyEventSink, OnPreservedKey, (ITfContext * context, REFGUID id, BOOL *eaten),
        (context, id, eaten))
FORWARD(ITfThreadMgrEventSink, OnInitDocumentMgr, (ITfDocumentMgr * manager), (manager))
FORWARD(ITfThreadMgrEventSink, OnUninitDocumentMgr, (ITfDocumentMgr * manager), (manager))
FORWARD(ITfThreadMgrEventSink, OnSetFocus, (ITfDocumentMgr * focus, ITfDocumentMgr *previous),
        (focus, previous))
FORWARD(ITfThreadMgrEventSink, OnPushContext, (ITfContext * context), (context))
FORWARD(ITfThreadMgrEventSink, OnPopContext, (ITfContext * context), (context))
FORWARD(ITfCompositionSink, OnCompositionTerminated,
        (TfEditCookie cookie, ITfComposition *composition), (cookie, composition))
FORWARD(ITfInputProcessorProfileActivationSink, OnActivated,
        (DWORD type, LANGID language, REFCLSID service, REFGUID category, REFGUID profile,
         HKL layout, DWORD flags),
        (type, language, service, category, profile, layout, flags))
#undef FORWARD

HRESULT TestService::Copy(uint32_t size, uint32_t version, diagnostics::Snapshot *output) {
  return Forward<diagnostics::SnapshotReader>(
      diagnostics::snapshot_iid, [&](auto *reader) { return reader->Copy(size, version, output); });
}
HRESULT TestService::Copy(uint32_t size, uint32_t version, diagnostics::SnapshotV2 *output) {
  return Forward<diagnostics::SnapshotReaderV2>(diagnostics::snapshot_v2_iid, [&](auto *reader) {
    return reader->Copy(size, version, output);
  });
}
HRESULT TestService::Copy(uint32_t size, uint32_t version, runtime::Snapshot *output) {
  return Forward<runtime::Reader>(
      runtime::reader_iid, [&](auto *reader) { return reader->Copy(size, version, output); });
}
bool TestService::HandleEvent(ITfContext *context, const kr_event &event) {
  BOOL eaten = FALSE;
  auto dispatch = reinterpret_cast<KrTestEvent>(TestExport("KeyrillTestEvent"));
  dispatch(handle_, context, &event, &eaten);
  return eaten != FALSE;
}
bool TestService::stopped() const {
  BOOL value = FALSE;
  auto stopped = reinterpret_cast<KrTestStopped>(TestExport("KeyrillTestStopped"));
  if (stopped(handle_, &value) != S_OK)
    TransportFailure("stopped service");
  return value != FALSE;
}
void TestService::Maintain() noexcept {
  reinterpret_cast<KrTestMaintain>(TestExport("KeyrillTestMaintain"))(handle_);
}
} // namespace keyrill
