#include "keys.h"
#include "profiles.h"
#include "test_service.h"
#include <cstdlib>

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

bool ExactCommandModifiers(const BYTE (&state)[256]) noexcept {
  auto exact = reinterpret_cast<HRESULT(WINAPI *)(const BYTE *, BOOL *)>(
      TestExport("KeyrillTestExactCommandModifiers"));
  BOOL result = FALSE;
  if (exact(state, &result) != S_OK)
    std::abort();
  return result != FALSE;
}

NativeKey TranslateKey(WPARAM key, LPARAM flags, HKL layout, const BYTE (&state)[256]) {
  auto translate =
      reinterpret_cast<HRESULT(WINAPI *)(WPARAM, LPARAM, HKL, const BYTE *, kr_event *, BOOL *)>(
          TestExport("KeyrillTestTranslate"));
  kr_event event{};
  BOOL modifier = FALSE;
  if (translate(key, flags, layout, state, &event, &modifier) != S_OK)
    std::abort();
  if (modifier)
    return ModifierOnly{};
  return event;
}

HKL ProfileLayout(const TF_INPUTPROCESSORPROFILE &profile, uint32_t mode, HKL substitute) {
  auto resolve =
      reinterpret_cast<HRESULT(WINAPI *)(const TF_INPUTPROCESSORPROFILE *, uint32_t, HKL, HKL *)>(
          TestExport("KeyrillTestProfileLayout"));
  HKL result = nullptr;
  if (resolve(&profile, mode, substitute, &result) != S_OK)
    std::abort();
  return result;
}
} // namespace keyrill
