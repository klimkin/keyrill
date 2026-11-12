#pragma once
#include "host_probe.h"
#include "native_field.h"
#include "runtime_diagnostics.h"
#include "rust/test_api.h"
#include "service_diagnostics.h"
#include <wrl/client.h>

namespace keyrill {
using Microsoft::WRL::ComPtr;

FARPROC TestExport(const char *name);
long TestObjectCount();
long TestWorkerCount();

class TestService final {
public:
  explicit TestService(ITfInputProcessorProfileMgr *profile_api = nullptr,
                       decltype(&GetKeyboardLayoutList) layouts = GetKeyboardLayoutList,
                       const native_field::Source *source = nullptr);
  HRESULT QueryInterface(REFIID id, void **output);
  ULONG AddRef();
  ULONG Release();
  HRESULT Activate(ITfThreadMgr *, TfClientId);
  HRESULT ActivateEx(ITfThreadMgr *, TfClientId, DWORD);
  HRESULT Deactivate();
  HRESULT GetType(GUID *);
  HRESULT GetDescription(BSTR *);
  HRESULT GetFunction(REFGUID, REFIID, IUnknown **);
  HRESULT Copy(uint32_t, uint32_t, diagnostics::Snapshot *);
  HRESULT Copy(uint32_t, uint32_t, diagnostics::SnapshotV2 *);
  HRESULT Copy(uint32_t, uint32_t, runtime::Snapshot *);
  HRESULT OnSetFocus(BOOL);
  HRESULT OnTestKeyDown(ITfContext *, WPARAM, LPARAM, BOOL *);
  HRESULT OnTestKeyUp(ITfContext *, WPARAM, LPARAM, BOOL *);
  HRESULT OnKeyDown(ITfContext *, WPARAM, LPARAM, BOOL *);
  HRESULT OnKeyUp(ITfContext *, WPARAM, LPARAM, BOOL *);
  HRESULT OnPreservedKey(ITfContext *, REFGUID, BOOL *);
  HRESULT OnInitDocumentMgr(ITfDocumentMgr *);
  HRESULT OnUninitDocumentMgr(ITfDocumentMgr *);
  HRESULT OnSetFocus(ITfDocumentMgr *, ITfDocumentMgr *);
  HRESULT OnPushContext(ITfContext *);
  HRESULT OnPopContext(ITfContext *);
  HRESULT OnCompositionTerminated(TfEditCookie, ITfComposition *);
  HRESULT OnActivated(DWORD, LANGID, REFCLSID, REFGUID, REFGUID, HKL, DWORD);
  bool HandleEvent(ITfContext *, const kr_event &);
  bool stopped() const;
  void Maintain() noexcept;

private:
  ~TestService();
  template <class Interface, class Call> HRESULT Forward(REFIID id, Call call) {
    ComPtr<Interface> target;
    HRESULT result = object_->QueryInterface(id, reinterpret_cast<void **>(target.GetAddressOf()));
    return result == S_OK && target ? call(target.Get()) : result;
  }
  ULONG references_ = 1;
  void *handle_ = nullptr;
  ComPtr<IUnknown> object_;
};
} // namespace keyrill
