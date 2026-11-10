#include "host_field_internal.h"
#include "host_threads.h"
#include <cwchar>
#include <uiautomation.h>

using Microsoft::WRL::ComPtr;
namespace keyrill::field {
namespace {
uint64_t Time(const FILETIME &value) noexcept {
  return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
bool Started(HANDLE handle, bool thread, uint64_t &started) noexcept {
  FILETIME creation{}, exit{}, kernel{}, user{};
  const bool ok = thread ? GetThreadTimes(handle, &creation, &exit, &kernel, &user)
                         : GetProcessTimes(handle, &creation, &exit, &kernel, &user);
  started = Time(creation);
  return ok;
}
Window Describe(HWND handle) noexcept {
  Window result;
  result.handle = handle;
  if (!handle || !IsWindow(handle))
    return result;
  result.tid = GetWindowThreadProcessId(handle, &result.pid);
  const auto length =
      GetClassNameW(handle, result.class_name.data(), static_cast<int>(result.class_name.size()));
  result.class_truncated = length == static_cast<int>(result.class_name.size()) - 1;
  SetLastError(ERROR_SUCCESS);
  result.style = GetWindowLongPtrW(handle, GWL_STYLE);
  const auto style_error = GetLastError();
  SetLastError(ERROR_SUCCESS);
  result.exstyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
  const auto exstyle_error = GetLastError();
  SetLastError(ERROR_SUCCESS);
  result.control = GetDlgCtrlID(handle);
  const auto control_error = GetLastError();
  result.enabled = IsWindowEnabled(handle) != FALSE;
  result.complete = result.tid && length > 0 && !result.class_truncated && !style_error &&
                    !exstyle_error && !control_error;
  return result;
}
class Provider {
public:
  explicit Provider(const DiagnosticTarget &target) : target_(target) {}
  Metadata MetadataNow() {
    Metadata result;
    if (!initialized_) {
      if (!Start()) {
        result.native = native_;
        result.refusal = failure_;
        return result;
      }
    }
    result.native = native_;
    if (!Guard()) {
      result.refusal = failure_;
      return result;
    }
    result.refusal = detail::NativeSafety(native_);
    if (result.refusal != Refusal::None)
      return result;
    ComPtr<IUIAutomationElement> focused, from_handle;
    UIA_HWND provider_window = nullptr;
    BOOL same = FALSE;
    BOOL password = TRUE, enabled = FALSE;
    if (Call([&] { return automation_->GetFocusedElement(&focused); }) != S_OK || !focused ||
        Call([&] { return focused->get_CurrentProcessId(&result.provider_pid); }) != S_OK ||
        Call([&] { return focused->get_CurrentNativeWindowHandle(&provider_window); }) != S_OK) {
      result.refusal = FailedProvider();
      return result;
    }
    result.provider_window = static_cast<HWND>(provider_window);
    if (result.provider_pid != static_cast<int>(target_.identity.process) ||
        result.provider_window != native_.focused.handle || !result.provider_window) {
      result.refusal = Refusal::Binding;
      return result;
    }
    if (Call([&] {
          return automation_->ElementFromHandle(native_.focused.handle, &from_handle);
        }) != S_OK ||
        !from_handle ||
        Call([&] {
          return automation_->CompareElements(focused.Get(), from_handle.Get(), &same);
        }) != S_OK ||
        !detail::Bound(native_, result.provider_pid, result.provider_window, same)) {
      result.refusal = failure_ == Refusal::Stale ? failure_ : Refusal::Binding;
      return result;
    }
    if (element_) {
      same = FALSE;
      if (Call([&] {
            return automation_->CompareElements(element_.Get(), focused.Get(), &same);
          }) != S_OK ||
          same != TRUE) {
        result.refusal = Refusal::Stale;
        return result;
      }
    } else {
      element_ = focused;
    }
    result.exact_binding = true;
    if (Call([&] { return focused->get_CurrentControlType(&result.control_type); }) != S_OK) {
      result.refusal = FailedProvider();
      return result;
    }
    if (Call([&] { return focused->get_CurrentIsPassword(&password); }) != S_OK ||
        (password != FALSE && password != TRUE)) {
      result.refusal = FailedProvider();
      return result;
    }
    result.nonpassword = password == FALSE;
    if (!result.nonpassword) {
      result.refusal = Refusal::Password;
      return result;
    }
    if (Call([&] { return focused->get_CurrentIsEnabled(&enabled); }) != S_OK ||
        (enabled != FALSE && enabled != TRUE)) {
      result.refusal = FailedProvider();
      return result;
    }
    result.enabled = enabled == TRUE;
    if (!result.enabled) {
      result.refusal = Refusal::Disabled;
      return result;
    }
    ComPtr<IUIAutomationValuePattern> value;
    result.value_readonly = ReadOnly::Unavailable;
    if (Call([&] {
          return focused->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value));
        }) == S_OK &&
        value) {
      BOOL readonly = TRUE;
      const auto hr = Call([&] { return value->get_CurrentIsReadOnly(&readonly); });
      result.value_readonly = detail::Boolean(hr, readonly);
    }
    result.text_readonly = ReadOnly::Unavailable;
    ComPtr<IUIAutomationTextPattern> text;
    ComPtr<IUIAutomationTextRange> document;
    if (Call([&] {
          return focused->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text));
        }) == S_OK &&
        text) {
      result.text_pattern = true;
      if (Call([&] { return text->get_DocumentRange(&document); }) == S_OK && document) {
        VARIANT attribute;
        VariantInit(&attribute);
        if (Call([&] {
              return document->GetAttributeValue(UIA_IsReadOnlyAttributeId, &attribute);
            }) == S_OK) {
          result.text_readonly = Attribute(attribute);
        }
        VariantClear(&attribute);
      }
    }
    if (!Guard()) {
      result.refusal = failure_;
      return result;
    }
    result.refusal = Refusal::None;
    text_ = text;
    document_ = document;
    return result;
  }
  Refusal ReadDocument(int bound, std::wstring &text) {
    if (!document_ || !Guard())
      return FailedProvider();
    BSTR value = nullptr;
    const auto hr = Call([&] { return document_->GetText(bound, &value); });
    const auto length = SysStringLen(value);
    if (hr == S_OK && value && length <= static_cast<UINT>(bound))
      text.assign(value, length);
    SysFreeString(value);
    if (failure_ == Refusal::Stale)
      return failure_;
    return hr == S_OK && length <= static_cast<UINT>(bound) ? Refusal::None
                                                            : Refusal::TextUnavailable;
  }
  Refusal EndCaret() {
    if (!text_ || !document_ || !Guard())
      return FailedProvider();
    ComPtr<IUIAutomationTextRangeArray> selections;
    ComPtr<IUIAutomationTextRange> selection;
    detail::Selection facts;
    if (Call([&] { return text_->GetSelection(&selections); }) != S_OK || !selections ||
        Call([&] { return selections->get_Length(&facts.count); }) != S_OK || facts.count != 1 ||
        Call([&] { return selections->GetElement(0, &selection); }) != S_OK || !selection ||
        Call([&] {
          return selection->CompareEndpoints(TextPatternRangeEndpoint_Start, selection.Get(),
                                             TextPatternRangeEndpoint_End, &facts.collapsed);
        }) != S_OK ||
        facts.collapsed != 0 ||
        Call([&] {
          return selection->CompareEndpoints(TextPatternRangeEndpoint_End, document_.Get(),
                                             TextPatternRangeEndpoint_End, &facts.at_end);
        }) != S_OK)
      return failure_ == Refusal::Stale ? failure_ : Refusal::Selection;
    facts.complete = true;
    return detail::EndCaret(facts) ? Refusal::None : Refusal::Selection;
  }

private:
  bool Start() {
    if (!target_.process || !target_.identity.process || !target_.identity.started ||
        target_.identity.kind == host::Kind::Unknown)
      return false;
    const HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(foreground, &pid);
    if (!foreground || pid != target_.identity.process || !tid) {
      failure_ = Refusal::NativeFocus;
      return false;
    }
    foreground_thread_ = inspection::CaptureThread(target_.process, pid, tid);
    if (foreground_thread_.state != inspection::ThreadState::Live ||
        !Started(foreground_thread_.handle.get(), true, foreground_started_)) {
      failure_ = Refusal::Stale;
      return false;
    }
    GUITHREADINFO info{sizeof(info)};
    DWORD focused_pid = 0;
    if (!GetGUIThreadInfo(tid, &info) || !info.hwndFocus ||
        GetAncestor(info.hwndFocus, GA_ROOT) != foreground) {
      failure_ = Refusal::NativeFocus;
      return false;
    }
    const auto focused_tid = GetWindowThreadProcessId(info.hwndFocus, &focused_pid);
    if (!focused_tid || focused_pid != pid) {
      failure_ = Refusal::NativeFocus;
      return false;
    }
    focused_thread_ = inspection::CaptureThread(target_.process, pid, focused_tid);
    if (focused_thread_.state != inspection::ThreadState::Live ||
        !Started(focused_thread_.handle.get(), true, focused_started_)) {
      failure_ = Refusal::Stale;
      return false;
    }
    if (!Sample(native_)) {
      failure_ = Refusal::NativeFocus;
      return false;
    }
    initialized_ = true;
    failure_ = detail::NativeSafety(native_);
    if (failure_ != Refusal::None)
      return false;
    if (Call([&] {
          return CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&automation_));
        }) != S_OK ||
        !automation_) {
      if (failure_ != Refusal::Stale)
        failure_ = Refusal::Provider;
      initialized_ = false;
      return false;
    }
    return true;
  }
  bool Sample(NativeFocus &value) {
    auto live = [&](const inspection::ThreadCheck &thread, uint64_t started) {
      detail::LifetimeFacts lifetime;
      lifetime.process_wait = WaitForSingleObject(target_.process, 0);
      lifetime.process = GetProcessId(target_.process);
      lifetime.thread_wait = WaitForSingleObject(thread.handle.get(), 0);
      lifetime.thread_owner = GetProcessIdOfThread(thread.handle.get());
      lifetime.thread = GetThreadId(thread.handle.get());
      return Started(target_.process, false, lifetime.process_started) &&
             Started(thread.handle.get(), true, lifetime.thread_started) &&
             detail::Current(lifetime, target_.identity, thread.tid, started);
    };
    if (!live(foreground_thread_, foreground_started_) || !live(focused_thread_, focused_started_))
      return false;
    value.foreground = GetForegroundWindow();
    DWORD pid = 0;
    value.foreground_tid = GetWindowThreadProcessId(value.foreground, &pid);
    if (!value.foreground || pid != target_.identity.process ||
        value.foreground_tid != foreground_thread_.tid)
      return false;
    GUITHREADINFO info{sizeof(info)};
    if (!GetGUIThreadInfo(value.foreground_tid, &info) || !info.hwndFocus ||
        GetAncestor(info.hwndFocus, GA_ROOT) != value.foreground)
      return false;
    value.pid = pid;
    value.process_started = target_.identity.started;
    value.foreground_thread_started = foreground_started_;
    value.focused_thread_started = focused_started_;
    value.focused = Describe(info.hwndFocus);
    value.root = Describe(GetAncestor(info.hwndFocus, GA_ROOT));
    GUITHREADINFO focused_info{sizeof(focused_info)};
    const bool focused_read = GetGUIThreadInfo(focused_thread_.tid, &focused_info) != FALSE;
    if (value.focused.pid != pid || value.focused.tid != focused_thread_.tid ||
        !detail::ExactQueues({true, info.hwndFocus}, {focused_read, focused_info.hwndFocus},
                             value.root.handle, value.foreground))
      return false;
    HWND parent = GetAncestor(info.hwndFocus, GA_PARENT);
    while (parent && value.parent_count != value.parents.size()) {
      value.parents[value.parent_count++] = Describe(parent);
      parent = GetAncestor(parent, GA_PARENT);
    }
    value.parents_truncated = parent != nullptr;
    return true;
  }
  bool Guard() {
    NativeFocus now;
    if (failure_ == Refusal::Stale || !Sample(now) || now != native_) {
      failure_ = Refusal::Stale;
      return false;
    }
    return true;
  }
  template <typename Function> HRESULT Call(Function function) {
    if (!Guard())
      return E_ABORT;
    const HRESULT result = function();
    return Guard() ? result : E_ABORT;
  }
  Refusal FailedProvider() const {
    return failure_ == Refusal::Stale ? failure_ : Refusal::Provider;
  }
  ReadOnly Attribute(const VARIANT &value) {
    if (value.vt == VT_BOOL)
      return detail::AttributeBoolean(value.boolVal);
    if (value.vt != VT_UNKNOWN || !value.punkVal)
      return ReadOnly::Unknown;
    ComPtr<IUnknown> mixed, unsupported, identity;
    if (Call([&] { return value.punkVal->QueryInterface(IID_PPV_ARGS(&identity)); }) != S_OK ||
        Call([&] { return automation_->get_ReservedMixedAttributeValue(&mixed); }) != S_OK ||
        Call([&] { return automation_->get_ReservedNotSupportedValue(&unsupported); }) != S_OK)
      return ReadOnly::Unknown;
    if (identity == mixed)
      return ReadOnly::Mixed;
    return identity == unsupported ? ReadOnly::Unsupported : ReadOnly::Unknown;
  }
  DiagnosticTarget target_;
  inspection::ThreadCheck foreground_thread_;
  inspection::ThreadCheck focused_thread_;
  uint64_t foreground_started_ = 0;
  uint64_t focused_started_ = 0;
  NativeFocus native_;
  bool initialized_ = false;
  Refusal failure_ = Refusal::Target;
  ComPtr<IUIAutomation> automation_;
  ComPtr<IUIAutomationElement> element_;
  ComPtr<IUIAutomationTextPattern> text_;
  ComPtr<IUIAutomationTextRange> document_;
};
} // namespace
bool ParsePublicCase(const wchar_t *name, PublicCase &output) noexcept {
  if (name)
    for (const auto &entry : detail::cases)
      if (wcscmp(name, entry.name) == 0) {
        output = entry.id;
        return true;
      }
  return false;
}
const char *CaseName(PublicCase id) noexcept {
  for (const auto &entry : detail::cases)
    if (entry.id == id)
      return entry.label;
  return "invalid";
}
Metadata Observe(const DiagnosticTarget &target) {
  Provider provider(target);
  return detail::Observe(provider);
}
CaseResult CheckPublicCase(const DiagnosticTarget &target, PublicCase id) {
  Provider provider(target);
  for (const auto &entry : detail::cases)
    if (entry.id == id)
      return detail::Check(provider, entry);
  return {Refusal::Target, false};
}
} // namespace keyrill::field
