#include "profiles.h"
#include <cstdio>
#include <string>
#include <uiautomation.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
namespace {
struct Search {
  DWORD process;
  HWND window = nullptr;
};
BOOL CALLBACK Find(HWND window, LPARAM argument) {
  auto &search = *reinterpret_cast<Search *>(argument);
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  wchar_t name[64]{};
  if (process == search.process && GetClassNameW(window, name, 64) &&
      wcscmp(name, keyrill::fixture_class) == 0) {
    search.window = window;
    return FALSE;
  }
  return TRUE;
}
bool Check(HWND window, const wchar_t *expected, bool caret) {
  ComPtr<IUIAutomation> automation;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&automation))))
    return false;
  ComPtr<IUIAutomationElement> element;
  if (FAILED(automation->ElementFromHandle(window, &element)))
    return false;
  BOOL password = TRUE;
  BOOL enabled = FALSE;
  if (FAILED(element->get_CurrentIsPassword(&password)) || password ||
      FAILED(element->get_CurrentIsEnabled(&enabled)) || !enabled)
    return false;
  ComPtr<IUIAutomationValuePattern> value;
  if (FAILED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))))
    return false;
  BOOL readonly = TRUE;
  if (FAILED(value->get_CurrentIsReadOnly(&readonly)) || readonly)
    return false;
  BSTR text = nullptr;
  if (FAILED(value->get_CurrentValue(&text)))
    return false;
  bool matches = text && std::wstring(text, SysStringLen(text)) == expected;
  SysFreeString(text);
  if (!matches || !caret)
    return matches;
  ComPtr<IUIAutomationTextPattern> pattern;
  if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern))))
    return false;
  ComPtr<IUIAutomationTextRangeArray> selections;
  if (FAILED(pattern->GetSelection(&selections)))
    return false;
  int count = 0;
  if (FAILED(selections->get_Length(&count)) || count != 1)
    return false;
  ComPtr<IUIAutomationTextRange> selection, document;
  if (FAILED(selections->GetElement(0, &selection)) ||
      FAILED(pattern->get_DocumentRange(&document)))
    return false;
  int comparison = 1;
  if (FAILED(selection->CompareEndpoints(TextPatternRangeEndpoint_Start, selection.Get(),
                                         TextPatternRangeEndpoint_End, &comparison)) ||
      comparison)
    return false;
  return SUCCEEDED(selection->CompareEndpoints(TextPatternRangeEndpoint_End, document.Get(),
                                               TextPatternRangeEndpoint_End, &comparison)) &&
         comparison == 0;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  if (argc != 3) {
    fwprintf(stderr,
             L"Usage: KeyrillProofInspect FIXTURE_PID correction|reversal|continuation|empty\n");
    return 2;
  }
  wchar_t *end = nullptr;
  unsigned long pid = wcstoul(argv[1], &end, 10);
  if (!pid || !end || *end)
    return 2;
  const wchar_t *expected = nullptr;
  if (wcscmp(argv[2], L"correction") == 0)
    expected = L"\x043f\x0440\x0438\x0432\x0435\x0442 ";
  if (wcscmp(argv[2], L"reversal") == 0)
    expected = L"ghbdtn ";
  if (wcscmp(argv[2], L"continuation") == 0)
    expected = L"\x043f\x0440\x0438\x0432\x0435\x0442 x";
  if (wcscmp(argv[2], L"empty") == 0)
    expected = L"";
  if (!expected)
    return 2;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  wchar_t path[32768]{};
  DWORD length = 32768;
  bool fixture = process && QueryFullProcessImageNameW(process, 0, path, &length);
  if (process)
    CloseHandle(process);
  const wchar_t *base = wcsrchr(path, L'\\');
  if (!fixture || !base || _wcsicmp(base + 1, L"KeyrillProofEditor.exe"))
    return 2;
  Search search{pid};
  EnumWindows(Find, reinterpret_cast<LPARAM>(&search));
  if (!search.window)
    return 2;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    return 1;
  bool matched = Check(GetDlgItem(search.window, 101), expected, true) &&
                 Check(GetDlgItem(search.window, 102), L"", false);
  CoUninitialize();
  puts(matched ? "Fixture document and caret oracle matched. No text recorded."
               : "Fixture oracle mismatch or UIA unavailable. No text recorded.");
  return matched ? 0 : 1;
}
