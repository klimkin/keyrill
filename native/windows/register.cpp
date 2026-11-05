#include "profiles.h"
#include <cstdio>
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
namespace {
HRESULT Registry(const wchar_t *dll, bool install) {
  wchar_t id[40]{};
  StringFromGUID2(keyrill::service_id, id, 40);
  std::wstring path = std::wstring(L"Software\\Classes\\CLSID\\") + id;
  if (!install) {
    LSTATUS result = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    return result == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(result);
  }
  path += L"\\InprocServer32";
  HKEY key = nullptr;
  LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE,
                                   nullptr, &key, nullptr);
  if (result != ERROR_SUCCESS)
    return HRESULT_FROM_WIN32(result);
  result = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(dll),
                          static_cast<DWORD>((wcslen(dll) + 1) * sizeof(wchar_t)));
  if (result == ERROR_SUCCESS) {
    constexpr wchar_t apartment[] = L"Apartment";
    result = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                            reinterpret_cast<const BYTE *>(apartment), sizeof(apartment));
  }
  RegCloseKey(key);
  return HRESULT_FROM_WIN32(result);
}
HRESULT Register(const wchar_t *dll, bool install) {
  ComPtr<ITfInputProcessorProfiles> manager;
  ComPtr<ITfCategoryMgr> categories;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&manager));
  if (FAILED(result))
    return result;
  result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&categories));
  if (FAILED(result))
    return result;
  if (!install) {
    HRESULT profiles_result = manager->Unregister(keyrill::service_id);
    HRESULT category_result = categories->UnregisterCategory(
        keyrill::service_id, GUID_TFCAT_TIP_KEYBOARD, keyrill::service_id);
    HRESULT registry_result = Registry(nullptr, false);
    if (FAILED(profiles_result))
      return profiles_result;
    return FAILED(category_result) ? category_result : registry_result;
  }
  result = Registry(dll, true);
  if (FAILED(result))
    return result;
  result = manager->Register(keyrill::service_id);
  if (FAILED(result))
    return result;
  for (const auto &profile : keyrill::profiles) {
    result = manager->AddLanguageProfile(
        keyrill::service_id, profile.language, profile.id, profile.description,
        static_cast<ULONG>(wcslen(profile.description)), dll, static_cast<ULONG>(wcslen(dll)), 0);
    if (FAILED(result))
      return result;
    HKL layout = LoadKeyboardLayoutW(profile.layout, KLF_NOTELLSHELL);
    if (!layout)
      return HRESULT_FROM_WIN32(GetLastError());
    result = manager->SubstituteKeyboardLayout(keyrill::service_id, profile.language, profile.id,
                                               layout);
    if (FAILED(result))
      return result;
  }
  return categories->RegisterCategory(keyrill::service_id, GUID_TFCAT_TIP_KEYBOARD,
                                      keyrill::service_id);
}
HRESULT Status() {
  ComPtr<ITfInputProcessorProfiles> manager;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&manager));
  if (FAILED(result))
    return result;
  wchar_t id[40]{};
  StringFromGUID2(keyrill::service_id, id, 40);
  std::wstring path = std::wstring(L"Software\\Classes\\CLSID\\") + id + L"\\InprocServer32";
  HKEY key = nullptr;
  LSTATUS registry = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key);
  if (key)
    RegCloseKey(key);
  printf("COM HKCU registration: %s.\n", registry == ERROR_SUCCESS ? "present" : "absent");
  for (const auto &profile : keyrill::profiles) {
    BSTR description = nullptr;
    HRESULT registered = manager->GetLanguageProfileDescription(
        keyrill::service_id, profile.language, profile.id, &description);
    bool present = SUCCEEDED(registered) && description != nullptr;
    SysFreeString(description);
    BOOL enabled = FALSE;
    HRESULT available = manager->IsEnabledLanguageProfile(keyrill::service_id, profile.language,
                                                          profile.id, &enabled);
    printf("TSF %s profile: %s; enabled: %s.\n", profile.mode == KR_EN ? "EN" : "RU",
           present ? "present" : "absent",
           !present               ? "not applicable"
           : SUCCEEDED(available) ? enabled ? "yes" : "no"
                                  : "unavailable");
  }
  puts("TSF profile registration scope has not been established. No elevation fallback or source "
       "selection.");
  return S_OK;
}
HRESULT Enable(bool enabled) {
  ComPtr<ITfInputProcessorProfiles> manager;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&manager));
  if (FAILED(result))
    return result;
  for (const auto &profile : keyrill::profiles) {
    result =
        manager->EnableLanguageProfile(keyrill::service_id, profile.language, profile.id, enabled);
    if (FAILED(result))
      return result;
  }
  return S_OK;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  bool install = argc == 3 && wcscmp(argv[1], L"register") == 0;
  bool remove = argc == 2 && wcscmp(argv[1], L"unregister") == 0;
  bool status = argc == 2 && wcscmp(argv[1], L"status") == 0;
  bool enable = argc == 2 && wcscmp(argv[1], L"enable") == 0;
  bool disable = argc == 2 && wcscmp(argv[1], L"disable") == 0;
  if (!install && !remove && !status && !enable && !disable) {
    fwprintf(stderr, L"Maintainer only. Usage: KeyrillRegister status | register ABSOLUTE_DLL_PATH "
                     L"| enable | disable | unregister\n");
    return 2;
  }
  wchar_t path[32768]{};
  if (install) {
    DWORD count = GetFullPathNameW(argv[2], 32768, path, nullptr);
    if (!count || count >= 32768 || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
      return 2;
  }
  HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(result))
    return 1;
  result = Status();
  if (SUCCEEDED(result) && !status)
    result = enable || disable ? Enable(enable) : Register(path, install);
  CoUninitialize();
  if (FAILED(result)) {
    fwprintf(stderr,
             L"Operation failed (0x%08lx). Partial registration may remain; run status before "
             L"cleanup.\n",
             static_cast<unsigned long>(result));
    return 1;
  }
  if (!status)
    puts(install  ? "Registered proof profiles through TSF and COM through HKCU. Enablement and "
                    "source selection are separate."
         : remove ? "Removed proof profiles and HKCU COM registration."
         : enable ? "Enabled proof profiles. Source selection is manual."
                  : "Disabled proof profiles.");
  return 0;
}
