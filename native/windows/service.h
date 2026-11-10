#pragma once
#include "host_probe.h"
#include "native_field.h"
#include "profiles.h"
#include "runtime_diagnostics.h"
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
                      public diagnostics::SnapshotReader,
                      public diagnostics::SnapshotReaderV2,
                      public runtime::Reader {
public:
  explicit Service(ITfInputProcessorProfileMgr *profile_api = nullptr,
                   decltype(&GetKeyboardLayoutList) layouts = GetKeyboardLayoutList,
                   vocabulary::ModuleOwner module_owner = vocabulary::ModuleOwner::Dll,
                   const native_field::Source *field_source = nullptr);
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
  HRESULT STDMETHODCALLTYPE Copy(uint32_t, uint32_t, diagnostics::SnapshotV2 *) override;
  HRESULT STDMETHODCALLTYPE Copy(uint32_t, uint32_t, runtime::Snapshot *) override;
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
  void Maintain() noexcept;

private:
  friend class EditSession;
  struct DiagnosticCallback {
    Service &service;
    explicit DiagnosticCallback(Service &value) : service(value) { ++service.diagnostic_depth_; }
    ~DiagnosticCallback() {
      if (!--service.diagnostic_depth_)
        service.PublishObservation();
    }
  };
  ~Service();
  struct Owned {
    ComPtr<ITfContext> context;
    ComPtr<IUnknown> context_id;
    ComPtr<ITfComposition> composition;
    ComPtr<IUnknown> composition_id;
    uint32_t diagnostic_serial = 0;
    LONG start = 0;
    std::wstring verified;
  };
  void Invalidate(bool stop);
  void InvalidateForLifecycle(diagnostics::LifecycleCause cause);
  bool StartMaintenance();
  void StopMaintenance() noexcept;
  static LRESULT CALLBACK MaintenanceWindow(HWND, UINT, WPARAM, LPARAM);
  bool Current(uint64_t generation) const;
  void RegisterDiagnostics() noexcept;
  void PublishObservation() noexcept;
  void ObserveHost(ITfContext *);
  void UnregisterDiagnostics(ITfSourceSingle *, TfClientId) noexcept;
  void Refuse(diagnostics::Stage stage, diagnostics::Cause cause, HRESULT result = S_OK) noexcept {
    diagnostics::Refuse(diagnostics_, stage, cause, result);
  }
  ULONG references_ = 1;
  DWORD thread_ = GetCurrentThreadId();
  uint64_t generation_ = 1;
  enum class Activation { Inactive, Active, Deactivating };
  Activation activation_ = Activation::Inactive;
  bool Active() const { return activation_ == Activation::Active; }
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
  const native_field::Source *const field_source_;
  DWORD thread_cookie_ = TF_INVALID_COOKIE;
  DWORD profile_cookie_ = TF_INVALID_COOKIE;
  uint32_t diagnostic_depth_ = 0;
  diagnostics::Snapshot diagnostics_;
  diagnostics::LifecycleJournal lifecycle_;
  ComPtr<ITfSourceSingle> diagnostic_source_;
  TfClientId diagnostic_client_ = TF_CLIENTID_NULL;
  const vocabulary::ModuleOwner module_owner_;
  std::optional<vocabulary::Config> accepted_config_;
  vocabulary::Worker vocabulary_;
  std::optional<vocabulary::Ready> pending_ready_;
  runtime::Snapshot runtime_;
  HWND maintenance_window_ = nullptr;
  HINSTANCE maintenance_module_ = nullptr;
  host::Identity host_identity_;
  host::Metadata host_metadata_;
  probe::Publisher host_publisher_;
  uint64_t observed_generation_ = 0;
  bool observing_host_ = false;
};
} // namespace keyrill
