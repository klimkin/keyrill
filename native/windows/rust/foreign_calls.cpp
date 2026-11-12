#include "foreign_calls.h"

extern "C" KrCall kr_foreign_query(IUnknown *object, const GUID *id, void **output) noexcept {
  try {
    return {object->QueryInterface(*id, output), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_foreign_lifetime(void(WINAPI *call)(void *), void *context) noexcept {
  try {
    call(context);
    return {S_OK, 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_foreign_add_ref(IUnknown *object, ULONG *count) noexcept {
  try {
    *count = object->AddRef();
    return {S_OK, 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_foreign_release(IUnknown *object, ULONG *count) noexcept {
  try {
    *count = object->Release();
    return {S_OK, 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_foreign_request(ITfContext *context, TfClientId client,
                                     ITfEditSession *session, DWORD flags,
                                     HRESULT *session_result) noexcept {
  try {
    return {context->RequestEditSession(client, session, flags, session_result), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

extern "C" KrCall kr_foreign_compartment_value(ITfCompartment *compartment,
                                               VARIANT *output) noexcept {
  try {
    return {compartment->GetValue(output), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}
