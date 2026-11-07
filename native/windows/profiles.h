#pragma once
#include "keyrill.h"
#include <msctf.h>
#include <windows.h>

namespace keyrill {
inline constexpr CLSID service_id = {
    0x1a746212, 0x84db, 0x4acf, {0x88, 0xb3, 0xca, 0xaa, 0x26, 0x35, 0x27, 0x74}};
struct Profile {
  GUID id;
  LANGID language;
  const wchar_t *layout;
  const wchar_t *description;
  uint32_t mode;
};
inline constexpr Profile profiles[] = {
    {{0x0d52a994, 0x9eb5, 0x44ab, {0xb1, 0x8f, 0x60, 0x3d, 0xc5, 0xf6, 0x96, 0x71}},
     0x0409,
     L"00000409",
     L"Keyrill proof EN",
     KR_EN},
    {{0x4c1e583c, 0xf8e5, 0x4b7d, {0xb6, 0x6c, 0xb7, 0xe7, 0x31, 0x9a, 0xed, 0x51}},
     0x0419,
     L"00000419",
     L"Keyrill proof RU",
     KR_RU}};
inline constexpr wchar_t fixture_class[] = L"KeyrillProofEditor";
bool ProofWindow(HWND window);
bool TranslateKey(WPARAM key, LPARAM flags, HKL layout, const BYTE (&state)[256], kr_event &event);
HKL ProfileLayout(const TF_INPUTPROCESSORPROFILE &profile, uint32_t mode, HKL substitute);
} // namespace keyrill
