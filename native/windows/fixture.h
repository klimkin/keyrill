#pragma once
#include "profiles.h"
#include <inputscope.h>
#include <richedit.h>

namespace keyrill {
using FixtureScopeSetter = HRESULT(WINAPI *)(HWND, InputScope);
struct FixtureField {
  int id;
  const wchar_t *label;
  DWORD style;
  InputScope scope;
};
inline constexpr DWORD fixture_edit_style =
    WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOVSCROLL;
inline constexpr FixtureField fixture_fields[] = {
    {101, L"A. Writable ordinary RichEdit", fixture_edit_style | ES_MULTILINE | ES_WANTRETURN,
     IS_TEXT},
    {102, L"B. Writable ordinary RichEdit", fixture_edit_style | ES_MULTILINE | ES_WANTRETURN,
     IS_TEXT},
    {103, L"Excluded. Read-only", fixture_edit_style | ES_MULTILINE | ES_WANTRETURN | ES_READONLY,
     IS_DEFAULT},
    {104, L"Excluded. Password", fixture_edit_style | ES_PASSWORD, IS_PASSWORD}};

inline HRESULT WINAPI SetFixtureInputScope(HWND window, InputScope scope) {
  HMODULE tsf = GetModuleHandleW(L"msctf.dll");
  auto set_scope =
      tsf ? reinterpret_cast<FixtureScopeSetter>(GetProcAddress(tsf, "SetInputScope")) : nullptr;
  return set_scope ? set_scope(window, scope) : E_FAIL;
}

inline HRESULT
ResetFixtureInputScopes(HWND root, FixtureScopeSetter set_scope = SetFixtureInputScope) noexcept {
  HRESULT first = S_OK;
  for (const auto &field : fixture_fields) {
    HWND child = GetDlgItem(root, field.id);
    if (!child)
      continue;
    HRESULT result = E_UNEXPECTED;
    try {
      result = set_scope(child, IS_DEFAULT);
    } catch (...) {
    }
    if (first == S_OK && result != S_OK)
      first = result;
  }
  return first;
}

inline HWND CreateFixture(HINSTANCE instance, WNDPROC procedure, bool visible,
                          FixtureScopeSetter set_scope = SetFixtureInputScope) {
  if (!LoadLibraryW(L"msftedit.dll"))
    return nullptr;
  HMODULE tsf = LoadLibraryW(L"msctf.dll");
  if (!tsf)
    return nullptr;
  if (!set_scope)
    return nullptr;
  WNDCLASSW type{};
  type.hInstance = instance;
  type.lpfnWndProc = procedure;
  type.lpszClassName = fixture_class;
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassW(&type)) {
    WNDCLASSW existing{};
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS ||
        !GetClassInfoW(instance, fixture_class, &existing) || existing.lpfnWndProc != procedure)
      return nullptr;
  }
  HWND root =
      CreateWindowExW(0, fixture_class, L"Keyrill TSF proof editor", WS_OVERLAPPEDWINDOW,
                      CW_USEDEFAULT, CW_USEDEFAULT, 780, 620, nullptr, nullptr, instance, nullptr);
  if (!root)
    return nullptr;
  int i = 0;
  for (const auto &field : fixture_fields) {
    if (!CreateWindowW(L"STATIC", field.label, WS_CHILD | WS_VISIBLE, 16, 16 + 132 * i, 700, 24,
                       root, nullptr, instance, nullptr)) {
      DestroyWindow(root);
      return nullptr;
    }
    HWND edit =
        CreateWindowExW(0, MSFTEDIT_CLASS, L"", field.style, 16, 42 + 132 * i, 720, 90, root,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(field.id)), instance, nullptr);
    if (!edit) {
      DestroyWindow(root);
      return nullptr;
    }
    SendMessageW(edit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
    HRESULT result = E_UNEXPECTED;
    try {
      result = set_scope(edit, field.scope);
    } catch (...) {
    }
    if (result != S_OK) {
      DestroyWindow(root);
      return nullptr;
    }
    SendMessageW(edit, EM_EMPTYUNDOBUFFER, 0, 0);
    ++i;
  }
  if (visible)
    ShowWindow(root, SW_SHOW);
  return root;
}
} // namespace keyrill
