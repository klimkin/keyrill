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
}
