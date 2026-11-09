#include "host_build.h"
#include "host_probe.h"
#include <cstdio>
#include <memory>
#include <tlhelp32.h>
#include <uiautomation.h>
#include <vector>

using namespace keyrill;
using Microsoft::WRL::ComPtr;
namespace {
bool Number(const wchar_t *text, DWORD &value) {
  wchar_t *end = nullptr;
  if (!text || !*text || *text == L'-' || *text == L'+')
    return false;
  const auto parsed = wcstoul(text, &end, 10);
  if (!end || *end || parsed == 0 || parsed == MAXDWORD)
    return false;
  value = parsed;
  return true;
}
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
void Print(const host::Identity &id, probe::Availability state, const probe::Payload &words) {
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
           static_cast<unsigned>(value.kind), value.build == probe::build_digest);
    for (auto part : value.build)
      printf("%08x", part);
    printf(" reason=%s result=%08x focus=%u scope=%u request=%08x session=%08x"
           " entered=%lu completed=%lu selection=%lu acp=%lu activation=%lu active=%lu"
           " stopped=%lu mode=%lu test=%lu key=%lu attempted=%lu verified=%lu unknown=%lu"
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
           static_cast<unsigned long>(count(diagnostics::Counter::KeyEntered)),
           static_cast<unsigned long>(count(diagnostics::Counter::Attempted)),
           static_cast<unsigned long>(count(diagnostics::Counter::Verified)),
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
bool Empty(DWORD pid) {
  ComPtr<IUIAutomation> automation;
  ComPtr<IUIAutomationElement> element;
  ComPtr<IUIAutomationTextPattern> text;
  int actual_pid = 0;
  BOOL password = TRUE, enabled = FALSE;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&automation))) ||
      FAILED(automation->GetFocusedElement(&element)) || !element ||
      FAILED(element->get_CurrentProcessId(&actual_pid)) || actual_pid != static_cast<int>(pid) ||
      FAILED(element->get_CurrentIsPassword(&password)) || password ||
      FAILED(element->get_CurrentIsEnabled(&enabled)) || !enabled ||
      FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text)))) {
    puts("public-empty oracle unavailable before text read");
    return false;
  }
  ComPtr<IUIAutomationTextRange> document, selection_end, document_end;
  ComPtr<IUIAutomationTextRangeArray> selections;
  BSTR value = nullptr;
  if (FAILED(text->get_DocumentRange(&document)) || !document ||
      FAILED(document->GetText(2, &value)))
    return false;
  const UINT length = SysStringLen(value);
  SysFreeString(value);
  int count = 0;
  if (FAILED(text->GetSelection(&selections)) || !selections ||
      FAILED(selections->get_Length(&count)) || count != 1 ||
      FAILED(selections->GetElement(0, &selection_end)) || !selection_end ||
      FAILED(document->Clone(&document_end)))
    return false;
  int collapsed = 1, at_end = 1;
  if (FAILED(selection_end->CompareEndpoints(TextPatternRangeEndpoint_Start, selection_end.Get(),
                                             TextPatternRangeEndpoint_End, &collapsed)) ||
      FAILED(selection_end->CompareEndpoints(TextPatternRangeEndpoint_End, document_end.Get(),
                                             TextPatternRangeEndpoint_End, &at_end)))
    return false;
  printf("public-empty length=%u collapsed=%u caret_at_end=%u\n", length, collapsed == 0,
         at_end == 0);
  return length == 0 && collapsed == 0 && at_end == 0;
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
  if (argc == 2 && wcscmp(argv[1], L"build") == 0) {
    for (auto part : probe::build_digest)
      printf("%08x", part);
    puts("");
    return 0;
  }
  DWORD pid = 0, seconds = 0;
  if (argc < 3 || argc > 5 || !Number(argv[2], pid) ||
      (wcscmp(argv[1], L"host") && wcscmp(argv[1], L"service") &&
       wcscmp(argv[1], L"public-empty")) ||
      (argc != 3 &&
       (argc != 5 || wcscmp(argv[1], L"service") || wcscmp(argv[3], L"--arm-seconds") ||
        !Number(argv[4], seconds) || seconds > 3600))) {
    puts("Usage: KeyrillHostInspect build | host|service|public-empty PID [--arm-seconds 1..3600]");
    return 2;
  }
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
  printf("pid=%lu started=%016llx kind=%u version=%u.%u.%u.%u threads=%zu\n", pid, id.started,
         static_cast<unsigned>(id.kind), id.version[0], id.version[1], id.version[2], id.version[3],
         threads.size());
  if (wcscmp(argv[1], L"host") == 0) {
    NativeMetadata(pid, threads);
    CloseHandle(process);
    return threads.empty() ? 1 : 0;
  }
  if (wcscmp(argv[1], L"public-empty") == 0) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool matched = SUCCEEDED(initialized) && Empty(pid);
    if (SUCCEEDED(initialized))
      CoUninitialize();
    CloseHandle(process);
    return matched ? 0 : 1;
  }
  std::vector<std::unique_ptr<probe::ArmedPage>> armed;
  struct ThreadHandles {
    std::vector<HANDLE> values;
    ~ThreadHandles() {
      for (auto handle : values)
        CloseHandle(handle);
    }
  } handles;
  if (threads.empty()) {
    CloseHandle(process);
    return 1;
  }
  for (auto tid : threads) {
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, tid);
    if (!thread || GetProcessIdOfThread(thread) != pid ||
        WaitForSingleObject(thread, 0) != WAIT_TIMEOUT) {
      if (thread)
        CloseHandle(thread);
      puts("target thread unavailable before arming");
      CloseHandle(process);
      return 1;
    }
    handles.values.push_back(thread);
  }
  if (seconds) {
    for (auto tid : threads) {
      id.thread = tid;
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
  std::vector<probe::Payload> previous(threads.size());
  std::vector<probe::Availability> previous_states(threads.size(), probe::Availability::Missing);
  for (DWORD iteration = 0; iteration <= seconds; ++iteration) {
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
      puts("target stale; retained mappings are unavailable evidence");
      CloseHandle(process);
      return 1;
    }
    for (size_t i = 0; i != threads.size(); ++i) {
      id.thread = threads[i];
      probe::Payload payload{};
      const auto state = WaitForSingleObject(handles.values[i], 0) != WAIT_TIMEOUT
                             ? probe::Availability::Stale
                         : seconds ? armed[i]->Read(payload)
                                   : Existing(id, payload);
      if (!iteration || payload != previous[i] || state != previous_states[i])
        Print(id, state, payload);
      previous[i] = payload;
      previous_states[i] = state;
      probe::Snapshot decoded;
      available |= state == probe::Availability::Available && probe::Decode(payload, decoded) &&
                   decoded.build == probe::build_digest;
    }
    if (iteration < seconds)
      Sleep(1000);
  }
  CloseHandle(process);
  return available ? 0 : 1;
}
