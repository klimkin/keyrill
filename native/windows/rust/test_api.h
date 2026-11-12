#pragma once
#include "../../../include/keyrill.h"
#include <cstdint>
#include <msctf.h>
#include <windows.h>

// A test handle and its COM object share the production Service. Release both after callbacks
// drain.
inline constexpr uint32_t KR_WINDOWS_TEST_VERSION = 2;
struct KrWindowsHostIdentity {
  uint32_t process;
  uint32_t thread;
  uint64_t started;
  uint32_t kind;
  uint32_t version[4];
};
struct KrWindowsFieldWindow {
  HWND handle;
  uint32_t process;
  uint32_t thread;
  int32_t control;
  intptr_t style;
  uint16_t class_name[64];
  uint32_t enabled;
  uint32_t complete;
};
struct KrWindowsFieldFacts {
  KrWindowsHostIdentity host;
  uint32_t process;
  uint32_t thread;
  HWND view;
  HWND focus;
  HWND foreground;
  HWND root;
  KrWindowsFieldWindow field;
  KrWindowsFieldWindow parent;
  KrWindowsFieldWindow grandparent;
  KrWindowsFieldWindow proof_root;
  uint32_t proof_process;
};
struct KrWindowsDependencies {
  uint32_t size;
  uint32_t version;
  void *context;
  void(WINAPI *retain)(void *);
  void(WINAPI *release)(void *);
  IUnknown *profiles;
  int(WINAPI *layouts)(int, HKL *);
  HRESULT(WINAPI *capture)(void *, HWND, const KrWindowsHostIdentity *, KrWindowsFieldFacts *);
};
// The table is copied. Paired retain/release callbacks own context and callback targets through
// every service and retained frame. Without these callbacks the caller supplies that lifetime.
// profiles, layouts and capture are optional. Null selects the production platform dependency.
static_assert(sizeof(KrWindowsHostIdentity) == 40);
static_assert(sizeof(KrWindowsFieldWindow) == 168);
static_assert(sizeof(KrWindowsFieldFacts) == 760);
static_assert(sizeof(KrWindowsDependencies) == 56);
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

using KrTestCreate = HRESULT(WINAPI *)(uint32_t, const KrWindowsDependencies *, void **, void **);
using KrTestRelease = HRESULT(WINAPI *)(void *);
using KrTestPanic = HRESULT(WINAPI *)(void *, uint32_t);
using KrTestStopped = HRESULT(WINAPI *)(void *, BOOL *);
using KrTestProbe = HRESULT(WINAPI *)(void *, IUnknown *, uint32_t, uint32_t, KrWindowsProbe *);
using KrTestEvent = HRESULT(WINAPI *)(void *, ITfContext *, const kr_event *, BOOL *);
using KrTestMaintain = HRESULT(WINAPI *)(void *);

// Probe 0 performs fenced canonical QI. Probe 1 requests the generated one-use edit-session shell.
// Panic sites are 1 before effect recording, 2 after recording, and 3 in final Service destruction.
