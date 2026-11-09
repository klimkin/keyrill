#pragma once
#include <array>
#include <cstdint>
#include <msctf.h>
#include <wrl/client.h>

namespace keyrill::host {
enum class Kind : uint32_t { Unknown, ProofEditor, PackagedNotepad };
struct Identity {
  DWORD process = 0;
  DWORD thread = 0;
  uint64_t started = 0;
  Kind kind = Kind::Unknown;
  std::array<uint32_t, 4> version{};
};
bool Capture(HANDLE process, DWORD pid, DWORD thread, Identity &output) noexcept;
enum class Reason : uint32_t {
  Unobserved,
  Unsupported,
  Stale,
  Native,
  Context,
  ReadOnly,
  Disabled,
  Selection,
  MissingScope,
  Scope,
  MissingAcp,
  ProofOnly,
  Request,
  Session,
  Absent,
  Secure,
  Exception
};
inline constexpr const char *reason_labels[] = {
    "Unobserved", "Unsupported", "Stale",        "Native", "Context",    "ReadOnly",
    "Disabled",   "Selection",   "MissingScope", "Scope",  "MissingAcp", "ProofOnly",
    "Request",    "Session",     "Absent",       "Secure", "Exception"};
enum class Focus : uint32_t { Unobserved, Exact, Different, Windowless };
enum class Scope : uint32_t { Unobserved, Missing, Default, Text, Excluded, Ambiguous };
struct Metadata {
  Reason reason = Reason::Unobserved;
  uint32_t result = 0;
  Focus focus = Focus::Unobserved;
  Scope scope = Scope::Unobserved;
  uint32_t request = 0;
  uint32_t session = 0;
  uint32_t entered = 0;
  uint32_t completed = 0;
  uint32_t selection = 0;
  uint32_t acp = 0;
};
struct Lifetime {
  IUnknown *owner;
  void *state;
  uint64_t generation;
  bool (*current)(void *, uint64_t) noexcept;
};
Metadata Observe(ITfContext *, ITfThreadMgr *, TfClientId, Lifetime);
} // namespace keyrill::host
