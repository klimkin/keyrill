#pragma once
#include "profiles.h"
#include "service_diagnostics.h"
#include <cstdint>
#include <memory>
#include <string>
#include <wrl/client.h>

namespace keyrill {
using Microsoft::WRL::ComPtr;
extern long object_count;

class Service final : public ITfTextInputProcessorEx,
                      public ITfKeyEventSink,
                      public ITfThreadMgrEventSink,
                      public ITfCompositionSink,
                      public ITfInputProcessorProfileActivationSink,
                      public ITfFunctionProvider,
                      public diagnostics::SnapshotReader {
public:
  explicit Service(ITfInputProcessorProfileMgr *profile_api = nullptr,
                   decltype(&GetKeyboardLayoutList) layouts = GetKeyboardLayoutList);
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr *manager, TfClientId client) override;
  HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr *, TfClientId, DWORD) override;
  HRESULT STDMETHODCALLTYPE Deactivate() override;
  HRESULT STDMETHODCALLTYPE GetType(GUID *) override;
  HRESULT STDMETHODCALLTYPE GetDescription(BSTR *) override;
  HRESULT STDMETHODCALLTYPE GetFunction(REFGUID, REFIID, IUnknown **) override;
  HRESULT STDMETHODCALLTYPE Copy(uint32_t, uint32_t, diagnostics::Snapshot *) override;
  HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override;
  HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext *, WPARAM, LPARAM, BOOL *) override;
  HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext *, WPARAM, LPARAM, BOOL *) override;
  HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext *, WPARAM, LPARAM, BOOL *) override;
  HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext *, WPARAM, LPARAM, BOOL *) override;
  HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext *, REFGUID, BOOL *) override;
  HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr *) override;
  HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr *) override;
  HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr *, ITfDocumentMgr *) override;
  HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext *) override;
  HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext *) override;
  HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie, ITfComposition *) override;
  HRESULT STDMETHODCALLTYPE OnActivated(DWORD, LANGID, REFCLSID, REFGUID, REFGUID, HKL,
                                        DWORD) override;

  bool HandleEvent(ITfContext *context, const kr_event &event);
  bool stopped() const { return stopped_; }

private:
  friend class EditSession;
  struct DiagnosticCallback {
    Service &service;
    explicit DiagnosticCallback(Service &value) : service(value) { ++service.diagnostic_depth_; }
    ~DiagnosticCallback() { --service.diagnostic_depth_; }
  };
  ~Service();
  struct Owned {
    ComPtr<ITfContext> context;
    ComPtr<IUnknown> context_id;
    ComPtr<ITfComposition> composition;
    ComPtr<IUnknown> composition_id;
    LONG start = 0;
    std::wstring verified;
  };
  void Invalidate(bool stop);
  void InvalidateForLifecycle();
  bool Current(uint64_t generation) const;
  void RegisterDiagnostics() noexcept;
  void UnregisterDiagnostics(ITfSourceSingle *, TfClientId) noexcept;
  void Refuse(diagnostics::Stage stage, diagnostics::Cause cause, HRESULT result = S_OK) noexcept {
    diagnostics::Refuse(diagnostics_, stage, cause, result);
  }
  ULONG references_ = 1;
  DWORD thread_ = GetCurrentThreadId();
  uint64_t generation_ = 1;
  bool active_ = false;
  bool busy_ = false;
  bool key_admission_ = false;
  bool stopped_ = false;
  uint32_t mode_ = 0;
  TfClientId client_ = TF_CLIENTID_NULL;
  kr_engine *engine_ = nullptr;
  std::unique_ptr<Owned> owned_;
  ComPtr<ITfThreadMgr> manager_;
  ComPtr<ITfSource> source_;
  ComPtr<ITfKeystrokeMgr> keys_;
  ComPtr<ITfInputProcessorProfileMgr> profiles_manager_;
  const ComPtr<ITfInputProcessorProfileMgr> profile_dependency_;
  const decltype(&GetKeyboardLayoutList) layouts_;
  DWORD thread_cookie_ = TF_INVALID_COOKIE;
  DWORD profile_cookie_ = TF_INVALID_COOKIE;
  uint32_t diagnostic_depth_ = 0;
  diagnostics::Snapshot diagnostics_;
  ComPtr<ITfSourceSingle> diagnostic_source_;
  TfClientId diagnostic_client_ = TF_CLIENTID_NULL;
};
} // namespace keyrill
