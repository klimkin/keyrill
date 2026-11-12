#include "foreign_calls.h"
#include <cwctype>

extern "C" KrCall kr_foreign_whitespace(WCHAR unit, BOOL *output) noexcept {
  try { *output = iswspace(unit) != 0; return {S_OK, 0}; }
  catch (...) { return {E_UNEXPECTED, 1}; }
}

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

extern "C" KrCall kr_foreign_focus(ITfThreadMgr *manager, ITfDocumentMgr **output) noexcept {
  KR_RETURN(manager->GetFocus(output))
}
extern "C" KrCall kr_foreign_top(ITfDocumentMgr *document, ITfContext **output) noexcept {
  KR_RETURN(document->GetTop(output))
}
extern "C" KrCall kr_foreign_view(ITfContext *context, ITfContextView **output) noexcept {
  KR_RETURN(context->GetActiveView(output))
}
extern "C" KrCall kr_foreign_window(ITfContextView *view, HWND *output) noexcept {
  KR_RETURN(view->GetWnd(output))
}
extern "C" KrCall kr_foreign_status(ITfContext *context, TF_STATUS *output) noexcept {
  KR_RETURN(context->GetStatus(output))
}
extern "C" KrCall kr_foreign_compartment(ITfCompartmentMgr *manager, const GUID *id,
                                         ITfCompartment **output) noexcept {
  KR_RETURN(manager->GetCompartment(*id, output))
}
extern "C" KrCall kr_foreign_property(ITfContext *context, const GUID *id, ITfReadOnlyProperty **output) noexcept {
  KR_RETURN(context->GetAppProperty(*id, output))
}
extern "C" KrCall kr_foreign_property_value(ITfReadOnlyProperty *property, TfEditCookie cookie,
                                            ITfRange *range, VARIANT *output) noexcept {
  KR_RETURN(property->GetValue(cookie, range, output))
}
extern "C" KrCall kr_foreign_variant_clear(VARIANT *value) noexcept {
  KR_RETURN(VariantClear(value))
}
extern "C" KrCall kr_foreign_scopes(ITfInputScope *scope, InputScope **output,
                                    UINT *count) noexcept {
  KR_RETURN(scope->GetInputScopes(output, count))
}
extern "C" KrCall kr_foreign_range_context(ITfRange *range, ITfContext **output) noexcept {
  KR_RETURN(range->GetContext(output))
}
extern "C" KrCall kr_foreign_extent(ITfRangeACP *range, LONG *start, LONG *length) noexcept {
  KR_RETURN(range->GetExtent(start, length))
}
extern "C" KrCall kr_foreign_selection(ITfContext *context, TfEditCookie cookie,
                                       TF_SELECTION *output, ULONG *fetched) noexcept {
  KR_RETURN(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, output, fetched))
}
extern "C" KrCall kr_foreign_set_selection(ITfContext *context, TfEditCookie cookie,
                                           const TF_SELECTION *selection) noexcept {
  KR_RETURN(context->SetSelection(cookie, 1, selection))
}
extern "C" KrCall kr_foreign_clone(ITfRange *range, ITfRange **output) noexcept {
  KR_RETURN(range->Clone(output))
}
extern "C" KrCall kr_foreign_shift_start(ITfRange *range, TfEditCookie cookie, LONG amount,
                                         LONG *shifted) noexcept {
  KR_RETURN(range->ShiftStart(cookie, amount, shifted, nullptr))
}
extern "C" KrCall kr_foreign_shift_end(ITfRange *range, TfEditCookie cookie, LONG amount,
                                       LONG *shifted) noexcept {
  KR_RETURN(range->ShiftEnd(cookie, amount, shifted, nullptr))
}
extern "C" KrCall kr_foreign_collapse(ITfRange *range, TfEditCookie cookie,
                                      TfAnchor anchor) noexcept {
  KR_RETURN(range->Collapse(cookie, anchor))
}
extern "C" KrCall kr_foreign_compositions(ITfContextComposition *context,
                                          IEnumITfCompositionView **output) noexcept {
  KR_RETURN(context->EnumCompositions(output))
}
extern "C" KrCall kr_foreign_next(IEnumITfCompositionView *entries, ITfCompositionView **output,
                                  ULONG *fetched) noexcept {
  KR_RETURN(entries->Next(1, output, fetched))
}
extern "C" KrCall kr_foreign_text(ITfRange *range, TfEditCookie cookie, WCHAR *output,
                                  ULONG capacity, ULONG *count) noexcept {
  KR_RETURN(range->GetText(cookie, 0, output, capacity, count))
}
extern "C" KrCall kr_foreign_set_text(ITfRange *range, TfEditCookie cookie, const WCHAR *text,
                                      LONG length) noexcept {
  KR_RETURN(range->SetText(cookie, 0, text, length))
}
extern "C" KrCall kr_foreign_start_composition(ITfContextComposition *context, TfEditCookie cookie,
                                               ITfRange *range, ITfCompositionSink *sink,
                                               ITfComposition **output) noexcept {
  KR_RETURN(context->StartComposition(cookie, range, sink, output))
}
extern "C" KrCall kr_foreign_composition_range(ITfComposition *composition,
                                               ITfRange **output) noexcept {
  KR_RETURN(composition->GetRange(output))
}
extern "C" KrCall kr_foreign_end_composition(ITfComposition *composition,
                                             TfEditCookie cookie) noexcept {
  KR_RETURN(composition->EndComposition(cookie))
}
extern "C" KrCall kr_foreign_field(decltype(KrWindowsDependencies::capture) capture, void *context,
                                   HWND window, const KrWindowsHostIdentity *host,
                                   KrWindowsFieldFacts *output) noexcept {
  KR_RETURN(capture(context, window, host, output))
}
