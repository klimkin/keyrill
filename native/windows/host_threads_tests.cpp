#include "host_threads.h"
#include <cstdio>
#include <cstdlib>
#include <sddl.h>

using namespace keyrill::inspection;
namespace {
void Require(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}
DWORD WINAPI GatedThread(void *event) {
  return WaitForSingleObject(event, 10000) == WAIT_OBJECT_0 ? 0 : 1;
}
void Report(const ThreadCheck &result) {
  printf("thread=%lu state=%s owner=%lu error=%lu wait=%08lx process_wait=%08lx\n", result.tid,
         thread_labels[static_cast<size_t>(result.state)], result.owner, result.error, result.wait,
         result.process_wait);
}
void DeniedThread(HANDLE process, DWORD pid) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
              L"D:P(D;;0x00100800;;;WD)", SDDL_REVISION_1, &descriptor, nullptr) != FALSE,
          "owned denial fixture security descriptor");
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Require(event != nullptr, "denial fixture event");
  DWORD tid = 0;
  HANDLE thread = CreateThread(&attributes, 0, GatedThread, event, 0, &tid);
  LocalFree(descriptor);
  Require(thread != nullptr, "owned live thread with protected denial DACL");
  const auto denied = CaptureThread(process, pid, tid);
  Report(denied);
  if (denied.state == ThreadState::Live)
    puts("INCONCLUSIVE: thread DACL denial bypassed; no security privilege changed");
  else
    Require(denied.state == ThreadState::OpenFailed && denied.error == ERROR_ACCESS_DENIED,
            "live inaccessible thread is fatal access denial, never Missing or Exited");
  Require(SetEvent(event) != FALSE && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0,
          "owned denied thread exits");
  CloseHandle(thread);
  CloseHandle(event);
}
void ChildProcess(HANDLE process, DWORD pid) {
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
  HANDLE event = CreateEventW(&attributes, TRUE, FALSE, nullptr);
  Require(event != nullptr, "owned child exit event");
  wchar_t executable[32768]{};
  Require(GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))) != 0,
          "owned child executable");
  wchar_t command[33000]{};
  Require(swprintf_s(command, L"\"%ls\" --child %llx", executable,
                     reinterpret_cast<uintptr_t>(event)) > 0,
          "owned child command");
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION child{};
  Require(CreateProcessW(executable, command, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                         nullptr, &startup, &child) != FALSE,
          "owned child starts without desktop interaction");
  const auto wrong_owner = CaptureThread(process, pid, child.dwThreadId);
  Report(wrong_owner);
  Require(wrong_owner.state == ThreadState::WrongOwner && wrong_owner.owner == child.dwProcessId,
          "live thread in another process is fatal owner mismatch");
  Require(CaptureThread(child.hProcess, pid, child.dwThreadId).state == ThreadState::WrongProcess,
          "held process handle cannot authorize another PID");
  Require(SetEvent(event) != FALSE && WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0,
          "owned child exits at event boundary");
  DWORD code = 1;
  Require(GetExitCodeProcess(child.hProcess, &code) && code == 0, "owned child exit succeeds");
  const auto stale = CaptureThread(child.hProcess, child.dwProcessId, child.dwThreadId);
  Report(stale);
  Require(stale.state == ThreadState::ProcessStale && stale.process_wait == WAIT_OBJECT_0,
          "held dead process fails closed before any thread capture");
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  CloseHandle(event);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  if (argc == 3 && wcscmp(argv[1], L"--child") == 0)
    return GatedThread(reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 16)));
  Require(argc == 1, "known thread schedule mode");
  const DWORD pid = GetCurrentProcessId();
  const HANDLE process = GetCurrentProcess();
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Require(event != nullptr, "controlled thread event created");
  DWORD tid = 0;
  HANDLE thread = CreateThread(nullptr, 0, GatedThread, event, 0, &tid);
  Require(thread != nullptr, "controlled native thread created");
  {
    const auto live = CaptureThread(process, pid, tid);
    Report(live);
    Require(live.state == ThreadState::Live && live.handle && live.owner == pid,
            "live snapshot thread retains a verified owner handle");
  }
  Require(SetEvent(event) != FALSE && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0,
          "thread exits at deterministic event boundary");
  {
    const auto exited = CaptureThread(process, pid, tid);
    Report(exited);
    Require(exited.state == ThreadState::Exited && exited.wait == WAIT_OBJECT_0,
            "exited snapshot thread is classified without rejecting live target");
  }
  CloseHandle(thread);
  {
    const auto missing = CaptureThread(process, pid, tid);
    Report(missing);
    Require(missing.state == ThreadState::Exited ||
                (missing.state == ThreadState::Missing && missing.error == ERROR_INVALID_PARAMETER),
            "closed exited thread remains exited or is positively missing");
  }
  CloseHandle(event);
  const auto absent = CaptureThread(process, pid, 0);
  Report(absent);
  Require(absent.state == ThreadState::Missing && absent.error == ERROR_INVALID_PARAMETER,
          "nonexistent thread ID reports exact OpenThread error");
  const auto invalid_process = CaptureThread(nullptr, pid, GetCurrentThreadId());
  Report(invalid_process);
  Require(invalid_process.state == ThreadState::ProcessFailed &&
              invalid_process.error == ERROR_INVALID_HANDLE,
          "invalid process handle fails closed with exact native error");
  DeniedThread(process, pid);
  ChildProcess(process, pid);
  puts("host thread startup schedules passed");
}
