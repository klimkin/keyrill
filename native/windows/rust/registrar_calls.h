#pragma once
#include "foreign_calls.h"

extern "C" {
KrCall kr_registrar_create(BOOL, void **) noexcept;
KrCall kr_registrar_entries(ITfInputProcessorProfiles *, LANGID,
                            IEnumTfLanguageProfiles **) noexcept;
KrCall kr_registrar_next(IEnumTfLanguageProfiles *, TF_LANGUAGEPROFILE *, ULONG *) noexcept;
KrCall kr_registrar_description(ITfInputProcessorProfiles *, const GUID *, LANGID, const GUID *,
                                BSTR *) noexcept;
KrCall kr_registrar_enable(ITfInputProcessorProfiles *, const GUID *, LANGID, const GUID *,
                           BOOL) noexcept;
KrCall kr_registrar_enabled(ITfInputProcessorProfiles *, const GUID *, LANGID, const GUID *,
                            BOOL *) noexcept;
KrCall kr_registrar_service(ITfInputProcessorProfiles *, const GUID *, BOOL) noexcept;
KrCall kr_registrar_add(ITfInputProcessorProfiles *, const GUID *, LANGID, const GUID *,
                        const wchar_t *, ULONG, const wchar_t *, ULONG) noexcept;
KrCall kr_registrar_substitute(ITfInputProcessorProfiles *, const GUID *, LANGID, const GUID *,
                               HKL) noexcept;
KrCall kr_registrar_category(ITfCategoryMgr *, const GUID *, BOOL) noexcept;
}
