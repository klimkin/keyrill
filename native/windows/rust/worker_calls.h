#pragma once
#include <cstdint>
#include <windows.h>
#include <winternl.h>

// Null native entries select the generated Windows bindings in the Rust worker.
struct KrWorkerNativeCalls {
  decltype(&NtCreateFile) open;
  decltype(&ReadFile) read;
  decltype(&GetOverlappedResult) completion;
  decltype(&CancelIoEx) cancel;
  decltype(&CreateEventW) event;
  decltype(&GetModuleHandleExW) pin;
  decltype(&TrySubmitThreadpoolCallback) submit;
  decltype(&CallbackMayRunLong) long_running;
  decltype(&GetVolumePathNamesForVolumeNameW) volume_paths;
};
struct KrWorkerHooks {
  void (*call)(void *, uint32_t, HANDLE, size_t);
  void *context;
  KrWorkerNativeCalls native;
  void(WINAPI *retain_context)(void *);
  void(WINAPI *release_context)(void *);
};
struct KrWorkerCall {
  uintptr_t value;
  uint32_t error;
  uint32_t exception;
};

extern "C" {
KrWorkerCall kr_worker_hook(void *, void *, uint32_t, HANDLE, size_t) noexcept;
KrWorkerCall kr_worker_lifetime(void *, void *) noexcept;
KrWorkerCall kr_worker_open(void *, PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
                            ULONG, ULONG, ULONG, ULONG) noexcept;
KrWorkerCall kr_worker_read(void *, HANDLE, void *, DWORD, OVERLAPPED *) noexcept;
KrWorkerCall kr_worker_completion(void *, HANDLE, OVERLAPPED *, DWORD *, BOOL) noexcept;
KrWorkerCall kr_worker_cancel(void *, HANDLE, OVERLAPPED *) noexcept;
KrWorkerCall kr_worker_event(void *) noexcept;
KrWorkerCall kr_worker_pin(void *, DWORD, LPCWSTR, HMODULE *) noexcept;
KrWorkerCall kr_worker_submit(void *, PTP_SIMPLE_CALLBACK, void *) noexcept;
KrWorkerCall kr_worker_long_running(void *, PTP_CALLBACK_INSTANCE) noexcept;
KrWorkerCall kr_worker_volume_paths(void *, LPCWSTR, LPWCH, DWORD, DWORD *) noexcept;
}
