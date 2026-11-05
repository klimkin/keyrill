#pragma once
#include "profiles.h"
#include <inputscope.h>
#include <richedit.h>

namespace keyrill {
inline HWND CreateFixture(HINSTANCE instance, WNDPROC procedure, bool visible) {
  if (!LoadLibraryW(L"msftedit.dll"))
    return nullptr;
  HMODULE tsf = LoadLibraryW(L"msctf.dll");
  if (!tsf)
    return nullptr;
  auto set_scope =
      reinterpret_cast<HRESULT(WINAPI *)(HWND, InputScope)>(GetProcAddress(tsf, "SetInputScope"));
  if (!set_scope)
    return nullptr;
  WNDCLASSW type{};
  type.hInstance = instance;
  type.lpfnWndProc = procedure;
  type.lpszClassName = fixture_class;
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return nullptr;
  HWND root =
      CreateWindowExW(0, fixture_class, L"Keyrill TSF proof editor", WS_OVERLAPPEDWINDOW,
                      CW_USEDEFAULT, CW_USEDEFAULT, 780, 620, nullptr, nullptr, instance, nullptr);
  if (!root)
    return nullptr;
  const wchar_t *labels[] = {L"A. Writable ordinary RichEdit", L"B. Writable ordinary RichEdit",
                             L"Excluded. Read-only", L"Excluded. Password"};
  for (int i = 0; i != 4; ++i) {
    CreateWindowW(L"STATIC", labels[i], WS_CHILD | WS_VISIBLE, 16, 16 + 132 * i, 700, 24, root,
                  nullptr, instance, nullptr);
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOVSCROLL;
    if (i < 3)
      style |= ES_MULTILINE | ES_WANTRETURN;
    if (i == 2)
      style |= ES_READONLY;
    if (i == 3)
      style |= ES_PASSWORD;
    HWND edit =
        CreateWindowExW(0, MSFTEDIT_CLASS, L"", style, 16, 42 + 132 * i, 720, 90, root,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(101 + i)), instance, nullptr);
    if (!edit) {
      DestroyWindow(root);
      return nullptr;
    }
    SendMessageW(edit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
    if (FAILED(set_scope(edit, i == 3 ? IS_PASSWORD : IS_DEFAULT))) {
      DestroyWindow(root);
      return nullptr;
    }
    SendMessageW(edit, EM_EMPTYUNDOBUFFER, 0, 0);
  }
  if (visible)
    ShowWindow(root, SW_SHOW);
  return root;
}
} // namespace keyrill
