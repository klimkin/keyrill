#include "host_threads.h"
#include <cstdio>
#include <cstdlib>

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
} // namespace
int main() {
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
    Require(missing.state == ThreadState::Missing && missing.error == ERROR_INVALID_PARAMETER,
            "vanished snapshot thread is classified without accepting an API error");
  }
  CloseHandle(event);
  puts("host thread startup schedules passed");
}
