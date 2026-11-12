struct RuntimeEnvironment {
  const wchar_t *name;
  std::optional<std::wstring> previous;
  RuntimeEnvironment(const wchar_t *key, const wchar_t *value) : name(key) {
    wchar_t buffer[32768]{};
    DWORD size = GetEnvironmentVariableW(key, buffer, 32768);
    if (size) {
      Require(size < 32768, "bounded test environment");
      previous = std::wstring(buffer, size);
    }
    Require(SetEnvironmentVariableW(key, value), "set isolated test environment");
  }
  ~RuntimeEnvironment() { SetEnvironmentVariableW(name, previous ? previous->c_str() : nullptr); }
};
struct RuntimeFiles {
  std::filesystem::path root;
  RuntimeFiles() {
    wchar_t temporary[32768]{};
    Require(GetTempPathW(32768, temporary) != 0, "temporary filesystem root");
    GUID id{};
    wchar_t suffix[40]{};
    Require(SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(id, suffix, 40),
            "unique test directory");
    root = std::filesystem::path(temporary) / (std::wstring(L"keyrill-public-") + suffix);
    Require(CreateDirectoryW(root.c_str(), nullptr), "create owned test directory");
  }
  ~RuntimeFiles() {
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(root, error);
    Require(!error && resolved.is_absolute() &&
                resolved.filename().wstring().starts_with(L"keyrill-public-") &&
                std::filesystem::is_directory(resolved) &&
                !(GetFileAttributesW(resolved.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT),
            "cleanup is confined to the owned public test directory");
    std::filesystem::remove_all(root, error);
  }
  std::wstring Name(const wchar_t *name) const { return (root / name).wstring(); }
  void Write(const wchar_t *name, const std::string &bytes) {
    HANDLE file = CreateFileW(Name(name).c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              CREATE_ALWAYS, 0, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "open public test vocabulary");
    DWORD written = 0;
    bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
              written == bytes.size();
    CloseHandle(file);
    Require(ok, "write public test vocabulary");
  }
  vocabulary::Source Source(const wchar_t *name, bool defaults = false) const {
    auto path = vocabulary::ParsePath(Name(name));
    Require(path.has_value(), "test source uses bounded local path");
    if (defaults)
      return vocabulary::DefaultSource{path};
    return vocabulary::ExplicitSource{*path};
  }
};
template <class Predicate> void RuntimeWait(Predicate predicate, const char *why) {
  const ULONGLONG deadline = GetTickCount64() + 15000;
  do {
    if (predicate())
      return;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  } while (GetTickCount64() < deadline);
  Require(false, why);
}
runtime::Snapshot RuntimeSnapshot(TestService &service) {
  runtime::Snapshot value;
  Require(service.Copy(sizeof(value), runtime::version, &value) == S_OK,
          "content-free runtime snapshot");
  return value;
}
runtime::Snapshot ReadyRuntime(TestService &service) {
  runtime::Snapshot value;
  RuntimeWait(
      [&] {
        service.Maintain();
        value = RuntimeSnapshot(service);
        return value.effective == KR_POLICY_MODEL &&
               value.sources[0] == vocabulary::Status::Ready &&
               value.sources[1] == vocabulary::Status::Ready;
      },
      "model and both vocabulary sources become ready");
  return value;
}
void RuntimeCodecSchedules() {
  runtime::Snapshot value;
  value.activation = 7;
  value.requested = value.effective = KR_POLICY_MODEL;
  value.model_ready = 1;
  value.revision = std::numeric_limits<uint64_t>::max();
  value.sources = {vocabulary::Status::Ready, vocabulary::Status::Retained};
  value.source_errors[1] = vocabulary::Error::Parse;
  wchar_t text[runtime::status_capacity]{};
  runtime::Snapshot parsed;
  Require(runtime::Format(value, text) && runtime::Parse(text, parsed) && parsed.activation == 7 &&
              parsed.revision == value.revision && parsed.sources == value.sources &&
              parsed.source_errors == value.source_errors,
          "readiness transport preserves bounded content-free facts");
  for (const auto &bad : {std::wstring(text) + L" trailing", std::wstring(text) + L"; extra=1",
                          std::wstring(L"Keyrill runtime v1; activation=-1"),
                          std::wstring(L"Keyrill runtime v2; activation=7")}) {
    parsed.activation = 42;
    Require(!runtime::Parse(bad, parsed) && parsed.activation == 42,
            "malformed readiness never publishes a partial snapshot");
  }
}
thread_local TestService *runtime_creating = nullptr;
thread_local HRESULT runtime_reentry_result = E_PENDING;
thread_local Manager *runtime_reentry_manager = nullptr;
thread_local bool runtime_destroying = false;
thread_local HRESULT runtime_reactivation_result = E_PENDING;
LRESULT CALLBACK RuntimeCreationHook(int code, WPARAM w, LPARAM l) {
  if (code == (runtime_destroying ? HCBT_DESTROYWND : HCBT_CREATEWND) && runtime_creating) {
    wchar_t name[64]{};
    if (GetClassNameW(reinterpret_cast<HWND>(w), name, 64) &&
        wcscmp(name, L"KeyrillSnapshotMaintenance") == 0) {
      auto *service = std::exchange(runtime_creating, nullptr);
      runtime_reentry_result = service->Deactivate();
      if (runtime_reentry_manager)
        runtime_reactivation_result = service->Activate(runtime_reentry_manager, 42);
    }
  }
  return CallNextHookEx(nullptr, code, w, l);
}
void RuntimeWindowReentrySchedules() {
  Harness h;
  Require(h.service->Deactivate() == S_OK, "stop initial runtime before creation regression");
  RuntimeWait([] { return TestWorkerCount() == 0; }, "initial worker drains independently");
  runtime_creating = h.service.Get();
  runtime_reentry_result = E_PENDING;
  HHOOK hook = SetWindowsHookExW(WH_CBT, RuntimeCreationHook, nullptr, GetCurrentThreadId());
  Require(hook != nullptr, "install thread-local hidden-window creation schedule");
  const HRESULT activated = h.service->Activate(&h.manager, 42);
  Require(UnhookWindowsHookEx(hook), "remove thread-local creation schedule");
  const bool called = runtime_creating == nullptr;
  runtime_creating = nullptr;
  WNDCLASSW remaining{};
  Require(FAILED(activated) && called && runtime_reentry_result == S_OK &&
              !DiagnosticSnapshot(*h.service.Get()).active && TestWorkerCount() == 0 &&
              !GetClassInfoW(GetModuleHandleW(nullptr), L"KeyrillSnapshotMaintenance", &remaining),
          "reentrant deactivation starts no late worker and retires its window class");
  runtime_creating = h.service.Get();
  runtime_reentry_manager = &h.manager;
  runtime_reactivation_result = E_PENDING;
  hook = SetWindowsHookExW(WH_CBT, RuntimeCreationHook, nullptr, GetCurrentThreadId());
  Require(hook && FAILED(h.service->Activate(&h.manager, 42)) && UnhookWindowsHookEx(hook),
          "creation schedule cancels the whole activation frame");
  Require(!runtime_creating && runtime_reactivation_result == E_INVALIDARG &&
              !DiagnosticSnapshot(*h.service.Get()).active,
          "creation cannot overlap a newer activation before captured resources drain");
  Require(h.service->Activate(&h.manager, 42) == S_OK,
          "activation resumes only after the cancelled creation frame drains");
  const auto ready_from_timer = [&] {
    RuntimeWait(
        [&] {
          auto ready = RuntimeSnapshot(*h.service.Get());
          return ready.model_ready && ready.effective == KR_POLICY_MODEL &&
                 ready.sources[0] == vocabulary::Status::Ready &&
                 ready.sources[1] == vocabulary::Status::Ready;
        },
        "new activation retains its maintenance window and reaches readiness through its timer");
  };
  ready_from_timer();
  runtime_creating = h.service.Get();
  runtime_destroying = true;
  runtime_reactivation_result = E_PENDING;
  hook = SetWindowsHookExW(WH_CBT, RuntimeCreationHook, nullptr, GetCurrentThreadId());
  Require(hook && h.service->Deactivate() == S_OK && UnhookWindowsHookEx(hook),
          "destruction schedule attempts reactivation during captured teardown");
  Require(!runtime_creating && runtime_reentry_result == S_OK &&
              runtime_reactivation_result == E_INVALIDARG &&
              !DiagnosticSnapshot(*h.service.Get()).active,
          "nested teardown is idempotent and activation is refused until resources release");
  runtime_reentry_manager = nullptr;
  runtime_destroying = false;
  Require(h.service->Activate(&h.manager, 42) == S_OK, "fresh activation after teardown succeeds");
  ready_from_timer();
  Require(h.service->Deactivate() == S_OK, "retire fresh activation");
  RuntimeWait([] { return TestWorkerCount() == 0; }, "all lifecycle workers drain");
  Require(!GetClassInfoW(GetModuleHandleW(nullptr), L"KeyrillSnapshotMaintenance", &remaining),
          "creation and destruction schedules leave no window class");
}
void RuntimeServiceSchedules() {
  RuntimeCodecSchedules();
  RuntimeFiles files;
  files.Write(L"en.add", "");
  files.Write(L"ru.add", "");
  RuntimeEnvironment policy(L"KEYRILL_WINDOWS_POLICY", L"model");
  RuntimeEnvironment en(L"KEYRILL_WINDOWS_EN_DICTIONARY", files.Name(L"en.add").c_str());
  RuntimeEnvironment ru(L"KEYRILL_WINDOWS_RU_DICTIONARY", files.Name(L"ru.add").c_str());
  RuntimeWindowReentrySchedules();
  for (uint32_t mode : {KR_EN, KR_RU}) {
    Harness h;
    auto ready = ReadyRuntime(*h.service.Get());
    Require(ready.model_ready && ready.requested == KR_POLICY_MODEL,
            "actual prepared shared model");
    const wchar_t *word = mode == KR_EN ? L"cgbcjr" : L"цщкдв";
    for (const wchar_t *p = word; *p; ++p) {
      auto event = Text(*p);
      event.mode = mode;
      Require(h.service->HandleEvent(&h.a, event), "actual service owns model input");
    }
    auto space = Text(L' ');
    space.mode = mode;
    Require(h.service->HandleEvent(&h.a, space), "actual service model boundary");
    h.Oracle(mode == KR_EN ? L"список " : L"world ", mode == KR_EN ? 14 : 13);
    files.Write(L"en.add", "publicword\n");
    const auto previous = RuntimeSnapshot(*h.service.Get()).revision;
    RuntimeWait(
        [&] {
          h.service->Maintain();
          auto now = RuntimeSnapshot(*h.service.Get());
          return now.revision > previous + 1 && now.sources[0] == vocabulary::Status::Ready;
        },
        "dictionary adoption while conversion receipt is owned");
    Require(h.service->HandleEvent(&h.a, kr_event{KR_REVERSE, mode, KR_CTX_INELIGIBLE, 0, {}}),
            "reverse after snapshot publication");
    h.Oracle(mode == KR_EN ? L"cgbcjr " : L"цщкдв ", mode == KR_EN ? 14 : 13);
  }
  files.Write(L"en.add", "cgbcjr\n");
  {
    Harness h;
    ReadyRuntime(*h.service.Get());
    h.Type(L"cgbcjr ");
    h.Oracle(L"cgbcjr ", 14);
  }
  files.Write(L"en.add", "");
  files.Write(L"ru.add", "список/!\n");
  {
    Harness h;
    ReadyRuntime(*h.service.Get());
    h.Type(L"cgbcjr ");
    h.Oracle(L"cgbcjr ", 14);
  }
  files.Write(L"ru.add", "");
  {
    Harness h;
    auto before = ReadyRuntime(*h.service.Get());
    files.Write(L"en.add", std::string("bad\0data", 8));
    RuntimeWait(
        [&] {
          h.service->Maintain();
          auto now = RuntimeSnapshot(*h.service.Get());
          return now.revision > before.revision && now.sources[0] == vocabulary::Status::Retained &&
                 now.source_errors[0] == vocabulary::Error::Parse;
        },
        "malformed reload retains actual service vocabulary");
    h.Type(L"cgbcjr ");
    h.Oracle(L"список ", 14);
  }
  {
    RuntimeEnvironment missing(L"KEYRILL_WINDOWS_EN_DICTIONARY", files.Name(L"absent.add").c_str());
    Harness h;
    RuntimeWait(
        [&] {
          h.service->Maintain();
          return RuntimeSnapshot(*h.service.Get()).effective == KR_POLICY_MODEL;
        },
        "missing explicit source reports model readiness");
    Require(RuntimeSnapshot(*h.service.Get()).sources[0] == vocabulary::Status::Missing,
            "explicit missing source stays unavailable");
    h.Type(L"cgbcjr ");
    h.Oracle(L"cgbcjr ", 14);
  }
  {
    RuntimeEnvironment off(L"KEYRILL_WINDOWS_POLICY", L"off");
    Harness h;
    Require(RuntimeSnapshot(*h.service.Get()).effective == KR_POLICY_OFF, "off policy captured");
    h.Type(L"cgbcjr");
    Require(h.service->HandleEvent(&h.a, kr_event{KR_MANUAL, KR_EN, KR_CTX_INELIGIBLE, 0, {}}),
            "manual command remains available when off");
    h.Oracle(L"список", 13);
    h.service->Deactivate();
    RuntimeEnvironment invalid(L"KEYRILL_WINDOWS_POLICY", L"invalid");
    Require(h.service->Activate(&h.manager, 42) == S_OK,
            "reactivate with invalid complete configuration");
    auto state = RuntimeSnapshot(*h.service.Get());
    Require(state.requested == KR_POLICY_OFF &&
                state.configuration_error == vocabulary::Error::Configuration,
            "invalid configuration retains previous accepted whole config");
  }
  files.Write(L"en.add", "");
  {
    Harness h;
    auto before = ReadyRuntime(*h.service.Get());
    files.Write(L"en.add", "another\n");
    bool nested = false;
    h.a.hook = [&](const char *) {
      if (nested)
        return;
      nested = true;
      const auto until = GetTickCount64() + 1200;
      do {
        h.service->Maintain();
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
          DispatchMessageW(&message);
        runtime::Snapshot value;
        Require(h.service->Copy(sizeof(value), runtime::version, &value) == E_PENDING,
                "nested callbacks refuse readiness reads and adoption");
        SwitchToThread();
      } while (GetTickCount64() < until);
    };
    h.Key(L'c');
    h.a.hook = nullptr;
    Require(nested && RuntimeSnapshot(*h.service.Get()).revision == before.revision,
            "nested message pumping defers snapshot adoption");
    RuntimeWait(
        [&] {
          h.service->Maintain();
          return RuntimeSnapshot(*h.service.Get()).revision > before.revision;
        },
        "outer maintenance adopts latest deferred snapshot");
  }
}

struct WorkerEvents {
  HANDLE published = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::function<void(vocabulary::Boundary, HANDLE, size_t)> action;
  ~WorkerEvents() {
    CloseHandle(published);
    CloseHandle(finished);
  }
  static void Hook(void *context, vocabulary::Boundary boundary, HANDLE file, size_t index) {
    auto &events = *static_cast<WorkerEvents *>(context);
    if (events.action)
      events.action(boundary, file, index);
    if (boundary == vocabulary::Boundary::Published)
      SetEvent(events.published);
    if (boundary == vocabulary::Boundary::AfterCount)
      SetEvent(events.finished);
  }
  vocabulary::Hooks Hooks() { return {Hook, this}; }
  vocabulary::Ready Next(vocabulary::Worker &worker) {
    Require(WaitForSingleObject(published, 15000) == WAIT_OBJECT_0, "bounded worker publication");
    auto packet = worker.Poll();
    Require(packet.has_value(), "whole ready packet published");
    return std::move(*packet);
  }
  void Stop(vocabulary::Worker &worker) {
    worker.Stop();
    Require(WaitForSingleObject(finished, 15000) == WAIT_OBJECT_0,
            "worker drains after nonblocking stop");
  }
};
void RuntimeGlobalHookSchedules() {
  WorkerEvents events;
  auto create =
      reinterpret_cast<decltype(&KeyrillTestWorkerCreate)>(TestExport("KeyrillTestWorkerCreate"));
  HMODULE dll = GetModuleHandleW(KEYRILL_TEST_DLL_LOCATION);
  Require(dll && vocabulary::SetTestHooks(dll, events.Hooks()),
          "global Rust worker hooks retain their callback owner");
  auto start =
      reinterpret_cast<decltype(&KeyrillTestWorkerStart)>(TestExport("KeyrillTestWorkerStart"));
  auto stop =
      reinterpret_cast<decltype(&KeyrillTestWorkerStop)>(TestExport("KeyrillTestWorkerStop"));
  auto free_worker =
      reinterpret_cast<decltype(&KeyrillTestWorkerFree)>(TestExport("KeyrillTestWorkerFree"));
  void *worker = create();
  KrWorkerSource sources[2]{{1, nullptr, 0}, {1, nullptr, 0}};
  KrWorkerHooks inherited{};
  Require(worker && start(worker, sources, 61, 0, &inherited),
          "worker acquires globally retained hooks after setter returns");
  Require(WaitForSingleObject(events.published, 15000) == WAIT_OBJECT_0,
          "global hooks remain live through asynchronous publication");
  Require(vocabulary::ClearTestHooks(dll), "global hook owner can retire while worker runs");
  stop(worker);
  free_worker(worker);
  Require(WaitForSingleObject(events.finished, 15000) == WAIT_OBJECT_0,
          "worker callback owner survives global clear through AfterCount");
}
void RuntimeAbsentSourceSchedules() {
  WorkerEvents events;
  vocabulary::Worker worker;
  Require(
      worker.Start(vocabulary::Model{{vocabulary::DefaultSource{}, vocabulary::DefaultSource{}}},
                   62, vocabulary::ModuleOwner::Process, events.Hooks()),
      "absent optional sources serialize as null empty paths");
  const auto ready = events.Next(worker);
  Require(ready.model && ready.sources[0].status == vocabulary::Status::Missing &&
              ready.sources[1].status == vocabulary::Status::Missing &&
              ready.sources[0].error == vocabulary::Error::Unavailable &&
              ready.sources[1].error == vocabulary::Error::Unavailable &&
              !ready.sources[0].dictionary && !ready.sources[1].dictionary,
          "actual absent default source paths publish unavailable dictionaries");
  events.Stop(worker);
}
struct NativeSchedule {
  enum class Io { Real, Immediate, Pending, Drain } io = Io::Real;
  enum class Failure {
    None,
    Event,
    Pin,
    Submit,
    LongRunning,
    Root,
    Component,
    Verification
  } failure = Failure::None;
  static inline NativeSchedule *current = nullptr;
  vocabulary::NativeCalls defaults;
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE cancelled = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE draining = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE file = nullptr;
  OVERLAPPED *operation = nullptr;
  void *buffer = nullptr;
  std::atomic<unsigned> completions{0}, reads{0}, opens{0}, events{0};
  std::atomic<bool> verification{false}, remap{false}, completion_returned{false};
  bool change_association = false;
  unsigned fail_event_at = 1;
  NativeSchedule() {
    Require(!current, "native schedules have one harness-owned dependency context");
    current = this;
  }
  ~NativeSchedule() {
    current = nullptr;
    for (HANDLE event : {entered, cancelled, draining, release, finished})
      CloseHandle(event);
  }
  static NTSTATUS NTAPI Open(PHANDLE out, ACCESS_MASK access, POBJECT_ATTRIBUTES object,
                             PIO_STATUS_BLOCK status, PLARGE_INTEGER size, ULONG attributes,
                             ULONG sharing, ULONG disposition, ULONG options, PVOID ea,
                             ULONG ea_length) {
    auto &s = *current;
    ++s.opens;
    if ((s.failure == Failure::Root && !object->RootDirectory) ||
        (s.failure == Failure::Component && object->RootDirectory && !s.verification.load()) ||
        (s.failure == Failure::Verification && s.verification.load())) {
      if (s.change_association)
        s.remap = true;
      *out = nullptr;
      return static_cast<NTSTATUS>(0xc0000034L);
    }
    return s.defaults.open(out, access, object, status, size, attributes, sharing, disposition,
                           options, ea, ea_length);
  }
  static BOOL WINAPI VolumePaths(LPCWSTR guid, LPWCH paths, DWORD capacity, PDWORD length) {
    if (current->remap.load()) {
      *length = 1;
      if (capacity)
        paths[0] = 0;
      return TRUE;
    }
    return current->defaults.volume_paths(guid, paths, capacity, length);
  }
  static HANDLE WINAPI Event(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL initial,
                             LPCWSTR name) {
    auto &s = *current;
    ++s.events;
    if (s.failure == Failure::Event && s.events == s.fail_event_at)
      return nullptr;
    return s.defaults.event(attributes, manual, initial, name);
  }
  static BOOL WINAPI Pin(DWORD flags, LPCWSTR address, HMODULE *module) {
    if (current->failure == Failure::Pin)
      return FALSE;
    return current->defaults.pin(flags, address, module);
  }
  static BOOL WINAPI Submit(PTP_SIMPLE_CALLBACK callback, PVOID context,
                            PTP_CALLBACK_ENVIRON environment) {
    if (current->failure == Failure::Submit)
      return FALSE;
    auto submission =
        std::make_unique<Submission>(Submission{callback, context, current->finished});
    auto *raw = submission.release();
    if (!current->defaults.submit(Submitted, raw, environment)) {
      delete raw;
      return FALSE;
    }
    return TRUE;
  }
  struct Submission {
    PTP_SIMPLE_CALLBACK callback;
    void *context;
    HANDLE finished;
  };
  static void CALLBACK Submitted(PTP_CALLBACK_INSTANCE instance, void *context) {
    std::unique_ptr<Submission> submission(static_cast<Submission *>(context));
    submission->callback(instance, submission->context);
    SetEvent(submission->finished);
  }
  static BOOL WINAPI LongRunning(PTP_CALLBACK_INSTANCE instance) {
    if (current->failure == Failure::LongRunning)
      return FALSE;
    return current->defaults.long_running(instance);
  }
  static BOOL WINAPI Read(HANDLE file, LPVOID buffer, DWORD bytes, LPDWORD count,
                          LPOVERLAPPED operation) {
    auto &s = *current;
    ++s.reads;
    Require(!count && operation && bytes > 0, "read count comes from completion only");
    s.file = file;
    s.operation = operation;
    s.buffer = buffer;
    if (!operation->Offset && s.io != Io::Drain) {
      Require(bytes >= 6, "completion fixture buffer holds exact source bytes");
      memcpy(buffer, "hello\n", 6);
    }
    SetEvent(s.entered);
    if (s.io == Io::Immediate)
      return TRUE;
    if (s.io == Io::Pending)
      SetEvent(operation->hEvent);
    SetLastError(ERROR_IO_PENDING);
    return FALSE;
  }
  static BOOL WINAPI Cancel(HANDLE file, LPOVERLAPPED operation) {
    auto &s = *current;
    Require(file == s.file && operation == s.operation, "cancel targets only the pending read");
    SetEvent(s.cancelled);
    return TRUE;
  }
  static BOOL WINAPI Completion(HANDLE file, LPOVERLAPPED operation, LPDWORD count, BOOL wait) {
    auto &s = *current;
    ++s.completions;
    Require(file == s.file && operation == s.operation, "completion owns the original operation");
    if (s.io == Io::Drain) {
      Require(wait && WaitForSingleObject(s.cancelled, 0) == WAIT_OBJECT_0,
              "cancellation submission precedes blocking completion drainage");
      SetEvent(s.draining);
      Require(WaitForSingleObject(s.release, 15000) == WAIT_OBJECT_0,
              "harness releases simulated pending completion");
      DWORD flags = 0;
      Require(GetHandleInformation(file, &flags) && GetHandleInformation(operation->hEvent, &flags),
              "file and overlapped event remain live during delayed completion");
      *static_cast<uint8_t *>(s.buffer) = 0;
      *count = 0;
      s.completion_returned = true;
      SetLastError(ERROR_OPERATION_ABORTED);
      return FALSE;
    }
    Require(!wait, "signaled completion never needs blocking drainage");
    *count = operation->Offset ? 0 : 6;
    return TRUE;
  }
  static void Hook(void *context, vocabulary::Boundary at, HANDLE, size_t index) {
    auto &s = *static_cast<NativeSchedule *>(context);
    if (at == vocabulary::Boundary::ParentOpened && index == 0)
      s.verification = false;
    if (at == vocabulary::Boundary::AfterRead)
      s.verification = true;
  }
  vocabulary::Hooks Hooks() {
    vocabulary::Hooks hooks{Hook, this};
    hooks.native.open = Open;
    hooks.native.volume_paths = VolumePaths;
    hooks.native.event = Event;
    hooks.native.pin = Pin;
    hooks.native.submit = Submit;
    hooks.native.long_running = LongRunning;
    if (io != Io::Real) {
      hooks.native.read = Read;
      hooks.native.completion = Completion;
      hooks.native.cancel = Cancel;
    }
    return hooks;
  }
};
void RuntimeNativeSchedules() {
  RuntimeFiles files;
  files.Write(L"en.add", "hello\n");
  const vocabulary::Model model{{files.Source(L"en.add", true), files.Source(L"en.add", true)}};
  for (auto failure : {NativeSchedule::Failure::Root, NativeSchedule::Failure::Verification}) {
    for (bool remap : {false, true}) {
      NativeSchedule schedule;
      schedule.failure = failure;
      schedule.change_association = remap;
      vocabulary::Worker worker;
      Require(worker.Start(model, 31, vocabulary::ModuleOwner::Process, schedule.Hooks()),
              "native-open failure worker starts");
      std::optional<vocabulary::Ready> ready;
      RuntimeWait(
          [&] {
            ready = worker.Poll();
            return ready.has_value();
          },
          "native-open failure publishes content-free status");
      Require(ready->sources[0].status == vocabulary::Status::Missing &&
                  !ready->sources[0].dictionary &&
                  ready->sources[0].error == (failure == NativeSchedule::Failure::Root
                                                  ? vocabulary::Error::Unavailable
                                                  : vocabulary::Error::Unstable),
              "unadmitted root and verification absence cannot publish default empty");
      worker.Stop();
      Require(WaitForSingleObject(schedule.finished, 15000) == WAIT_OBJECT_0,
              "failure injection context outlives the worker");
    }
  }
  {
    NativeSchedule schedule;
    schedule.failure = NativeSchedule::Failure::Component;
    schedule.change_association = true;
    vocabulary::Worker worker;
    Require(worker.Start(model, 31, vocabulary::ModuleOwner::Process, schedule.Hooks()),
            "original component absence with changed association starts");
    std::optional<vocabulary::Ready> ready;
    RuntimeWait(
        [&] {
          ready = worker.Poll();
          return ready.has_value();
        },
        "changed association is checked after original open failure");
    worker.Stop();
    Require(WaitForSingleObject(schedule.finished, 15000) == WAIT_OBJECT_0 &&
                ready->sources[0].error == vocabulary::Error::Unstable &&
                ready->sources[0].status == vocabulary::Status::Missing &&
                !ready->sources[0].dictionary && schedule.reads == 0,
            "original component absence loses empty-default authority when association changes");
  }
  {
    NativeSchedule schedule;
    schedule.failure = NativeSchedule::Failure::Event;
    schedule.fail_event_at = 2;
    vocabulary::Worker worker;
    Require(worker.Start(model, 31, vocabulary::ModuleOwner::Process, schedule.Hooks()),
            "stop event succeeds before forced I/O event failure");
    std::optional<vocabulary::Ready> ready;
    RuntimeWait(
        [&] {
          ready = worker.Poll();
          return ready.has_value();
        },
        "I/O event failure publishes unavailable source");
    worker.Stop();
    Require(WaitForSingleObject(schedule.finished, 15000) == WAIT_OBJECT_0 &&
                ready->sources[0].error == vocabulary::Error::Read && !ready->sources[0].dictionary,
            "I/O event creation failure owns no outstanding read and grants no default empty");
  }
  for (auto io : {NativeSchedule::Io::Immediate, NativeSchedule::Io::Pending}) {
    NativeSchedule schedule;
    schedule.io = io;
    vocabulary::Worker worker;
    Require(worker.Start(model, 32, vocabulary::ModuleOwner::Process, schedule.Hooks()),
            "deterministic completion worker starts");
    std::optional<vocabulary::Ready> ready;
    RuntimeWait(
        [&] {
          ready = worker.Poll();
          return ready && ready->sources[0].dictionary;
        },
        "immediate and simulated pending completions settle exact bytes");
    worker.Stop();
    Require(WaitForSingleObject(schedule.finished, 15000) == WAIT_OBJECT_0,
            "completion context remains alive through callback return");
    Require(ready->sources[0].status == vocabulary::Status::Ready && schedule.completions >= 8 &&
                schedule.completions == schedule.reads,
            "all data and EOF lengths come from completed byte counts");
  }
  {
    WorkerEvents old_events, new_events;
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> held{false};
    old_events.action = [&](vocabulary::Boundary at, HANDLE, size_t) {
      if (at == vocabulary::Boundary::BeforePublish && !held.exchange(true)) {
        SetEvent(entered);
        Require(WaitForSingleObject(release, 15000) == WAIT_OBJECT_0,
                "release old activation publication after new activation starts");
      }
    };
    vocabulary::Worker worker;
    Require(worker.Start(model, 40, vocabulary::ModuleOwner::Process, old_events.Hooks()) &&
                WaitForSingleObject(entered, 15000) == WAIT_OBJECT_0,
            "old activation reaches publication barrier");
    Require(worker.Start(model, 41, vocabulary::ModuleOwner::Process, new_events.Hooks()),
            "same worker object starts new detached state");
    auto ready = new_events.Next(worker);
    Require(ready.activation == 41, "new mailbox belongs to its activation");
    SetEvent(release);
    Require(WaitForSingleObject(old_events.finished, 15000) == WAIT_OBJECT_0,
            "old publication drains after new mailbox exists");
    ready = new_events.Next(worker);
    Require(ready.activation == 41 && ready.revision > 1,
            "late old publication cannot replace the new activation mailbox");
    new_events.Stop(worker);
    CloseHandle(entered);
    CloseHandle(release);
  }
}
void RuntimeReparseSchedules() {
  RuntimeFiles files, target;
  target.Write(L"en.add", "sentinel\n");
  const auto directory = files.Name(L"parent");
  Require(CreateDirectoryW(directory.c_str(), nullptr), "create owned empty reparse fixture");
  HANDLE attributes =
      CreateFileW(directory.c_str(), FILE_WRITE_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  Require(attributes != INVALID_HANDLE_VALUE, "open parent attribute mutation handle");
  const std::wstring substitute = L"\\??\\" + target.root.wstring();
  const std::wstring printable = target.root.wstring();
  struct MountPoint {
    DWORD tag;
    WORD length, reserved, substitute_offset, substitute_length, print_offset, print_length;
  };
  static_assert(sizeof(MountPoint) == 16);
  const auto substitute_bytes = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
  const auto printable_bytes = static_cast<WORD>(printable.size() * sizeof(wchar_t));
  const WORD data_length = 8 + substitute_bytes + printable_bytes + 4;
  MountPoint header{IO_REPARSE_TAG_MOUNT_POINT,
                    data_length,
                    0,
                    0,
                    substitute_bytes,
                    static_cast<WORD>(substitute_bytes + 2),
                    printable_bytes};
  std::vector<uint8_t> packet(8 + data_length, 0);
  memcpy(packet.data(), &header, sizeof(header));
  memcpy(packet.data() + 16, substitute.data(), substitute_bytes);
  memcpy(packet.data() + 18 + substitute_bytes, printable.data(), printable_bytes);
  WorkerEvents events;
  std::atomic<bool> mutated{false};
  std::atomic<unsigned> reads{0};
  const auto path = vocabulary::ParsePath(files.Name(L"parent\\en.add"));
  Require(path.has_value(), "reparse fixture path is local");
  events.action = [&](vocabulary::Boundary at, HANDLE, size_t index) {
    if (at == vocabulary::Boundary::BeforeRead)
      ++reads;
    if (at == vocabulary::Boundary::ParentOpened && index + 1 == path->components.size() &&
        !mutated.exchange(true)) {
      DWORD returned = 0;
      Require(DeviceIoControl(attributes, FSCTL_SET_REPARSE_POINT, packet.data(),
                              static_cast<DWORD>(packet.size()), nullptr, 0, &returned, nullptr),
              "attribute-capable parent mutation reaches the real filesystem");
    }
  };
  vocabulary::Worker worker;
  Require(worker.Start(vocabulary::Model{{files.Source(L"parent\\en.add", true),
                                          files.Source(L"absent.add")}},
                       11, vocabulary::ModuleOwner::Process, events.Hooks()),
          "reparse race worker starts");
  for (int observation = 0; observation != 2; ++observation) {
    const auto ready = events.Next(worker);
    Require(mutated && reads == 0 && ready.sources[0].status == vocabulary::Status::Missing &&
                ready.sources[0].error == vocabulary::Error::Unavailable &&
                !ready.sources[0].dictionary,
            "mutated and preexisting reparses cause no payload read or default-empty publication");
  }
  events.Stop(worker);
  CloseHandle(attributes);
  Require(RemoveDirectoryW(directory.c_str()), "remove only the owned junction entry");
}
void RuntimeStabilitySchedules() {
  enum class Change { Truncate, Grow, RestoreTimes };
  for (auto change : {Change::Truncate, Change::Grow, Change::RestoreTimes}) {
    RuntimeFiles files;
    files.Write(L"en.add", "hello\n");
    files.Write(L"ru.add", "");
    WorkerEvents events;
    std::atomic<bool> armed{false}, changed{false};
    events.action = [&](vocabulary::Boundary at, HANDLE file, size_t) {
      if (at != vocabulary::Boundary::BeforeRead || !armed.exchange(false))
        return;
      FILE_BASIC_INFO original{};
      Require(GetFileInformationByHandleEx(file, FileBasicInfo, &original, sizeof(original)),
              "capture real-file timestamps before mutation");
      files.Write(L"en.add", change == Change::Truncate ? "hi\n"
                             : change == Change::Grow   ? "longerword\n"
                                                        : "other\n");
      if (change == Change::RestoreTimes) {
        HANDLE attributes =
            CreateFileW(files.Name(L"en.add").c_str(), FILE_WRITE_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Require(
            attributes != INVALID_HANDLE_VALUE &&
                SetFileInformationByHandle(attributes, FileBasicInfo, &original, sizeof(original)),
            "restore real same-length rewrite timestamps through an attribute handle");
        CloseHandle(attributes);
        FILE_BASIC_INFO restored{};
        Require(GetFileInformationByHandleEx(file, FileBasicInfo, &restored, sizeof(restored)) &&
                    restored.CreationTime.QuadPart == original.CreationTime.QuadPart &&
                    restored.LastWriteTime.QuadPart == original.LastWriteTime.QuadPart &&
                    restored.ChangeTime.QuadPart == original.ChangeTime.QuadPart,
                "same-length rewrite restores creation, write, and change timestamps");
      }
      changed = true;
    };
    vocabulary::Worker worker;
    Require(worker.Start(vocabulary::Model{{files.Source(L"en.add"), files.Source(L"ru.add")}}, 51,
                         vocabulary::ModuleOwner::Process, events.Hooks()),
            "real file stability worker starts");
    events.Next(worker);
    auto before = events.Next(worker);
    Require(before.sources[0].status == vocabulary::Status::Ready && before.sources[0].dictionary,
            "stability schedule establishes last-good source");
    const auto last_good = before.sources[0].dictionary;
    armed = true;
    auto candidate = events.Next(worker);
    Require(changed && candidate.sources[0].status == vocabulary::Status::Retained &&
                candidate.sources[0].dictionary == last_good,
            "changed observation retains last-good instead of publishing new bytes");
    if (change != Change::RestoreTimes) {
      Require(candidate.sources[0].error == vocabulary::Error::Unstable,
              "truncate and growth past captured length reject the raced observation");
      candidate = events.Next(worker);
      Require(candidate.sources[0].status == vocabulary::Status::Retained &&
                  candidate.sources[0].error == vocabulary::Error::None &&
                  candidate.sources[0].dictionary == last_good,
              "first stable replacement observation still retains last-good");
    } else {
      Require(candidate.sources[0].error == vocabulary::Error::None,
              "exact-byte mismatch starts settling despite restored metadata");
    }
    auto stable = events.Next(worker);
    Require(stable.sources[0].status == vocabulary::Status::Ready && stable.sources[0].dictionary &&
                stable.sources[0].dictionary != last_good,
            "two equal new observations publish the changed real source");
    events.Stop(worker);
  }
  for (auto boundary : {vocabulary::Boundary::BeforeParse, vocabulary::Boundary::BeforePublish}) {
    RuntimeFiles files;
    files.Write(L"en.add", "hello\n");
    WorkerEvents events;
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> held{false};
    events.action = [&](vocabulary::Boundary at, HANDLE, size_t) {
      if (at == boundary && !held.exchange(true)) {
        SetEvent(entered);
        Require(WaitForSingleObject(release, 15000) == WAIT_OBJECT_0,
                "harness releases stopped parse or publication boundary");
      }
    };
    vocabulary::Worker worker;
    Require(worker.Start(vocabulary::Model{{files.Source(L"en.add"), files.Source(L"en.add")}}, 52,
                         vocabulary::ModuleOwner::Process, events.Hooks()) &&
                WaitForSingleObject(entered, 15000) == WAIT_OBJECT_0,
            "worker reaches controlled parse or publication boundary");
    worker.Stop();
    Require(WaitForSingleObject(events.finished, 0) == WAIT_TIMEOUT && !worker.Poll(),
            "Stop returns with worker still held and no owner packet");
    SetEvent(release);
    Require(WaitForSingleObject(events.finished, 15000) == WAIT_OBJECT_0 && !worker.Poll(),
            "stopped parse and publication cannot deliver a late packet");
    CloseHandle(entered);
    CloseHandle(release);
  }
}
void RuntimeFileSchedules() {
  RuntimeAbsentSourceSchedules();
  RuntimeGlobalHookSchedules();
  RuntimeNativeSchedules();
  RuntimeStabilitySchedules();
  RuntimeReparseSchedules();
  for (const wchar_t *path :
       {L"\\\\server\\share\\file", L"C:file", L"C:\\a\\..\\b", L"C:\\NUL.add", L"C:\\a:stream",
        L"C:\\a.\\b", L"C:\\a\\", L"C:\\a//b", L"C:\\\\a"})
    Require(!vocabulary::ParsePath(path), "unsafe path rejected before acquisition");
  RuntimeFiles files;
  files.Write(L"en.add", "hello\n");
  files.Write(L"ru.add", "");
  WorkerEvents events;
  vocabulary::Worker worker;
  Require(worker.Start(vocabulary::Model{{files.Source(L"en.add"), files.Source(L"ru.add")}}, 7,
                       vocabulary::ModuleOwner::Process, events.Hooks()),
          "real file worker starts");
  auto first = events.Next(worker);
  Require(first.activation == 7 && first.sources[0].status == vocabulary::Status::Pending &&
              !first.sources[0].dictionary,
          "first real observation is staged");
  auto ready = events.Next(worker);
  Require(ready.sources[0].status == vocabulary::Status::Ready &&
              ready.sources[1].status == vocabulary::Status::Ready,
          "two equal observations publish real and empty dictionaries");
  files.Write(L"en.add", std::string("\xff\nhello\n", 8));
  events.Next(worker);
  ready = events.Next(worker);
  Require(ready.sources[0].status == vocabulary::Status::Ready && ready.sources[0].dictionary,
          "invalid UTF-8 line is skipped without rejecting the valid source");
  kr_engine *probe = nullptr;
  uint32_t skipped = 0;
  Require(kr_engine_new(KR_ABI_VERSION, &probe) == KR_OK &&
              kr_adopt_dictionaries(probe, ready.sources[0].dictionary.get(), nullptr) == KR_OK &&
              kr_dictionary_skipped_lines(probe, KR_EN, &skipped) == KR_OK && skipped == 1,
          "native acquisition retains the shared parser's skipped-line count");
  kr_engine_free(probe);
  auto last_good = ready.sources[0].dictionary;
  for (const auto &bytes : {std::string("bad\0data", 8), std::string("/encoding=latin1\nword\n"),
                            std::string(1024 * 1024 + 1, 'a')}) {
    files.Write(L"en.add", bytes);
    auto changed = events.Next(worker);
    Require(changed.sources[0].status == vocabulary::Status::Retained &&
                changed.sources[0].dictionary == last_good,
            "invalid or oversized candidate retains last-good handle");
  }
  Require(DeleteFileW(files.Name(L"en.add").c_str()), "delete real public vocabulary");
  auto deleted = events.Next(worker);
  Require(deleted.sources[0].status == vocabulary::Status::Retained &&
              deleted.sources[0].dictionary == last_good,
          "disappearance retains published real file");
  events.Stop(worker);
  {
    WorkerEvents absent_events;
    vocabulary::Worker absent;
    Require(absent.Start(vocabulary::Model{{files.Source(L"missing\\default.add", true),
                                            files.Source(L"explicit.add")}},
                         8, vocabulary::ModuleOwner::Process, absent_events.Hooks()),
            "absence worker starts");
    auto packet = absent_events.Next(absent);
    Require(packet.sources[0].status == vocabulary::Status::Ready && packet.sources[0].dictionary &&
                packet.sources[1].status == vocabulary::Status::Missing &&
                !packet.sources[1].dictionary,
            "default missing parent is empty and explicit absence unavailable");
    absent_events.Stop(absent);
  }
  {
    files.Write(L"en.add", "before\n");
    files.Write(L"new.add", "after\n");
    WorkerEvents replacement;
    bool exchanged = false;
    replacement.action = [&](vocabulary::Boundary at, HANDLE, size_t) {
      if (at == vocabulary::Boundary::BeforeRead && !exchanged) {
        exchanged = true;
        Require(ReplaceFileW(files.Name(L"en.add").c_str(), files.Name(L"new.add").c_str(), nullptr,
                             0, nullptr, nullptr),
                "atomic final replacement allowed while file handle is open");
      }
    };
    vocabulary::Worker replace;
    Require(replace.Start(vocabulary::Model{{files.Source(L"en.add"), files.Source(L"ru.add")}}, 9,
                          vocabulary::ModuleOwner::Process, replacement.Hooks()),
            "replacement worker starts");
    auto rejected = replacement.Next(replace);
    Require(exchanged && rejected.sources[0].status == vocabulary::Status::Missing,
            "replaced identity rejects old observation");
    Require(replacement.Next(replace).sources[0].status == vocabulary::Status::Pending,
            "replacement must settle again");
    Require(replacement.Next(replace).sources[0].status == vocabulary::Status::Ready,
            "stable replacement publishes");
    replacement.Stop(replace);
  }
  {
    WorkerEvents parents;
    bool attempted = false;
    auto path = vocabulary::ParsePath(files.Name(L"en.add"));
    parents.action = [&](vocabulary::Boundary at, HANDLE, size_t index) {
      if (at == vocabulary::Boundary::ParentOpened && index + 1 == path->components.size() &&
          !attempted) {
        attempted = true;
        Require(!MoveFileExW(files.root.c_str(), files.Name(L"..\\renamed").c_str(), 0),
                "retained parent denies rename before child open");
      }
    };
    vocabulary::Worker held;
    Require(held.Start(vocabulary::Model{{files.Source(L"en.add"), files.Source(L"ru.add")}}, 10,
                       vocabulary::ModuleOwner::Process, parents.Hooks()),
            "parent handle worker starts");
    parents.Next(held);
    Require(attempted, "parent rename boundary exercised");
    parents.Stop(held);
  }
}

struct DllBarrier {
  vocabulary::Boundary boundary;
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::atomic<bool> used{false};
  ~DllBarrier() {
    CloseHandle(entered);
    CloseHandle(release);
  }
  static void Hook(void *context, vocabulary::Boundary at, HANDLE, size_t) {
    auto &barrier = *static_cast<DllBarrier *>(context);
    if (at == barrier.boundary && !barrier.used.exchange(true)) {
      SetEvent(barrier.entered);
      Require(WaitForSingleObject(barrier.release, 15000) == WAIT_OBJECT_0,
              "host releases actual DLL callback barrier");
    }
  }
};
bool RuntimeModuleLoaded(const wchar_t *path) {
  HMODULE module = nullptr;
  return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, path, &module) && module;
}
void RuntimeDllNativeSchedules(const wchar_t *path) {
  RuntimeFiles files;
  files.Write(L"en.add", "hello\n");
  RuntimeEnvironment policy(L"KEYRILL_WINDOWS_POLICY", L"model");
  RuntimeEnvironment en(L"KEYRILL_WINDOWS_EN_DICTIONARY", files.Name(L"en.add").c_str());
  RuntimeEnvironment ru(L"KEYRILL_WINDOWS_RU_DICTIONARY", files.Name(L"en.add").c_str());
  for (auto failure : {NativeSchedule::Failure::Event, NativeSchedule::Failure::Pin,
                       NativeSchedule::Failure::Submit, NativeSchedule::Failure::LongRunning,
                       NativeSchedule::Failure::None}) {
    HMODULE dll = LoadLibraryW(path);
    Require(dll != nullptr, "load actual DLL for deterministic native boundary schedules");
    auto set_hooks = GetProcAddress(dll, "KeyrillTestWorkerHooks");
    if (!set_hooks) {
      FreeLibrary(dll);
      return;
    }
    auto get = reinterpret_cast<HRESULT(WINAPI *)(REFCLSID, REFIID, void **)>(
        GetProcAddress(dll, "DllGetClassObject"));
    auto unload = reinterpret_cast<HRESULT(WINAPI *)()>(GetProcAddress(dll, "DllCanUnloadNow"));
    NativeSchedule schedule;
    schedule.failure = failure;
    if (failure == NativeSchedule::Failure::None)
      schedule.io = NativeSchedule::Io::Drain;
    Require(vocabulary::SetTestHooks(dll, schedule.Hooks()), "Rust worker native hooks accepted");
    Document document;
    Manager manager;
    manager.top = &document;
    ComPtr<IClassFactory> factory;
    ComPtr<ITfTextInputProcessorEx> service;
    ComPtr<runtime::Reader> reader;
    Require(get && unload && get(service_id, IID_PPV_ARGS(&factory)) == S_OK &&
                factory->CreateInstance(nullptr, IID_PPV_ARGS(&service)) == S_OK &&
                service->Activate(&manager, 42) == S_OK &&
                service->QueryInterface(runtime::reader_iid,
                                        reinterpret_cast<void **>(reader.GetAddressOf())) == S_OK,
            "actual DLL activates with harness-owned native dependencies");
    if (failure == NativeSchedule::Failure::None) {
      Require(WaitForSingleObject(schedule.entered, 15000) == WAIT_OBJECT_0,
              "simulated pending read enters inside actual worker DLL");
      Require(service->Deactivate() == S_OK,
              "owner stop returns while simulated completion remains held");
      Require(WaitForSingleObject(schedule.draining, 15000) == WAIT_OBJECT_0 &&
                  WaitForSingleObject(schedule.cancelled, 0) == WAIT_OBJECT_0 &&
                  !schedule.completion_returned &&
                  WaitForSingleObject(schedule.operation->hEvent, 0) == WAIT_TIMEOUT,
              "cancellation request leaves I/O storage owned until delayed completion");
      DWORD flags = 0;
      Require(GetHandleInformation(schedule.file, &flags) &&
                  GetHandleInformation(schedule.operation->hEvent, &flags),
              "pending file and event survive nonblocking owner stop");
      reader.Reset();
      service.Reset();
      factory.Reset();
      Require(unload() == S_FALSE, "pending drainage holds real DLL worker ownership");
      Require(vocabulary::ClearTestHooks(dll), "global hooks release before caller DLL reference");
      FreeLibrary(dll);
      Require(RuntimeModuleLoaded(path), "actual DLL remains pinned during delayed completion");
      SetEvent(schedule.release);
      RuntimeWait([&] { return !RuntimeModuleLoaded(path); },
                  "actual DLL unload follows delayed completion and callback return");
      Require(schedule.completion_returned && schedule.completions == 1 && schedule.reads == 1,
              "one cancellation drains one operation before releasing storage and module");
    } else {
      RuntimeWait(
          [&] {
            runtime::Snapshot value;
            return reader->Copy(sizeof(value), runtime::version, &value) == S_OK &&
                   value.error == vocabulary::Error::Startup && value.effective == KR_POLICY_OFF &&
                   !value.model_ready;
          },
          "forced startup failure stays manual-only with visible content-free error");
      Require(schedule.events == 1 && schedule.opens == 0 && schedule.reads == 0,
              "startup failure grants no acquisition or I/O authority");
      Require(service->Deactivate() == S_OK, "failed startup deactivates without joins");
      reader.Reset();
      service.Reset();
      factory.Reset();
      RuntimeWait([&] { return unload() == S_OK; }, "startup failure releases worker count");
      Require(vocabulary::ClearTestHooks(dll),
              "failed startup releases global hooks before unload");
      FreeLibrary(dll);
      RuntimeWait([&] { return !RuntimeModuleLoaded(path); },
                  "startup failure releases every acquired module reference");
    }
  }
}
void RuntimeDllSchedules(const wchar_t *path) {
  RuntimeDllNativeSchedules(path);
  RuntimeFiles files;
  files.Write(L"en.add", "");
  files.Write(L"ru.add", "");
  RuntimeEnvironment policy(L"KEYRILL_WINDOWS_POLICY", L"model");
  RuntimeEnvironment en(L"KEYRILL_WINDOWS_EN_DICTIONARY", files.Name(L"en.add").c_str());
  RuntimeEnvironment ru(L"KEYRILL_WINDOWS_RU_DICTIONARY", files.Name(L"ru.add").c_str());
  for (auto boundary : {vocabulary::Boundary::BeforeWork, vocabulary::Boundary::BeforeRead,
                        vocabulary::Boundary::Cleanup, vocabulary::Boundary::AfterCount}) {
    HMODULE dll = LoadLibraryW(path);
    Require(dll != nullptr, "load actual runtime DLL");
    auto get = reinterpret_cast<HRESULT(WINAPI *)(REFCLSID, REFIID, void **)>(
        GetProcAddress(dll, "DllGetClassObject"));
    auto unload = reinterpret_cast<HRESULT(WINAPI *)()>(GetProcAddress(dll, "DllCanUnloadNow"));
    auto set_hooks = GetProcAddress(dll, "KeyrillTestWorkerHooks");
    DllBarrier barrier{boundary};
    if (set_hooks)
      Require(vocabulary::SetTestHooks(dll, {DllBarrier::Hook, &barrier}),
              "Rust worker barrier hook accepted");
    Document document;
    Manager manager;
    manager.top = &document;
    ComPtr<IClassFactory> factory;
    ComPtr<ITfTextInputProcessorEx> service;
    ComPtr<runtime::Reader> reader;
    Require(get && unload && get(service_id, IID_PPV_ARGS(&factory)) == S_OK &&
                factory->CreateInstance(nullptr, IID_PPV_ARGS(&service)) == S_OK &&
                service->Activate(&manager, 42) == S_OK,
            "actual DLL service activation starts production worker");
    Require(service->QueryInterface(runtime::reader_iid,
                                    reinterpret_cast<void **>(reader.GetAddressOf())) == S_OK,
            "actual DLL readiness interface");
    if (set_hooks && (boundary == vocabulary::Boundary::BeforeWork ||
                      boundary == vocabulary::Boundary::BeforeRead)) {
      Require(WaitForSingleObject(barrier.entered, 15000) == WAIT_OBJECT_0,
              "actual worker held before cleanup");
    } else {
      RuntimeWait(
          [&] {
            runtime::Snapshot value;
            return reader->Copy(sizeof(value), runtime::version, &value) == S_OK &&
                   value.effective == KR_POLICY_MODEL;
          },
          "actual DLL adopts prepared model");
    }
    Require(unload() == S_FALSE, "COM and worker prohibit unload");
    Require(service->Deactivate() == S_OK, "deactivation never joins held worker");
    reader.Reset();
    service.Reset();
    factory.Reset();
    if (set_hooks &&
        (boundary == vocabulary::Boundary::Cleanup || boundary == vocabulary::Boundary::AfterCount))
      Require(WaitForSingleObject(barrier.entered, 15000) == WAIT_OBJECT_0,
              "actual worker held at final cleanup or epilogue");
    if (set_hooks)
      Require(unload() == (boundary == vocabulary::Boundary::AfterCount ? S_OK : S_FALSE),
              "worker accounting and deferred release are separate");
    if (set_hooks)
      Require(vocabulary::ClearTestHooks(dll), "global hooks retire before last caller reference");
    FreeLibrary(dll);
    if (set_hooks) {
      Require(RuntimeModuleLoaded(path),
              "actual module stays mapped after last external reference");
      SetEvent(barrier.release);
    }
    RuntimeWait([&] { return !RuntimeModuleLoaded(path); },
                "actual module disappears after callback return");
    if (!set_hooks)
      break;
  }
}
