#include "keys.h"
#include "profiles.h"
#include <algorithm>

namespace keyrill {
const std::array<BindingSpec, 3> bindings{{
    {{0xd7abce42, 0xc8a9, 0x4a43, {0x99, 0x0a, 0x4a, 0x8d, 0x6c, 0x01, 0x1e, 0xbb}},
     {'K', TF_MOD_LCONTROL | TF_MOD_LALT},
     ManualIntent::Convert,
     L"Keyrill manual conversion"},
    {{0x16531b70, 0x516e, 0x4dfb, {0x9f, 0x18, 0xcb, 0x76, 0x0d, 0xe5, 0x90, 0xb4}},
     {'Z', TF_MOD_LCONTROL | TF_MOD_LALT},
     ManualIntent::Reverse,
     L"Keyrill reversal"},
    {{0x997f7561, 0x053d, 0x475a, {0xab, 0xb9, 0xb1, 0x1f, 0xcf, 0x16, 0xbb, 0x48}},
     {VK_MENU, TF_MOD_IGNORE_ALL_MODIFIER},
     AltPressed{},
     L"Keyrill Alt press observer"},
}};
static bool Modifier(WPARAM key) {
  switch (key) {
  case VK_SHIFT:
  case VK_LSHIFT:
  case VK_RSHIFT:
  case VK_CONTROL:
  case VK_LCONTROL:
  case VK_RCONTROL:
  case VK_MENU:
  case VK_LMENU:
  case VK_RMENU:
  case VK_LWIN:
  case VK_RWIN:
    return true;
  default:
    return false;
  }
}
static bool Printable(const wchar_t *text, int count) {
  for (int i = 0; i < count; ++i) {
    uint32_t scalar = text[i];
    if (scalar >= 0xd800 && scalar <= 0xdbff) {
      if (++i == count || text[i] < 0xdc00 || text[i] > 0xdfff)
        return false;
      scalar = 0x10000 + ((scalar - 0xd800) << 10) + (text[i] - 0xdc00);
    } else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
      return false;
    }
    if (scalar < 0x20 || (scalar >= 0x7f && scalar <= 0x9f) ||
        (scalar >= 0xe000 && scalar <= 0xf8ff) || (scalar >= 0xf0000 && scalar <= 0xffffd) ||
        (scalar >= 0x100000 && scalar <= 0x10fffd))
      return false;
  }
  return true;
}
bool ExactCommandModifiers(const BYTE (&state)[256]) noexcept {
  const auto down = [&](int code) { return (state[code] & 0x80) != 0; };
  return down(VK_LCONTROL) && down(VK_LMENU) && !down(VK_RCONTROL) && !down(VK_RMENU) &&
         !down(VK_SHIFT) && !down(VK_LSHIFT) && !down(VK_RSHIFT) && !down(VK_LWIN) &&
         !down(VK_RWIN);
}
NativeKey TranslateKey(WPARAM key, LPARAM flags, HKL layout, const BYTE (&state)[256]) {
  if (Modifier(key))
    return ModifierOnly{};
  kr_event event{KR_FINISH_REQUEST, 0, KR_CTX_INELIGIBLE, 0, {}};
  for (const auto &profile : profiles)
    if (LOWORD(reinterpret_cast<ULONG_PTR>(layout)) == profile.language &&
        HIWORD(reinterpret_cast<ULONG_PTR>(layout)) == profile.language)
      event.mode = profile.mode;
  if (!event.mode || LOWORD(flags) != 1)
    return event;
  const auto down = [&](int code) { return (state[code] & 0x80) != 0; };
  bool shift = down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT);
  bool windows = down(VK_LWIN) || down(VK_RWIN);
  bool command = down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL) || down(VK_MENU) ||
                 down(VK_LMENU) || down(VK_RMENU) || windows;
  if (ExactCommandModifiers(state))
    for (const auto &entry : bindings)
      if (const auto *intent = std::get_if<ManualIntent>(&entry.action);
          intent && key == entry.key.uVKey) {
        event.role = *intent == ManualIntent::Convert ? KR_MANUAL : KR_REVERSE;
        return event;
      }
  if (command)
    return event;
  if (!shift && (key == VK_BACK || key == VK_ESCAPE)) {
    event.role = key == VK_BACK ? KR_BACKSPACE : KR_ESCAPE;
    return event;
  }
  wchar_t text[KR_EVENT_CAPACITY]{};
  int count = ToUnicodeEx(static_cast<UINT>(key), (static_cast<UINT>(flags) >> 16) & 0xff, state,
                          text, KR_EVENT_CAPACITY, 4, layout);
  if (count <= 0 || count > KR_EVENT_CAPACITY || !Printable(text, count))
    return event;
  event.role = KR_TEXT;
  event.len = static_cast<uint32_t>(count);
  std::copy(text, text + count, event.units);
  return event;
}
} // namespace keyrill
