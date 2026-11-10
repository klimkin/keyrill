#pragma once
#include "keyrill.h"
#include <array>
#include <msctf.h>
#include <variant>
#include <windows.h>

namespace keyrill {
enum class ManualIntent { Convert, Reverse };
struct CommandSpec {
  GUID id;
  TF_PRESERVEDKEY key;
  ManualIntent intent;
  const wchar_t *description;
};
extern const std::array<CommandSpec, 2> commands;
bool ExactCommandModifiers(const BYTE (&state)[256]) noexcept;
struct ModifierOnly {};
using NativeKey = std::variant<ModifierOnly, kr_event>;
NativeKey TranslateKey(WPARAM key, LPARAM flags, HKL layout, const BYTE (&state)[256]);
} // namespace keyrill
