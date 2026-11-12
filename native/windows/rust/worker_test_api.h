#pragma once
#include "worker_calls.h"

struct KrWorkerSource {
  uint32_t default_source;
  const uint16_t *path;
  uint32_t length;
};
struct KrWorkerSourceReady {
  uint32_t status;
  uint32_t error;
  const void *dictionary;
  const void *owner;
};
struct KrWorkerReady {
  uint32_t activation;
  uint32_t model;
  uint64_t revision;
  uint32_t error;
  uint32_t reserved;
  KrWorkerSourceReady sources[2];
};

// The creator owns Worker until Free. Each returned owner transfers one Arc token,
// released exactly once with DictionaryRelease after every dictionary borrow drains.
extern "C" {
// A successful setter retains paired callback storage until replacement or null-table clear.
// Clear before releasing the caller's DLL reference. Started workers hold independent leases.
BOOL WINAPI KeyrillTestWorkerHooks(const KrWorkerHooks *) noexcept;
void *WINAPI KeyrillTestWorkerCreate() noexcept;
BOOL WINAPI KeyrillTestWorkerStart(void *, const KrWorkerSource *, uint32_t activation,
                                   uint32_t dll_owner, const KrWorkerHooks *) noexcept;
void WINAPI KeyrillTestWorkerStop(void *) noexcept;
BOOL WINAPI KeyrillTestWorkerPoll(void *, KrWorkerReady *) noexcept;
void WINAPI KeyrillTestWorkerFree(void *) noexcept;
void WINAPI KeyrillTestWorkerDictionaryRelease(const void *) noexcept;
BOOL WINAPI KeyrillTestWorkerParsePath(const uint16_t *, uint32_t) noexcept;
}
