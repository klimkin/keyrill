#pragma once
#include "keyrill.h"
#include <variant>
#include <windows.h>

namespace keyrill {
struct ModifierOnly {};
using NativeKey = std::variant<ModifierOnly, kr_event>;
NativeKey TranslateKey(WPARAM key, LPARAM flags, HKL layout, const BYTE (&state)[256]);
} // namespace keyrill
