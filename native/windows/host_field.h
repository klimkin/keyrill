#pragma once
#include "host_observation.h"
#include <array>

namespace keyrill::field {
enum class PublicCase { Empty, ProbeG, FixtureEnBoundary };
bool ParsePublicCase(const wchar_t *, PublicCase &) noexcept;
const char *CaseName(PublicCase) noexcept;
enum class ReadOnly { Unknown, Unavailable, Unsupported, Writable, ReadOnly, Mixed };
inline constexpr const char *readonly_labels[] = {"Unknown",  "Unavailable", "Unsupported",
                                                  "Writable", "ReadOnly",    "Mixed"};
enum class Refusal {
  None,
  Target,
  NativeFocus,
  NativeFacts,
  NativeExcluded,
  Stale,
  Provider,
  Binding,
  ControlType,
  Password,
  Disabled,
  ReadOnly,
  Mixed,
  Writability,
  TextUnavailable,
  TextMismatch,
  Selection
};
inline constexpr const char *refusal_labels[] = {
    "None",     "Target",      "NativeFocus",     "NativeFacts",  "NativeExcluded", "Stale",
    "Provider", "Binding",     "ControlType",     "Password",     "Disabled",       "ReadOnly",
    "Mixed",    "Writability", "TextUnavailable", "TextMismatch", "Selection"};
struct DiagnosticTarget {
  HANDLE process;
  host::Identity identity;
};
struct Window {
  HWND handle = nullptr;
  DWORD pid = 0;
  DWORD tid = 0;
  int control = 0;
  LONG_PTR style = 0;
  LONG_PTR exstyle = 0;
  bool enabled = false;
  bool complete = false;
  bool class_truncated = false;
  std::array<wchar_t, 128> class_name{};
  bool operator==(const Window &) const = default;
};
struct NativeFocus {
  DWORD pid = 0;
  DWORD foreground_tid = 0;
  uint64_t process_started = 0;
  uint64_t foreground_thread_started = 0;
  uint64_t focused_thread_started = 0;
  HWND foreground = nullptr;
  Window focused;
  Window root;
  std::array<Window, 8> parents{};
  size_t parent_count = 0;
  bool parents_truncated = false;
  bool operator==(const NativeFocus &) const = default;
};
struct Metadata {
  Refusal refusal = Refusal::Target;
  NativeFocus native;
  int provider_pid = 0;
  HWND provider_window = nullptr;
  int control_type = 0;
  bool exact_binding = false;
  bool nonpassword = false;
  bool enabled = false;
  ReadOnly value_readonly = ReadOnly::Unknown;
  ReadOnly text_readonly = ReadOnly::Unknown;
  bool text_pattern = false;
  bool operator==(const Metadata &) const = default;
};
struct CaseResult {
  Refusal refusal = Refusal::Target;
  bool matched = false;
};
Metadata Observe(const DiagnosticTarget &);
CaseResult CheckPublicCase(const DiagnosticTarget &, PublicCase);
} // namespace keyrill::field
