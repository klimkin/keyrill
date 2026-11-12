#include "worker_calls.h"

extern "C" KrWorkerCall kr_worker_hook(void *target, void *context, uint32_t boundary,
                                       HANDLE handle, size_t index) noexcept {
  try {
    reinterpret_cast<decltype(KrWorkerHooks::call)>(target)(context, boundary, handle, index);
    return {1, 0, 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_lifetime(void *target, void *context) noexcept {
  try {
    reinterpret_cast<decltype(KrWorkerHooks::retain_context)>(target)(context);
    return {1, 0, 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_open(void *target, PHANDLE output, ACCESS_MASK access,
                                       POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK io,
                                       ULONG file_attributes, ULONG sharing, ULONG disposition,
                                       ULONG options) noexcept {
  try {
    auto status = reinterpret_cast<decltype(&NtCreateFile)>(target)(
        output, access, attributes, io, nullptr, file_attributes, sharing, disposition, options,
        nullptr, 0);
    return {static_cast<uintptr_t>(status), 0, 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_read(void *target, HANDLE file, void *buffer, DWORD count,
                                       OVERLAPPED *operation) noexcept {
  try {
    BOOL result =
        reinterpret_cast<decltype(&ReadFile)>(target)(file, buffer, count, nullptr, operation);
    return {static_cast<uintptr_t>(result), result ? 0 : GetLastError(), 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_completion(void *target, HANDLE file, OVERLAPPED *operation,
                                             DWORD *count, BOOL wait) noexcept {
  try {
    BOOL result =
        reinterpret_cast<decltype(&GetOverlappedResult)>(target)(file, operation, count, wait);
    return {static_cast<uintptr_t>(result), result ? 0 : GetLastError(), 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_cancel(void *target, HANDLE file,
                                         OVERLAPPED *operation) noexcept {
  try {
    BOOL result = reinterpret_cast<decltype(&CancelIoEx)>(target)(file, operation);
    return {static_cast<uintptr_t>(result), result ? 0 : GetLastError(), 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_event(void *target) noexcept {
  try {
    HANDLE result =
        reinterpret_cast<decltype(&CreateEventW)>(target)(nullptr, TRUE, FALSE, nullptr);
    return {reinterpret_cast<uintptr_t>(result), result ? 0 : GetLastError(), 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_pin(void *target, DWORD flags, LPCWSTR address,
                                      HMODULE *module) noexcept {
  try {
    BOOL result = reinterpret_cast<decltype(&GetModuleHandleExW)>(target)(flags, address, module);
    return {static_cast<uintptr_t>(result), result ? 0 : GetLastError(), 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_submit(void *target, PTP_SIMPLE_CALLBACK callback,
                                         void *context) noexcept {
  try {
    BOOL result = reinterpret_cast<decltype(&TrySubmitThreadpoolCallback)>(target)(
        callback, context, nullptr);
    return {static_cast<uintptr_t>(result), result ? 0 : GetLastError(), 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_long_running(void *target,
                                               PTP_CALLBACK_INSTANCE instance) noexcept {
  try {
    BOOL result = reinterpret_cast<decltype(&CallbackMayRunLong)>(target)(instance);
    return {static_cast<uintptr_t>(result), result ? 0 : GetLastError(), 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
extern "C" KrWorkerCall kr_worker_volume_paths(void *target, LPCWSTR guid, LPWCH paths,
                                               DWORD capacity, DWORD *length) noexcept {
  try {
    BOOL result = reinterpret_cast<decltype(&GetVolumePathNamesForVolumeNameW)>(target)(
        guid, paths, capacity, length);
    return {static_cast<uintptr_t>(result), result ? 0 : GetLastError(), 0};
  } catch (...) {
    return {0, 0, 1};
  }
}
