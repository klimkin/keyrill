#include "foreign_calls.h"

extern "C" KrCall kr_foreign_advise_single(ITfSourceSingle *source, TfClientId client,
                                           IUnknown *provider) noexcept {
  try {
    return {source->AdviseSingleSink(client, IID_ITfFunctionProvider, provider), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}
extern "C" KrCall kr_foreign_unadvise_single(ITfSourceSingle *source, TfClientId client) noexcept {
  try {
    return {source->UnadviseSingleSink(client, IID_ITfFunctionProvider), 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

#define KR_RETURN(call)                                                                            \
  try {                                                                                            \
    return {call, 0};                                                                              \
  } catch (...) {                                                                                  \
    return {E_UNEXPECTED, 1};                                                                      \
  }

extern "C" KrCall kr_foreign_advise(ITfSource *source, const GUID *id, IUnknown *sink,
                                    DWORD *cookie) noexcept {
  KR_RETURN(source->AdviseSink(*id, sink, cookie))
}
extern "C" KrCall kr_foreign_unadvise(ITfSource *source, DWORD cookie) noexcept {
  KR_RETURN(source->UnadviseSink(cookie))
}
extern "C" KrCall kr_foreign_advise_keys(ITfKeystrokeMgr *keys, TfClientId client,
                                         ITfKeyEventSink *sink) noexcept {
  KR_RETURN(keys->AdviseKeyEventSink(client, sink, TRUE))
}
extern "C" KrCall kr_foreign_unadvise_keys(ITfKeystrokeMgr *keys, TfClientId client) noexcept {
  KR_RETURN(keys->UnadviseKeyEventSink(client))
}
extern "C" KrCall kr_foreign_preserve(ITfKeystrokeMgr *keys, TfClientId client, const GUID *id,
                                      const TF_PRESERVEDKEY *key, const WCHAR *description,
                                      ULONG length) noexcept {
  KR_RETURN(keys->PreserveKey(client, *id, key, description, length))
}
extern "C" KrCall kr_foreign_unpreserve(ITfKeystrokeMgr *keys, const GUID *id,
                                        const TF_PRESERVEDKEY *key) noexcept {
  KR_RETURN(keys->UnpreserveKey(*id, key))
}
extern "C" KrCall kr_foreign_create_profiles(ITfInputProcessorProfileMgr **output) noexcept {
  KR_RETURN(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(output)))
}
extern "C" KrCall kr_foreign_active_profile(ITfInputProcessorProfileMgr *profiles,
                                            TF_INPUTPROCESSORPROFILE *output) noexcept {
  KR_RETURN(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, output))
}
extern "C" KrCall kr_foreign_substitute(ITfInputProcessorProfileSubstituteLayout *profiles,
                                        const GUID *service, LANGID language, const GUID *profile,
                                        HKL *output) noexcept {
  KR_RETURN(profiles->GetSubstituteKeyboardLayout(*service, language, *profile, output))
}
extern "C" KrCall kr_foreign_layouts(int(WINAPI *call)(int, HKL *), int count, HKL *layouts,
                                     int *output) noexcept {
  try {
    *output = call ? call(count, layouts) : GetKeyboardLayoutList(count, layouts);
    return {S_OK, 0};
  } catch (...) {
    return {E_UNEXPECTED, 1};
  }
}

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
