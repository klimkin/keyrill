#include "registrar_calls.h"

extern "C" KrCall kr_registrar_create(BOOL category, void **output) noexcept {
  try {
    return {CoCreateInstance(
                category ? CLSID_TF_CategoryMgr : CLSID_TF_InputProcessorProfiles, nullptr,
                CLSCTX_INPROC_SERVER,
                category ? __uuidof(ITfCategoryMgr) : __uuidof(ITfInputProcessorProfiles), output),
            0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_entries(ITfInputProcessorProfiles *manager, LANGID language,
                                       IEnumTfLanguageProfiles **output) noexcept {
  try {
    return {manager->EnumLanguageProfiles(language, output), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_next(IEnumTfLanguageProfiles *entries, TF_LANGUAGEPROFILE *entry,
                                    ULONG *fetched) noexcept {
  try {
    return {entries->Next(1, entry, fetched), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_description(ITfInputProcessorProfiles *manager, const GUID *service,
                                           LANGID language, const GUID *profile,
                                           BSTR *output) noexcept {
  try {
    return {manager->GetLanguageProfileDescription(*service, language, *profile, output), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_enable(ITfInputProcessorProfiles *manager, const GUID *service,
                                      LANGID language, const GUID *profile, BOOL enabled) noexcept {
  try {
    return {manager->EnableLanguageProfile(*service, language, *profile, enabled), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_enabled(ITfInputProcessorProfiles *manager, const GUID *service,
                                       LANGID language, const GUID *profile,
                                       BOOL *output) noexcept {
  try {
    return {manager->IsEnabledLanguageProfile(*service, language, *profile, output), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_service(ITfInputProcessorProfiles *manager, const GUID *service,
                                       BOOL install) noexcept {
  try {
    return {install ? manager->Register(*service) : manager->Unregister(*service), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_add(ITfInputProcessorProfiles *manager, const GUID *service,
                                   LANGID language, const GUID *profile, const wchar_t *description,
                                   ULONG description_length, const wchar_t *dll,
                                   ULONG dll_length) noexcept {
  try {
    return {manager->AddLanguageProfile(*service, language, *profile, description,
                                        description_length, dll, dll_length, 0),
            0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_substitute(ITfInputProcessorProfiles *manager, const GUID *service,
                                          LANGID language, const GUID *profile,
                                          HKL layout) noexcept {
  try {
    return {manager->SubstituteKeyboardLayout(*service, language, *profile, layout), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_registrar_category(ITfCategoryMgr *manager, const GUID *service,
                                        BOOL install) noexcept {
  try {
    return {install ? manager->RegisterCategory(*service, GUID_TFCAT_TIP_KEYBOARD, *service)
                    : manager->UnregisterCategory(*service, GUID_TFCAT_TIP_KEYBOARD, *service),
            0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}
