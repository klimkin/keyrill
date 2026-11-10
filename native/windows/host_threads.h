#pragma once
#include <memory>
#include <windows.h>

namespace keyrill::inspection {
enum class ThreadState {
  Live,
  Exited,
  Missing,
  OpenFailed,
  OwnerFailed,
  WrongOwner,
  WaitFailed,
  ProcessStale,
  ProcessFailed,
  WrongProcess
};
inline constexpr const char *thread_labels[] = {
    "Live",       "Exited",     "Missing",      "OpenFailed",    "OwnerFailed",
    "WrongOwner", "WaitFailed", "ProcessStale", "ProcessFailed", "WrongProcess"};
struct CloseHandleDeleter {
  void operator()(void *handle) const noexcept { CloseHandle(handle); }
};
struct ThreadCheck {
  ThreadState state = ThreadState::OpenFailed;
  DWORD tid = 0;
  DWORD owner = 0;
  DWORD error = 0;
  DWORD wait = WAIT_FAILED;
  DWORD process_wait = WAIT_FAILED;
  std::unique_ptr<void, CloseHandleDeleter> handle;
};
inline ThreadCheck CaptureThread(HANDLE process, DWORD pid, DWORD tid) noexcept {
  ThreadCheck result;
  result.tid = tid;
  auto process_live = [&] {
    const auto owner = GetProcessId(process);
    if (!owner) {
      result.error = GetLastError();
      result.state = ThreadState::ProcessFailed;
      return false;
    }
    if (owner != pid) {
      result.state = ThreadState::WrongProcess;
      return false;
    }
    result.process_wait = WaitForSingleObject(process, 0);
    if (result.process_wait == WAIT_TIMEOUT)
      return true;
    result.state = result.process_wait == WAIT_OBJECT_0 ? ThreadState::ProcessStale
                                                        : ThreadState::ProcessFailed;
    if (result.process_wait == WAIT_FAILED)
      result.error = GetLastError();
    return false;
  };
  if (!process_live())
    return result;
  result.handle.reset(OpenThread(THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, tid));
  if (!result.handle) {
    result.error = GetLastError();
    result.state =
        result.error == ERROR_INVALID_PARAMETER ? ThreadState::Missing : ThreadState::OpenFailed;
  } else {
    result.owner = GetProcessIdOfThread(result.handle.get());
    if (!result.owner) {
      result.error = GetLastError();
      result.state = ThreadState::OwnerFailed;
    } else if (result.owner != pid) {
      result.state = ThreadState::WrongOwner;
    } else {
      result.wait = WaitForSingleObject(result.handle.get(), 0);
      result.state = result.wait == WAIT_TIMEOUT    ? ThreadState::Live
                     : result.wait == WAIT_OBJECT_0 ? ThreadState::Exited
                                                    : ThreadState::WaitFailed;
      if (result.wait == WAIT_FAILED)
        result.error = GetLastError();
    }
  }
  process_live();
  return result;
}
} // namespace keyrill::inspection
