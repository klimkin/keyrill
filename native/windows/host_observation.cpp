#include "host_observation.h"
#include <appmodel.h>
#include <cwchar>
#include <inputscope.h>

namespace keyrill::host {
using Microsoft::WRL::ComPtr;
namespace {
struct VariantValue {
  VARIANT value{};
  ~VariantValue() { VariantClear(&value); }
};
struct ScopeValues {
  InputScope *value = nullptr;
  ~ScopeValues() { CoTaskMemFree(value); }
};
} // namespace
bool Capture(HANDLE process, DWORD pid, DWORD thread, Identity &output) noexcept {
  output = {};
  FILETIME created{}, exited{}, kernel{}, user{};
  USHORT machine = 0, native = 0;
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user) ||
      !IsWow64Process2(process, &machine, &native) || machine != IMAGE_FILE_MACHINE_UNKNOWN ||
      native != IMAGE_FILE_MACHINE_AMD64)
    return false;
  output.process = pid;
  output.thread = thread;
  output.started = (uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
  wchar_t path[32768]{};
  DWORD length = 32768;
  if (!QueryFullProcessImageNameW(process, 0, path, &length))
    return false;
  const auto *base = wcsrchr(path, L'\\');
  if (!base)
    return false;
  if (_wcsicmp(base + 1, L"KeyrillProofEditor.exe") == 0) {
    output.kind = Kind::ProofEditor;
    return true;
  }
  if (_wcsicmp(base + 1, L"Notepad.exe") != 0)
    return true;
  wchar_t family[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1]{};
  UINT32 family_size = static_cast<UINT32>(std::size(family));
  if (GetPackageFamilyName(process, &family_size, family) != ERROR_SUCCESS ||
      wcscmp(family, L"Microsoft.WindowsNotepad_8wekyb3d8bbwe") != 0)
    return true;
  alignas(PACKAGE_ID) BYTE storage[4096]{};
  UINT32 size = sizeof(storage);
  if (GetPackageId(process, &size, storage) != ERROR_SUCCESS)
    return true;
  const auto &package = *reinterpret_cast<PACKAGE_ID *>(storage);
  if (package.processorArchitecture != PROCESSOR_ARCHITECTURE_AMD64 || !package.name ||
      wcscmp(package.name, L"Microsoft.WindowsNotepad") != 0)
    return true;
  output.version = {package.version.Major, package.version.Minor, package.version.Build,
                    package.version.Revision};
  output.kind = Kind::PackagedNotepad;
  return true;
}

class MetadataSession final : public ITfEditSession {
public:
  MetadataSession(ITfContext *context, ITfThreadMgr *manager, Lifetime lifetime)
      : context_(context), manager_(manager), keep_(lifetime.owner), lifetime_(lifetime) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfEditSession)
      return E_NOINTERFACE;
    *out = static_cast<ITfEditSession *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto remaining = --references_;
    if (!remaining)
      delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
    if (expired_ || entered_)
      return E_FAIL;
    entered_ = true;
    value.entered = 1;
    try {
      Run(cookie);
    } catch (...) {
      value.reason = Reason::Exception;
    }
    value.completed = 1;
    return S_OK;
  }
  bool CaptureIdentity() {
    value.reason = Reason::Native;
    return Query([&] { return context_->QueryInterface(IID_PPV_ARGS(&identity_)); }) && identity_;
  }
  void Expire() noexcept { expired_ = true; }
  Metadata value;

private:
  bool Alive() const noexcept {
    return !expired_ && lifetime_.current(lifetime_.state, lifetime_.generation);
  }
  bool Good(HRESULT result) {
    if (!Alive()) {
      value.reason = Reason::Stale;
      return false;
    }
    if (result != S_OK) {
      value.reason = Reason::Native;
      value.result = static_cast<uint32_t>(result);
      return false;
    }
    return true;
  }
  template <typename F> bool Query(F call) {
    if (!Alive()) {
      value.reason = Reason::Stale;
      return false;
    }
    return Good(call());
  }
  bool Same(IUnknown *object) {
    ComPtr<IUnknown> identity;
    if (!object || !Query([&] { return object->QueryInterface(IID_PPV_ARGS(&identity)); }))
      return false;
    if (identity != identity_) {
      value.reason = Reason::Context;
      return false;
    }
    return true;
  }
  bool Enabled(IUnknown *object, REFGUID id) {
    ComPtr<ITfCompartmentMgr> manager;
    ComPtr<ITfCompartment> compartment;
    if (!Query([&] { return object->QueryInterface(IID_PPV_ARGS(&manager)); }) || !manager ||
        !Query([&] { return manager->GetCompartment(id, &compartment); }) || !compartment)
      return false;
    VariantValue state;
    const bool queried = Query([&] {
      const auto result = compartment->GetValue(&state.value);
      return result == S_FALSE && state.value.vt == VT_EMPTY ? S_OK : result;
    });
    const bool enabled =
        (state.value.vt == VT_EMPTY || (state.value.vt == VT_I4 && state.value.lVal == 0));
    if (!queried || !Alive())
      return false;
    if (!enabled)
      value.reason = Reason::Disabled;
    return enabled;
  }
  void Run(TfEditCookie cookie) {
    value.reason = Reason::Native;
    if (!Alive()) {
      value.reason = Reason::Stale;
      return;
    }
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> top;
    ComPtr<ITfContextView> view;
    HWND window = nullptr;
    if (!Query([&] { return manager_->GetFocus(&document); }) || !document ||
        !Query([&] { return document->GetTop(&top); }) || !Same(top.Get()) ||
        !Query([&] { return context_->GetActiveView(&view); }) || !view ||
        !Query([&] { return view->GetWnd(&window); }))
      return;
    value.focus = !window                ? Focus::Windowless
                  : window == GetFocus() ? Focus::Exact
                                         : Focus::Different;
    if (!Alive()) {
      value.reason = Reason::Stale;
      return;
    }
    const HWND focus = GetFocus();
    if ((window && (GetWindowLongPtrW(window, GWL_STYLE) & ES_PASSWORD)) ||
        (focus && (GetWindowLongPtrW(focus, GWL_STYLE) & ES_PASSWORD))) {
      value.reason = Reason::Secure;
      return;
    }
    if ((window && !IsWindowEnabled(window)) || (focus && !IsWindowEnabled(focus))) {
      value.reason = Reason::Disabled;
      return;
    }
    if ((window && (GetWindowLongPtrW(window, GWL_STYLE) & ES_READONLY)) ||
        (focus && (GetWindowLongPtrW(focus, GWL_STYLE) & ES_READONLY))) {
      value.reason = Reason::ReadOnly;
      return;
    }
    TF_STATUS status{};
    if (!Query([&] { return context_->GetStatus(&status); }))
      return;
    if (status.dwDynamicFlags & TF_SD_READONLY) {
      value.reason = Reason::ReadOnly;
      return;
    }
    if (!Enabled(context_.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED) ||
        !Enabled(context_.Get(), GUID_COMPARTMENT_EMPTYCONTEXT) ||
        !Enabled(manager_.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED))
      return;
    TF_SELECTION selected{};
    ULONG fetched = 0;
    const bool selected_ok = Query([&] {
      return context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selected, &fetched);
    });
    ComPtr<ITfRange> range;
    range.Attach(selected.range);
    if (!selected_ok)
      return;
    if (fetched != 1 || !range || selected.style.fInterimChar) {
      value.reason = Reason::Selection;
      return;
    }
    ComPtr<ITfContext> range_context;
    if (!Query([&] { return range->GetContext(&range_context); }) || !Same(range_context.Get()))
      return;
    value.selection = 1;
    ComPtr<ITfReadOnlyProperty> property;
    value.scope = Scope::Missing;
    value.reason = Reason::MissingScope;
    if (!Query([&] { return context_->GetAppProperty(GUID_PROP_INPUTSCOPE, &property); }) ||
        !property)
      return;
    VariantValue state;
    bool scope_ok = Query([&] { return property->GetValue(cookie, range.Get(), &state.value); });
    ComPtr<ITfInputScope> scope;
    if (scope_ok && state.value.vt == VT_UNKNOWN && state.value.punkVal)
      scope_ok = Query([&] { return state.value.punkVal->QueryInterface(IID_PPV_ARGS(&scope)); });
    else
      scope_ok = false;
    if (!Alive()) {
      value.reason = Reason::Stale;
      return;
    }
    if (!scope_ok || !scope) {
      value.reason = Reason::MissingScope;
      return;
    }
    ScopeValues scopes;
    UINT count = 0;
    scope_ok = Query([&] { return scope->GetInputScopes(&scopes.value, &count); });
    if (!scope_ok)
      return;
    value.scope = count != 1 || !scopes.value     ? Scope::Ambiguous
                  : scopes.value[0] == IS_DEFAULT ? Scope::Default
                  : scopes.value[0] == IS_TEXT    ? Scope::Text
                                                  : Scope::Excluded;
    if (value.scope != Scope::Default && value.scope != Scope::Text) {
      value.reason = Reason::Scope;
      return;
    }
    ComPtr<ITfRangeACP> acp;
    const bool acp_ok = Query([&] { return range.As(&acp); });
    if (!Alive()) {
      value.reason = Reason::Stale;
      return;
    }
    value.acp = acp_ok && acp ? 1u : 0u;
    value.reason = value.acp ? Reason::ProofOnly : Reason::MissingAcp;
  }
  ULONG references_ = 1;
  ComPtr<ITfContext> context_;
  ComPtr<ITfThreadMgr> manager_;
  ComPtr<IUnknown> keep_, identity_;
  Lifetime lifetime_;
  bool expired_ = false, entered_ = false;
};

Metadata Observe(ITfContext *context, ITfThreadMgr *manager, TfClientId client, Lifetime lifetime) {
  Metadata result;
  if (!context || !manager || !lifetime.current(lifetime.state, lifetime.generation)) {
    result.reason = Reason::Stale;
    return result;
  }
  ComPtr<MetadataSession> request;
  request.Attach(new MetadataSession(context, manager, lifetime));
  try {
    if (!request->CaptureIdentity()) {
      request->Expire();
      return request->value;
    }
    HRESULT session = E_UNEXPECTED;
    const auto status =
        context->RequestEditSession(client, request.Get(), TF_ES_SYNC | TF_ES_READ, &session);
    request->Expire();
    result = request->value;
    result.request = static_cast<uint32_t>(status);
    result.session = static_cast<uint32_t>(session);
    if (status != S_OK)
      result.reason = Reason::Request;
    else if (session != S_OK)
      result.reason = Reason::Session;
    else if (!result.completed)
      result.reason = Reason::Absent;
  } catch (...) {
    request->Expire();
    result = request->value;
    result.reason = Reason::Exception;
  }
  return result;
}
} // namespace keyrill::host
