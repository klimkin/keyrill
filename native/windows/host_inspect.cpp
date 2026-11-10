#include "host_build.h"
#include "host_inspect_options.h"
#include "host_probe.h"
#include "host_threads.h"
#include <cstdio>
#include <memory>
#include <tlhelp32.h>
#include <vector>

using namespace keyrill;
namespace {
std::vector<DWORD> Threads(DWORD pid) {
  std::vector<DWORD> result;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return result;
  THREADENTRY32 thread{sizeof(thread)};
  if (Thread32First(snapshot, &thread))
    do {
      if (thread.th32OwnerProcessID == pid) {
        if (result.size() == 64) {
          result.clear();
          break;
        }
        result.push_back(thread.th32ThreadID);
      }
    } while (Thread32Next(snapshot, &thread));
  CloseHandle(snapshot);
  return result;
}
void Print(const host::Identity &id, probe::Availability state, const probe::Payload &words,
           const inspection::BuildDigest &expected) {
  const char *labels[] = {"Available", "Missing", "Busy", "Incompatible", "Stale"};
  printf("thread=%lu availability=%s", id.thread, labels[static_cast<size_t>(state)]);
  if (state == probe::Availability::Available) {
    probe::Snapshot value;
    if (!probe::Decode(words, value))
      return;
    const auto &m = value.metadata;
    const auto &s = value.service.base;
    const auto &l = value.service.lifecycle;
    const auto &r = value.runtime;
    auto count = [&](diagnostics::Counter counter) {
      return s.counts[static_cast<size_t>(counter)];
    };
    printf(" instance=%lu kind=%u build_match=%u build=", value.instance,
           static_cast<unsigned>(value.kind), value.build == expected);
    for (auto part : value.build)
      printf("%08x", part);
    printf(" reason=%s result=%08x focus=%u scope=%u request=%08x session=%08x"
           " entered=%lu completed=%lu selection=%lu acp=%lu activation=%lu active=%lu"
           " stopped=%lu mode=%lu test=%lu interested=%lu key=%lu consumed=%lu handle=%lu"
           " requested=%lu session_entered=%lu session_completed=%lu"
           " attempted=%lu verified=%lu refused=%lu unknown=%lu"
           " owned=%u serial=%u lifecycle_sequence=%u lifecycle_count=%u"
           " requested_policy=%u effective_policy=%u model=%u revision=%llu"
           " en_state=%u en_error=%u ru_state=%u ru_error=%u\n",
           host::reason_labels[static_cast<size_t>(m.reason)], m.result,
           static_cast<unsigned>(m.focus), static_cast<unsigned>(m.scope), m.request, m.session,
           static_cast<unsigned long>(m.entered), static_cast<unsigned long>(m.completed),
           static_cast<unsigned long>(m.selection), static_cast<unsigned long>(m.acp),
           static_cast<unsigned long>(s.activation), static_cast<unsigned long>(s.active),
           static_cast<unsigned long>(s.stopped), static_cast<unsigned long>(s.mode),
           static_cast<unsigned long>(count(diagnostics::Counter::TestEntered)),
           static_cast<unsigned long>(count(diagnostics::Counter::Interested)),
           static_cast<unsigned long>(count(diagnostics::Counter::KeyEntered)),
           static_cast<unsigned long>(count(diagnostics::Counter::Consumed)),
           static_cast<unsigned long>(count(diagnostics::Counter::HandleEntered)),
           static_cast<unsigned long>(count(diagnostics::Counter::Requested)),
           static_cast<unsigned long>(count(diagnostics::Counter::SessionEntered)),
           static_cast<unsigned long>(count(diagnostics::Counter::SessionCompleted)),
           static_cast<unsigned long>(count(diagnostics::Counter::Attempted)),
           static_cast<unsigned long>(count(diagnostics::Counter::Verified)),
           static_cast<unsigned long>(count(diagnostics::Counter::Refused)),
           static_cast<unsigned long>(count(diagnostics::Counter::Unknown)), l.owned_present,
           l.owned_serial, l.sequence, l.count, r.requested, r.effective, r.model_ready, r.revision,
           static_cast<unsigned>(r.sources[0]), static_cast<unsigned>(r.source_errors[0]),
           static_cast<unsigned>(r.sources[1]), static_cast<unsigned>(r.source_errors[1]));
    for (size_t i = 0; i != l.count; ++i) {
      const auto &event = l.events[i];
      printf(" lifecycle=%u kind=%u subject=%u cause=%u unresolved=%u\n", event.sequence,
             static_cast<unsigned>(event.kind), event.subject, static_cast<unsigned>(event.cause),
             static_cast<unsigned>(event.unresolved));
    }
    for (size_t i = 0; i != s.refusals.size(); ++i)
      if (s.refusals[i])
        printf(" refusal_count_stage=%ls count=%lu\n", diagnostics::stage_labels[i],
               static_cast<unsigned long>(s.refusals[i]));
    if (s.first.present)
      printf(" first_refusal_stage=%ls cause=%ls result=%08x\n",
             diagnostics::stage_labels[static_cast<size_t>(s.first.stage)],
             diagnostics::cause_labels[static_cast<size_t>(s.first.cause)], s.first.result);
    if (s.latest.present)
      printf(" refusal_stage=%ls cause=%ls result=%08x\n",
             diagnostics::stage_labels[static_cast<size_t>(s.latest.stage)],
             diagnostics::cause_labels[static_cast<size_t>(s.latest.cause)], s.latest.result);
  } else {
    puts(" (unavailable does not establish inactivity)");
  }
}
probe::Availability Existing(const host::Identity &id, probe::Payload &payload) {
  wchar_t name[128]{};
  if (!probe::Name(id, name))
    return probe::Availability::Incompatible;
  HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
  if (!mapping)
    return probe::Availability::Missing;
  const auto *page = static_cast<const probe::Page *>(
      MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, probe::page_bytes));
  const auto state = page ? probe::Read(*page, id, payload) : probe::Availability::Incompatible;
  if (page)
    UnmapViewOfFile(page);
  CloseHandle(mapping);
  return state;
}
void PrintWindow(const char *role, const field::Window &window) {
  printf("%s hwnd=%p pid=%lu tid=%lu control=%d style=%016llx exstyle=%016llx"
         " enabled=%u complete=%u class_truncated=%u class=%ls\n",
         role, static_cast<void *>(window.handle), window.pid, window.tid, window.control,
         static_cast<unsigned long long>(window.style),
         static_cast<unsigned long long>(window.exstyle), window.enabled, window.complete,
         window.class_truncated, window.class_name.data());
}
void PrintField(const field::Metadata &value) {
  const auto &native = value.native;
  printf("field refusal=%s pid=%lu foreground_tid=%lu process_started=%016llx"
         " foreground_thread_started=%016llx focused_thread_started=%016llx"
         " foreground=%p parents=%zu parents_truncated=%u\n",
         field::refusal_labels[static_cast<size_t>(value.refusal)], native.pid,
         native.foreground_tid, native.process_started, native.foreground_thread_started,
         native.focused_thread_started, static_cast<void *>(native.foreground), native.parent_count,
         native.parents_truncated);
  PrintWindow("focused", native.focused);
  PrintWindow("root", native.root);
  for (size_t i = 0; i != native.parent_count; ++i)
    PrintWindow("parent", native.parents[i]);
  printf("uia pid=%d hwnd=%p control_type=%d exact_binding=%u nonpassword=%u enabled=%u"
         " value_readonly=%s text_readonly=%s text_pattern=%u\n",
         value.provider_pid, static_cast<void *>(value.provider_window), value.control_type,
         value.exact_binding, value.nonpassword, value.enabled,
         field::readonly_labels[static_cast<size_t>(value.value_readonly)],
         field::readonly_labels[static_cast<size_t>(value.text_readonly)], value.text_pattern);
}
void NativeMetadata(DWORD pid, const std::vector<DWORD> &threads) {
  for (auto tid : threads) {
    GUITHREADINFO info{sizeof(info)};
    if (!GetGUIThreadInfo(tid, &info) || !info.hwndFocus)
      continue;
    DWORD owner = 0;
    if (GetWindowThreadProcessId(info.hwndFocus, &owner) != tid || owner != pid)
      continue;
    wchar_t name[128]{};
    if (GetClassNameW(info.hwndFocus, name, static_cast<int>(std::size(name))))
      printf("thread=%lu focused_class=%ls\n", tid, name);
  }
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  inspection::Options options;
  if (!inspection::ParseOptions(argc, argv, options)) {
    puts("Usage: KeyrillHostInspect build | host|field PID | public-case PID"
         " empty|probe-g|fixture-en-boundary|fixture-en-corrected|fixture-en-word|fixture-ru-word|"
         "fixture-continuation|fixture-two-words|manual-provisional|manual-provisional-literal|"
         "manual-pc|manual-pc-literal|model-en|model-en-literal|model-ru|model-ru-literal|prefix-"
         "suffix"
         " | service PID"
         " [--arm-seconds 1..3600] [--expect-server-build SHA256]");
    return 2;
  }
  if (options.command == inspection::Command::Build) {
    for (auto part : probe::build_digest)
      printf("%08x", part);
    puts("");
    return 0;
  }
  const DWORD pid = options.pid, seconds = options.seconds;
  const auto expected = options.expected_build.value_or(probe::build_digest);
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
  host::Identity id;
  if (!process || !host::Capture(process, pid, 0, id) || id.kind == host::Kind::Unknown ||
      WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
    puts("explicit target unavailable, incompatible, or outside diagnostic candidate set");
    if (process)
      CloseHandle(process);
    return 1;
  }
  const auto threads = Threads(pid);
  if (options.command != inspection::Command::PublicCase)
    printf("pid=%lu started=%016llx kind=%u version=%u.%u.%u.%u threads=%zu\n", pid, id.started,
           static_cast<unsigned>(id.kind), id.version[0], id.version[1], id.version[2],
           id.version[3], threads.size());
  if (options.command == inspection::Command::Host) {
    NativeMetadata(pid, threads);
    CloseHandle(process);
    return threads.empty() ? 1 : 0;
  }
  if (options.command == inspection::Command::Field ||
      options.command == inspection::Command::PublicCase) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool success = false;
    if (SUCCEEDED(initialized)) {
      const field::DiagnosticTarget target{process, id};
      if (options.command == inspection::Command::Field) {
        const auto metadata = field::Observe(target);
        PrintField(metadata);
        success = metadata.refusal == field::Refusal::None;
      } else {
        const auto result = field::CheckPublicCase(target, options.public_case);
        printf("public-case case=%s matched=%u refusal=%s\n", field::CaseName(options.public_case),
               result.matched, field::refusal_labels[static_cast<size_t>(result.refusal)]);
        success = result.matched;
      }
    } else {
      puts("field provider initialization unavailable before text read");
    }
    if (SUCCEEDED(initialized))
      CoUninitialize();
    CloseHandle(process);
    return success ? 0 : 1;
  }
  printf("observer_native_build=");
  for (auto part : probe::build_digest)
    printf("%08x", part);
  printf(" expected_service_build=");
  for (auto part : expected)
    printf("%08x", part);
  printf(" expected_explicit=%u\n", options.expected_build.has_value());
  std::vector<inspection::ThreadCheck> live_threads;
  std::vector<std::unique_ptr<probe::ArmedPage>> armed;
  if (threads.empty()) {
    CloseHandle(process);
    return 1;
  }
  for (auto tid : threads) {
    auto checked = inspection::CaptureThread(process, pid, tid);
    if (checked.state == inspection::ThreadState::Live) {
      live_threads.push_back(std::move(checked));
      continue;
    }
    printf("pid=%lu started=%016llx thread=%lu capture=%s owner=%lu error=%lu"
           " thread_wait=%08lx process_wait=%08lx\n",
           pid, id.started, tid, inspection::thread_labels[static_cast<size_t>(checked.state)],
           checked.owner, checked.error, checked.wait, checked.process_wait);
    if (checked.state != inspection::ThreadState::Exited &&
        checked.state != inspection::ThreadState::Missing) {
      CloseHandle(process);
      return 1;
    }
  }
  if (live_threads.empty()) {
    puts("no live target threads captured; no mapping armed");
    CloseHandle(process);
    return 1;
  }
  if (seconds) {
    for (const auto &thread : live_threads) {
      if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        puts("target stale before arming; no further mapping created");
        CloseHandle(process);
        return 1;
      }
      id.thread = thread.tid;
      auto page = std::make_unique<probe::ArmedPage>();
      if (!page->Create(id)) {
        puts("arming unavailable or collision; no existing mapping changed");
        CloseHandle(process);
        return 1;
      }
      armed.push_back(std::move(page));
    }
    printf("armed=%zu; activate source now; newly created threads require explicit rearming\n",
           armed.size());
  }
  bool available = false;
  std::vector<probe::Payload> previous(live_threads.size());
  std::vector<probe::Availability> previous_states(live_threads.size(),
                                                   probe::Availability::Missing);
  for (DWORD iteration = 0; iteration <= seconds; ++iteration) {
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
      puts("target stale; retained mappings are unavailable evidence");
      CloseHandle(process);
      return 1;
    }
    for (size_t i = 0; i != live_threads.size(); ++i) {
      id.thread = live_threads[i].tid;
      probe::Payload payload{};
      const auto state = WaitForSingleObject(live_threads[i].handle.get(), 0) != WAIT_TIMEOUT
                             ? probe::Availability::Stale
                         : seconds ? armed[i]->Read(payload)
                                   : Existing(id, payload);
      if (!iteration || payload != previous[i] || state != previous_states[i])
        Print(id, state, payload, expected);
      previous[i] = payload;
      previous_states[i] = state;
      probe::Snapshot decoded;
      available |= state == probe::Availability::Available && probe::Decode(payload, decoded) &&
                   decoded.build == expected;
    }
    if (iteration < seconds)
      Sleep(1000);
  }
  CloseHandle(process);
  return available ? 0 : 1;
}
