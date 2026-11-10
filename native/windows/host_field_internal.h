#pragma once
#include "host_field.h"
#include <string>
#include <string_view>
#include <uiautomation.h>

namespace keyrill::field::detail {
inline Refusal CopyDocumentText(HRESULT result, BSTR value, int bound, std::wstring &text) {
  const auto length = SysStringLen(value);
  if (result == S_OK && value && length <= static_cast<UINT>(bound))
    text.assign(value, length);
  return result == S_OK && length <= static_cast<UINT>(bound) ? Refusal::None
                                                              : Refusal::TextUnavailable;
}
struct Case {
  PublicCase id;
  const wchar_t *name;
  const char *label;
  std::wstring_view text;
};
inline constexpr Case cases[] = {
    {PublicCase::Empty, L"empty", "empty", L""},
    {PublicCase::ProbeG, L"probe-g", "probe-g", L"g"},
    {PublicCase::FixtureEnBoundary, L"fixture-en-boundary", "fixture-en-boundary", L"ghbdtn "}};
struct LifetimeFacts {
  DWORD process = 0;
  uint64_t process_started = 0;
  DWORD process_wait = WAIT_FAILED;
  DWORD thread_owner = 0;
  DWORD thread = 0;
  uint64_t thread_started = 0;
  DWORD thread_wait = WAIT_FAILED;
};
inline bool Current(const LifetimeFacts &now, const host::Identity &target, DWORD thread,
                    uint64_t thread_started) noexcept {
  return now.process_wait == WAIT_TIMEOUT && now.thread_wait == WAIT_TIMEOUT &&
         now.process == target.process && now.process_started == target.started &&
         now.thread_owner == target.process && now.thread == thread &&
         now.thread_started == thread_started;
}
inline bool Bound(const NativeFocus &native, int pid, HWND window, BOOL compared) noexcept {
  return pid == static_cast<int>(native.pid) && window && window == native.focused.handle &&
         compared == TRUE;
}
struct QueueFocus {
  bool available = false;
  HWND focused = nullptr;
};
inline bool ExactQueues(QueueFocus foreground, QueueFocus focused, HWND root,
                        HWND foreground_window) noexcept {
  return foreground.available && focused.available && foreground.focused &&
         foreground.focused == focused.focused && root && root == foreground_window;
}
struct Selection {
  bool complete = false;
  int count = 0;
  int collapsed = 1;
  int at_end = 1;
};
inline bool EndCaret(const Selection &value) noexcept {
  return value.complete && value.count == 1 && value.collapsed == 0 && value.at_end == 0;
}
inline Refusal NativeSafety(const NativeFocus &value) noexcept {
  if (!value.focused.complete || !value.root.complete || value.parents_truncated)
    return Refusal::NativeFacts;
  if (!value.focused.enabled || (value.focused.style & (ES_PASSWORD | ES_READONLY)) != 0)
    return Refusal::NativeExcluded;
  for (size_t i = 0; i != value.parent_count; ++i)
    if (!value.parents[i].complete)
      return Refusal::NativeFacts;
    else if (!value.parents[i].enabled)
      return Refusal::NativeExcluded;
  return value.root.enabled ? Refusal::None : Refusal::NativeExcluded;
}
inline Refusal Writable(ReadOnly value, ReadOnly text) noexcept {
  if (value == ReadOnly::ReadOnly || text == ReadOnly::ReadOnly)
    return Refusal::ReadOnly;
  if (value == ReadOnly::Mixed || text == ReadOnly::Mixed)
    return Refusal::Mixed;
  if (value == ReadOnly::Unknown || text == ReadOnly::Unknown)
    return Refusal::Writability;
  return value == ReadOnly::Writable || text == ReadOnly::Writable ? Refusal::None
                                                                   : Refusal::Writability;
}
inline ReadOnly Boolean(HRESULT result, BOOL value) noexcept {
  if (result != S_OK)
    return ReadOnly::Unavailable;
  return value == FALSE  ? ReadOnly::Writable
         : value == TRUE ? ReadOnly::ReadOnly
                         : ReadOnly::Unknown;
}
inline ReadOnly AttributeBoolean(VARIANT_BOOL value) noexcept {
  return value == VARIANT_FALSE  ? ReadOnly::Writable
         : value == VARIANT_TRUE ? ReadOnly::ReadOnly
                                 : ReadOnly::Unknown;
}
inline Refusal Eligible(const Metadata &value) noexcept {
  if (value.refusal != Refusal::None)
    return value.refusal;
  const auto native = NativeSafety(value.native);
  if (native != Refusal::None)
    return native;
  if (!value.exact_binding)
    return Refusal::Binding;
  if (value.control_type != UIA_DocumentControlTypeId)
    return Refusal::ControlType;
  if (!value.nonpassword)
    return Refusal::Password;
  if (!value.enabled)
    return Refusal::Disabled;
  return Writable(value.value_readonly, value.text_readonly);
}
template <typename Provider> Metadata Observe(Provider &provider) { return provider.MetadataNow(); }
template <typename Provider> CaseResult Check(Provider &provider, const Case &expected) {
  const auto evidence = provider.MetadataNow();
  auto refusal = Eligible(evidence);
  if (refusal != Refusal::None)
    return {refusal, false};
  if (!evidence.text_pattern)
    return {Refusal::TextUnavailable, false};
  auto revalidate = [&] {
    const auto now = provider.MetadataNow();
    const auto failure = Eligible(now);
    return failure != Refusal::None ? failure : now == evidence ? Refusal::None : Refusal::Stale;
  };
  refusal = revalidate();
  if (refusal != Refusal::None)
    return {refusal, false};
  std::wstring text;
  refusal = provider.ReadDocument(static_cast<int>(expected.text.size() + 1), text);
  if (refusal != Refusal::None)
    return {refusal, false};
  if (text != expected.text)
    return {Refusal::TextMismatch, false};
  refusal = revalidate();
  if (refusal != Refusal::None)
    return {refusal, false};
  refusal = provider.EndCaret();
  if (refusal != Refusal::None)
    return {refusal, false};
  refusal = revalidate();
  return {refusal, refusal == Refusal::None};
}
} // namespace keyrill::field::detail
