#pragma once
#include <msctf.h>
#include <windows.h>

struct KrCall {
  HRESULT result;
  unsigned int exception;
};

extern "C" {
KrCall kr_foreign_query(IUnknown *, const GUID *, void **) noexcept;
KrCall kr_foreign_add_ref(IUnknown *, ULONG *) noexcept;
KrCall kr_foreign_release(IUnknown *, ULONG *) noexcept;
KrCall kr_foreign_request(ITfContext *, TfClientId, ITfEditSession *, DWORD, HRESULT *) noexcept;
KrCall kr_foreign_compartment_value(ITfCompartment *, VARIANT *) noexcept;
KrCall kr_foreign_lifetime(void(WINAPI *)(void *), void *) noexcept;
KrCall kr_foreign_advise(ITfSource *, const GUID *, IUnknown *, DWORD *) noexcept;
KrCall kr_foreign_unadvise(ITfSource *, DWORD) noexcept;
KrCall kr_foreign_advise_single(ITfSourceSingle *, TfClientId, IUnknown *) noexcept;
KrCall kr_foreign_unadvise_single(ITfSourceSingle *, TfClientId) noexcept;
KrCall kr_foreign_advise_keys(ITfKeystrokeMgr *, TfClientId, ITfKeyEventSink *) noexcept;
KrCall kr_foreign_unadvise_keys(ITfKeystrokeMgr *, TfClientId) noexcept;
KrCall kr_foreign_preserve(ITfKeystrokeMgr *, TfClientId, const GUID *, const TF_PRESERVEDKEY *,
                           const WCHAR *, ULONG) noexcept;
KrCall kr_foreign_unpreserve(ITfKeystrokeMgr *, const GUID *, const TF_PRESERVEDKEY *) noexcept;
KrCall kr_foreign_create_profiles(ITfInputProcessorProfileMgr **) noexcept;
KrCall kr_foreign_active_profile(ITfInputProcessorProfileMgr *,
                                 TF_INPUTPROCESSORPROFILE *) noexcept;
KrCall kr_foreign_substitute(ITfInputProcessorProfileSubstituteLayout *, const GUID *, LANGID,
                             const GUID *, HKL *) noexcept;
KrCall kr_foreign_layouts(int(WINAPI *)(int, HKL *), int, HKL *, int *) noexcept;
}
