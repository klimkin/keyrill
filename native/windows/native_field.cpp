#include "native_field.h"
#include "fixture.h"
#include <cwchar>
#include <richedit.h>

namespace keyrill::native_field {
namespace {
Window Describe(HWND handle) noexcept {
  Window result;
  result.handle = handle;
  if (!handle || !IsWindow(handle))
    return result;
  result.thread = GetWindowThreadProcessId(handle, &result.process);
  const int count =
      GetClassNameW(handle, result.class_name.data(), static_cast<int>(result.class_name.size()));
  SetLastError(ERROR_SUCCESS);
  result.style = GetWindowLongPtrW(handle, GWL_STYLE);
  const DWORD style_error = GetLastError();
  SetLastError(ERROR_SUCCESS);
  result.control = GetDlgCtrlID(handle);
  const DWORD control_error = GetLastError();
  result.enabled = IsWindowEnabled(handle) != FALSE;
  result.complete = result.thread && count > 0 &&
                    count < static_cast<int>(result.class_name.size()) - 1 && !style_error &&
                    !control_error;
  return result;
}
bool Node(const Window &node, DWORD process, const wchar_t *class_name) noexcept {
  return node.complete && node.handle && node.process == process && node.thread && node.enabled &&
         !node.control && !(node.style & WS_DISABLED) &&
         wcscmp(node.class_name.data(), class_name) == 0;
}
} // namespace
bool KnownNotepad(const host::Identity &identity) noexcept {
  return identity.kind == host::Kind::PackagedNotepad &&
         identity.version == std::array<uint32_t, 4>{11, 2607, 14, 0};
}
Facts Capture(HWND view, const host::Identity &host) noexcept {
  Facts result;
  result.host = host;
  result.process = GetCurrentProcessId();
  result.thread = GetCurrentThreadId();
  result.view = view;
  result.focus = GetFocus();
  result.foreground = GetForegroundWindow();
  result.root = GetAncestor(view, GA_ROOT);
  result.field = Describe(view);
  result.parent = Describe(GetParent(view));
  result.grandparent = Describe(GetParent(result.parent.handle));
  result.proof_root = Describe(result.root);
  wchar_t path[32768]{};
  const DWORD count = GetModuleFileNameW(nullptr, path, 32768);
  const wchar_t *base = count && count < 32768 ? wcsrchr(path, L'\\') : nullptr;
  result.proof_process = base && _wcsicmp(base + 1, L"KeyrillProofEditor.exe") == 0;
  return result;
}
std::optional<FieldIdentity> Match(const Facts &facts) noexcept {
  const auto &field = facts.field;
  if (!facts.process || !facts.thread || !facts.view || facts.view != facts.focus ||
      field.handle != facts.view || !field.complete || field.process != facts.process ||
      field.thread != facts.thread || !field.enabled ||
      (field.style & (ES_PASSWORD | ES_READONLY | WS_DISABLED)))
    return std::nullopt;
  if (facts.proof_process && _wcsicmp(field.class_name.data(), MSFTEDIT_CLASS) == 0 &&
      (field.control == 101 || field.control == 102) && facts.proof_root.complete &&
      facts.proof_root.handle == facts.root &&
      wcscmp(facts.proof_root.class_name.data(), fixture_class) == 0)
    return FieldIdentity{Kind::Proof,  field.handle,           nullptr, facts.root, facts.process,
                         facts.thread, facts.proof_root.thread};
  const auto &parent = facts.parent;
  const auto &root = facts.grandparent;
  if (!KnownNotepad(facts.host) || facts.host.process != facts.process ||
      facts.host.thread != facts.thread || !Node(field, facts.process, L"RichEditD2DPT") ||
      (field.style & (WS_CHILD | ES_MULTILINE)) != (WS_CHILD | ES_MULTILINE) ||
      !Node(parent, facts.process, L"NotepadTextBox") || parent.thread != facts.thread ||
      !Node(root, facts.process, L"Notepad") || root.handle != facts.root ||
      root.handle != facts.foreground)
    return std::nullopt;
  return FieldIdentity{Kind::Notepad, field.handle, parent.handle, root.handle,
                       facts.process, facts.thread, root.thread};
}
} // namespace keyrill::native_field
