#pragma once
#include "../../../include/keyrill.h"
#include <cstdint>
#include <windows.h>

// A test handle and its COM object share the production Service. Release both after callbacks
// drain.
inline constexpr uint32_t KR_WINDOWS_TEST_VERSION = 1;
struct KrWindowsProbe {
  uint32_t size;
  uint32_t version;
  HRESULT outer;
  HRESULT callback;
  uint32_t foreign_exception;
  BOOL entered;
  BOOL completed;
  uint32_t effect;
  uintptr_t identity;
};

using KrTestCreate = HRESULT(WINAPI *)(uint32_t, void **, void **);
using KrTestRelease = HRESULT(WINAPI *)(void *);
using KrTestPanic = HRESULT(WINAPI *)(void *, uint32_t);
using KrTestStopped = HRESULT(WINAPI *)(void *, BOOL *);
using KrTestProbe = HRESULT(WINAPI *)(void *, IUnknown *, uint32_t, uint32_t, KrWindowsProbe *);

// Probe 0 performs fenced canonical QI. Probe 1 requests the generated one-use edit-session shell.
// Panic sites are 1 before effect recording, 2 after recording, and 3 in final Service destruction.
