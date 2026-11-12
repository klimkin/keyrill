#pragma once
#include "test_api.h"
#include <inputscope.h>
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
KrCall kr_foreign_focus(ITfThreadMgr *manager, ITfDocumentMgr **output) noexcept;
KrCall kr_foreign_top(ITfDocumentMgr *document, ITfContext **output) noexcept;
KrCall kr_foreign_view(ITfContext *context, ITfContextView **output) noexcept;
KrCall kr_foreign_window(ITfContextView *view, HWND *output) noexcept;
KrCall kr_foreign_status(ITfContext *context, TF_STATUS *output) noexcept;
KrCall kr_foreign_compartment(ITfCompartmentMgr *manager, const GUID *id,
                              ITfCompartment **output) noexcept;
KrCall kr_foreign_property(ITfContext *context, const GUID *id,
                           ITfReadOnlyProperty **output) noexcept;
KrCall kr_foreign_whitespace(WCHAR unit, BOOL *output) noexcept;
KrCall kr_foreign_property_value(ITfReadOnlyProperty *property, TfEditCookie cookie,
                                 ITfRange *range, VARIANT *output) noexcept;
KrCall kr_foreign_variant_clear(VARIANT *value) noexcept;
KrCall kr_foreign_scopes(ITfInputScope *scope, InputScope **output, UINT *count) noexcept;
KrCall kr_foreign_range_context(ITfRange *range, ITfContext **output) noexcept;
KrCall kr_foreign_extent(ITfRangeACP *range, LONG *start, LONG *length) noexcept;
KrCall kr_foreign_selection(ITfContext *context, TfEditCookie cookie, TF_SELECTION *output,
                            ULONG *fetched) noexcept;
KrCall kr_foreign_set_selection(ITfContext *context, TfEditCookie cookie,
                                const TF_SELECTION *selection) noexcept;
KrCall kr_foreign_clone(ITfRange *range, ITfRange **output) noexcept;
KrCall kr_foreign_shift_start(ITfRange *range, TfEditCookie cookie, LONG amount,
                              LONG *shifted) noexcept;
KrCall kr_foreign_shift_end(ITfRange *range, TfEditCookie cookie, LONG amount,
                            LONG *shifted) noexcept;
KrCall kr_foreign_collapse(ITfRange *range, TfEditCookie cookie, TfAnchor anchor) noexcept;
KrCall kr_foreign_compositions(ITfContextComposition *context,
                               IEnumITfCompositionView **output) noexcept;
KrCall kr_foreign_next(IEnumITfCompositionView *entries, ITfCompositionView **output,
                       ULONG *fetched) noexcept;
KrCall kr_foreign_text(ITfRange *range, TfEditCookie cookie, WCHAR *output, ULONG capacity,
                       ULONG *count) noexcept;
KrCall kr_foreign_set_text(ITfRange *range, TfEditCookie cookie, const WCHAR *text,
                           LONG length) noexcept;
KrCall kr_foreign_start_composition(ITfContextComposition *context, TfEditCookie cookie,
                                    ITfRange *range, ITfCompositionSink *sink,
                                    ITfComposition **output) noexcept;
KrCall kr_foreign_composition_range(ITfComposition *composition, ITfRange **output) noexcept;
KrCall kr_foreign_end_composition(ITfComposition *composition, TfEditCookie cookie) noexcept;
KrCall kr_foreign_field(decltype(KrWindowsDependencies::capture) capture, void *context,
                        HWND window, const KrWindowsHostIdentity *host,
                        KrWindowsFieldFacts *output) noexcept;
}
