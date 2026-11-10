#pragma once
#include "host_observation.h"
#include <optional>

namespace keyrill::native_field {
enum class Kind { Proof, Notepad };
struct Window {
  HWND handle = nullptr;
  DWORD process = 0;
  DWORD thread = 0;
  int control = 0;
  LONG_PTR style = 0;
  std::array<wchar_t, 64> class_name{};
  bool enabled = false;
  bool complete = false;
};
struct Facts {
  host::Identity host;
  DWORD process = 0;
  DWORD thread = 0;
  HWND view = nullptr;
  HWND focus = nullptr;
  HWND foreground = nullptr;
  HWND root = nullptr;
  Window field;
  Window parent;
  Window grandparent;
  Window proof_root;
  bool proof_process = false;
};
struct FieldIdentity {
  Kind kind;
  HWND field;
  HWND parent;
  HWND root;
  DWORD process;
  DWORD thread;
  DWORD root_thread;
  bool operator==(const FieldIdentity &) const = default;
};
bool KnownNotepad(const host::Identity &) noexcept;
Facts Capture(HWND, const host::Identity &) noexcept;
std::optional<FieldIdentity> Match(const Facts &) noexcept;
class Source {
public:
  virtual ~Source() = default;
  virtual Facts Capture(HWND, const host::Identity &) const noexcept = 0;
};
} // namespace keyrill::native_field
