#include "profiles.h"
#include <cstdio>
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
namespace {
const char *Label(const keyrill::Profile &profile) { return profile.mode == KR_EN ? "EN" : "RU"; }
HRESULT Report(const char *stage, HRESULT result, const char *profile = "service") {
  printf("%s [%s]: 0x%08lx.\n", stage, profile, static_cast<unsigned long>(result));
  fflush(stdout);
  return result;
}
template <typename Manager> HRESULT Presence(Manager *manager, const keyrill::Profile &profile) {
  ComPtr<IEnumTfLanguageProfiles> entries;
  HRESULT result =
      Report("EnumLanguageProfiles", manager->EnumLanguageProfiles(profile.language, &entries),
             Label(profile));
  if (result != S_OK)
    return FAILED(result)
               ? result
               : Report("EnumLanguageProfiles unexpected result", E_UNEXPECTED, Label(profile));
  if (!entries)
    return Report("EnumLanguageProfiles null enumerator", E_UNEXPECTED, Label(profile));
  for (;;) {
    TF_LANGUAGEPROFILE entry{};
    ULONG fetched = 0;
    result = entries->Next(1, &entry, &fetched);
    if (result == S_FALSE && fetched == 0)
      return Report("Profile presence", S_FALSE, Label(profile));
    if (result != S_OK || fetched != 1)
      return Report("IEnumTfLanguageProfiles::Next", FAILED(result) ? result : E_UNEXPECTED,
                    Label(profile));
    if (entry.clsid == keyrill::service_id && entry.langid == profile.language &&
        entry.guidProfile == profile.id) {
      BSTR description = nullptr;
      result = Report("GetLanguageProfileDescription",
                      manager->GetLanguageProfileDescription(keyrill::service_id, profile.language,
                                                             profile.id, &description),
                      Label(profile));
      bool matches = result == S_OK && description &&
                     SysStringLen(description) == wcslen(profile.description) &&
                     wmemcmp(description, profile.description, SysStringLen(description)) == 0;
      SysFreeString(description);
      if (FAILED(result))
        return result;
      if (!matches)
        return Report("Profile description mismatch or unavailable", E_UNEXPECTED, Label(profile));
      return Report("Profile presence", S_OK, Label(profile));
    }
  }
}
template <typename Manager> HRESULT Preflight(Manager *manager) {
  HRESULT result = S_OK;
  for (const auto &profile : keyrill::profiles) {
    HRESULT present = Presence(manager, profile);
    if (present != S_OK && SUCCEEDED(result))
      result = present == S_FALSE ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND) : present;
  }
  return Report("Both-profile presence preflight", result);
}
template <typename Manager> HRESULT EnableProfiles(Manager *manager, bool enabled) {
  HRESULT result = Preflight(manager);
  if (FAILED(result))
    return result;
  for (const auto &profile : keyrill::profiles) {
    result = Report(
        "EnableLanguageProfile",
        manager->EnableLanguageProfile(keyrill::service_id, profile.language, profile.id, enabled),
        Label(profile));
    if (result != S_OK)
      return FAILED(result)
                 ? result
                 : Report("EnableLanguageProfile unexpected result", E_UNEXPECTED, Label(profile));
  }
  result = Preflight(manager);
  if (FAILED(result))
    return result;
  for (const auto &profile : keyrill::profiles) {
    BOOL actual = FALSE;
    result = Report("IsEnabledLanguageProfile readback",
                    manager->IsEnabledLanguageProfile(keyrill::service_id, profile.language,
                                                      profile.id, &actual),
                    Label(profile));
    if (result != S_OK)
      return FAILED(result)
                 ? result
                 : Report("Enablement readback unexpected result", E_UNEXPECTED, Label(profile));
    if ((actual != FALSE) != enabled)
      return Report("Enablement readback mismatch", E_FAIL, Label(profile));
  }
  return S_OK;
}
HRESULT Registry(const wchar_t *dll, bool install) {
  wchar_t id[40]{};
  StringFromGUID2(keyrill::service_id, id, 40);
  std::wstring path = std::wstring(L"Software\\Classes\\CLSID\\") + id;
  if (!install) {
    LSTATUS result = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    return Report("RegDeleteTreeW HKCU COM",
                  result == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(result));
  }
  path += L"\\InprocServer32";
  HKEY key = nullptr;
  LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE,
                                   nullptr, &key, nullptr);
  if (FAILED(Report("RegCreateKeyExW HKCU COM", HRESULT_FROM_WIN32(result))))
    return HRESULT_FROM_WIN32(result);
  result = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(dll),
                          static_cast<DWORD>((wcslen(dll) + 1) * sizeof(wchar_t)));
  Report("RegSetValueExW HKCU COM server", HRESULT_FROM_WIN32(result));
  if (result == ERROR_SUCCESS) {
    constexpr wchar_t apartment[] = L"Apartment";
    result = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                            reinterpret_cast<const BYTE *>(apartment), sizeof(apartment));
    Report("RegSetValueExW HKCU COM ThreadingModel", HRESULT_FROM_WIN32(result));
  }
  LSTATUS closed = RegCloseKey(key);
  Report("RegCloseKey HKCU COM", HRESULT_FROM_WIN32(closed));
  if (result == ERROR_SUCCESS)
    result = closed;
  return HRESULT_FROM_WIN32(result);
}
HRESULT Register(const wchar_t *dll, bool install) {
  ComPtr<ITfInputProcessorProfiles> manager;
  ComPtr<ITfCategoryMgr> categories;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&manager));
  Report("CoCreateInstance InputProcessorProfiles", result);
  if (FAILED(result))
    return result;
  result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&categories));
  Report("CoCreateInstance CategoryMgr", result);
  if (FAILED(result))
    return result;
  if (!install) {
    HRESULT profiles_result = Report("Unregister", manager->Unregister(keyrill::service_id));
    HRESULT category_result =
        Report("UnregisterCategory",
               categories->UnregisterCategory(keyrill::service_id, GUID_TFCAT_TIP_KEYBOARD,
                                              keyrill::service_id));
    HRESULT registry_result = Registry(nullptr, false);
    if (FAILED(profiles_result))
      return profiles_result;
    return FAILED(category_result) ? category_result : registry_result;
  }
  result = Registry(dll, true);
  if (FAILED(result))
    return result;
  result = Report("Register", manager->Register(keyrill::service_id));
  if (FAILED(result))
    return result;
  for (const auto &profile : keyrill::profiles) {
    result = manager->AddLanguageProfile(
        keyrill::service_id, profile.language, profile.id, profile.description,
        static_cast<ULONG>(wcslen(profile.description)), dll, static_cast<ULONG>(wcslen(dll)), 0);
    Report("AddLanguageProfile", result, Label(profile));
    if (FAILED(result))
      return result;
    HKL layout = LoadKeyboardLayoutW(profile.layout, KLF_NOTELLSHELL);
    DWORD layout_error = layout ? ERROR_SUCCESS : GetLastError();
    result = Report("LoadKeyboardLayoutW",
                    layout         ? S_OK
                    : layout_error ? HRESULT_FROM_WIN32(layout_error)
                                   : E_FAIL,
                    Label(profile));
    if (FAILED(result))
      return result;
    result = manager->SubstituteKeyboardLayout(keyrill::service_id, profile.language, profile.id,
                                               layout);
    Report("SubstituteKeyboardLayout", result, Label(profile));
    if (FAILED(result))
      return result;
  }
  result = Report("RegisterCategory",
                  categories->RegisterCategory(keyrill::service_id, GUID_TFCAT_TIP_KEYBOARD,
                                               keyrill::service_id));
  return result == S_OK   ? Preflight(manager.Get())
         : FAILED(result) ? result
                          : Report("RegisterCategory unexpected result", E_UNEXPECTED);
}
HRESULT Status() {
  ComPtr<ITfInputProcessorProfiles> manager;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&manager));
  Report("CoCreateInstance InputProcessorProfiles status", result);
  if (FAILED(result))
    return result;
  wchar_t id[40]{};
  StringFromGUID2(keyrill::service_id, id, 40);
  std::wstring path = std::wstring(L"Software\\Classes\\CLSID\\") + id + L"\\InprocServer32";
  HKEY key = nullptr;
  LSTATUS registry = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key);
  if (key)
    RegCloseKey(key);
  bool absent = registry == ERROR_FILE_NOT_FOUND || registry == ERROR_PATH_NOT_FOUND;
  printf("COM HKCU registration: %s.\n", registry == ERROR_SUCCESS ? "present"
                                         : absent                  ? "absent"
                                                                   : "unavailable");
  Report("RegOpenKeyExW HKCU COM status", HRESULT_FROM_WIN32(registry));
  result = registry == ERROR_SUCCESS || absent ? S_OK : HRESULT_FROM_WIN32(registry);
  for (const auto &profile : keyrill::profiles) {
    HRESULT registered = Presence(manager.Get(), profile);
    if (FAILED(registered))
      result = registered;
    BOOL enabled = FALSE;
    HRESULT available =
        registered == S_OK
            ? Report("IsEnabledLanguageProfile status",
                     manager->IsEnabledLanguageProfile(keyrill::service_id, profile.language,
                                                       profile.id, &enabled),
                     Label(profile))
            : E_FAIL;
    if (registered == S_OK && available != S_OK)
      result = FAILED(available)
                   ? available
                   : Report("Enablement status unexpected result", E_UNEXPECTED, Label(profile));
    printf("TSF %s profile: %s; enabled: %s.\n", Label(profile),
           registered == S_OK      ? "present"
           : registered == S_FALSE ? "absent"
                                   : "unavailable",
           registered != S_OK  ? "not applicable"
           : available == S_OK ? enabled ? "yes" : "no"
                               : "unavailable");
  }
  puts("TSF profile registration scope has not been established. No elevation fallback or source "
       "selection.");
  return result;
}
HRESULT Enable(bool enabled, bool check) {
  ComPtr<ITfInputProcessorProfiles> manager;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&manager));
  Report("CoCreateInstance InputProcessorProfiles preflight", result);
  if (FAILED(result))
    return result;
  return check ? Preflight(manager.Get()) : EnableProfiles(manager.Get(), enabled);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  bool install = argc == 3 && wcscmp(argv[1], L"register") == 0;
  bool remove = argc == 2 && wcscmp(argv[1], L"unregister") == 0;
  bool status = argc == 2 && wcscmp(argv[1], L"status") == 0;
  bool enable = argc == 2 && wcscmp(argv[1], L"enable") == 0;
  bool disable = argc == 2 && wcscmp(argv[1], L"disable") == 0;
  bool check = argc == 2 && wcscmp(argv[1], L"check") == 0;
  if (!install && !remove && !status && !enable && !disable && !check) {
    fwprintf(stderr,
             L"Maintainer only. Usage: KeyrillRegister status | check | register ABSOLUTE_DLL_PATH "
             L"| enable | disable | unregister\n");
    return 2;
  }
  wchar_t path[32768]{};
  if (install) {
    DWORD count = GetFullPathNameW(argv[2], 32768, path, nullptr);
    HRESULT resolved = !count           ? HRESULT_FROM_WIN32(GetLastError())
                       : count >= 32768 ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)
                                        : S_OK;
    if (FAILED(Report("GetFullPathNameW DLL", resolved)))
      return 2;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
      Report("GetFileAttributesW DLL", HRESULT_FROM_WIN32(GetLastError()));
      return 2;
    }
    Report("GetFileAttributesW DLL", S_OK);
  }
  HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  Report("CoInitializeEx", result);
  if (FAILED(result))
    return 1;
  if (status)
    result = Status();
  else if (enable || disable || check)
    result = Enable(enable, check);
  else {
    Status();
    result = Register(path, install);
  }
  CoUninitialize();
  if (FAILED(result)) {
    fwprintf(stderr,
             L"Operation failed (0x%08lx). Inspect the stage above and run status before any "
             L"further change.\n",
             static_cast<unsigned long>(result));
    return 1;
  }
  if (check)
    puts("Both proof profiles are present. Read-only check; no enablement or source selection.");
  else if (!status)
    puts(install  ? "Registered proof profiles through TSF and COM through HKCU. Enablement and "
                    "source selection are separate."
         : remove ? "Removed proof profiles and HKCU COM registration."
         : enable ? "Both proof profiles are present and enablement readback matches enabled. "
                    "Source selection is manual."
                  : "Both proof profiles are present and enablement readback matches disabled.");
  return 0;
}
