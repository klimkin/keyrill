#pragma once
#include "keyrill.h"
#include <array>
#include <msctf.h>
#include <variant>
#include <windows.h>

namespace keyrill {
enum class ManualIntent { Convert, Reverse };
struct AltPressed {};
struct BindingSpec {
  GUID id;
  TF_PRESERVEDKEY key;
  std::variant<ManualIntent, AltPressed> action;
  const wchar_t *description;
};
extern const std::array<BindingSpec, 3> bindings;
bool ExactCommandModifiers(const BYTE (&state)[256]) noexcept;
struct ModifierOnly {};
using NativeKey = std::variant<ModifierOnly, kr_event>;
NativeKey TranslateKey(WPARAM key, LPARAM flags, HKL layout, const BYTE (&state)[256]);
} // namespace keyrill
