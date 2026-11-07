#include "profiles.h"
#include "routing.h"
#include "service_diagnostics.h"
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
  const std::wstring expected_rich_edit_value = std::wstring(expected) + L'\r';
  bool matches = text && std::wstring(text, SysStringLen(text)) == expected_rich_edit_value;
  SysFreeString(text);
  if (!matches) {
    puts(caret ? "A document value mismatch." : "B document value mismatch.");
    return false;
  }
  if (!caret)
    return true;
  ComPtr<IUIAutomationTextPattern> pattern;
  if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern))))
    return false;
  ComPtr<IUIAutomationTextRangeArray> selections;
  if (FAILED(pattern->GetSelection(&selections)))
    return false;
  int count = 0;
  if (FAILED(selections->get_Length(&count)) || count != 1) {
    puts("A selection count unavailable or not one.");
    return false;
  }
  ComPtr<IUIAutomationTextRange> selection, document;
  if (FAILED(selections->GetElement(0, &selection)) ||
      FAILED(pattern->get_DocumentRange(&document)))
    return false;
  int comparison = 1;
  HRESULT compared = selection->CompareEndpoints(TextPatternRangeEndpoint_Start, selection.Get(),
                                                 TextPatternRangeEndpoint_End, &comparison);
  if (FAILED(compared) || comparison) {
    puts(FAILED(compared) ? "A selection comparison unavailable."
                          : "A selection is not collapsed.");
    return false;
  }
  bool end = SUCCEEDED(selection->CompareEndpoints(TextPatternRangeEndpoint_End, document.Get(),
                                                   TextPatternRangeEndpoint_End, &comparison)) &&
             comparison == 0;
  if (!end)
    puts("A caret is not at document end or comparison unavailable.");
  return end;
}
bool Routing(HWND root) {
  HWND status = GetDlgItem(root, 105);
  wchar_t type[16]{};
  if (!status || !GetClassNameW(status, type, 16) || _wcsicmp(type, L"STATIC"))
    return false;
  ComPtr<IUIAutomation> automation;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&automation))))
    return false;
  ComPtr<IUIAutomationElement> element;
  if (FAILED(automation->ElementFromHandle(status, &element)))
    return false;
  BSTR name = nullptr;
  HRESULT result = element->get_CurrentName(&name);
  keyrill::routing::Snapshot snapshot;
  bool parsed = SUCCEEDED(result) && name &&
                keyrill::routing::Parse(std::wstring_view(name, SysStringLen(name)), snapshot);
  SysFreeString(name);
  if (!parsed)
    return false;
  wchar_t known[keyrill::routing::status_capacity]{};
  if (!keyrill::routing::Format(snapshot, known))
    return false;
  wprintf(L"%ls\n", known);
  return true;
}
bool ServiceStatus(HWND root) {
  HWND status = GetDlgItem(root, 106);
  wchar_t type[16]{};
  if (!status || !GetClassNameW(status, type, 16) || _wcsicmp(type, L"STATIC"))
    return false;
  ComPtr<IUIAutomation> automation;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&automation))))
    return false;
  ComPtr<IUIAutomationElement> element;
  if (FAILED(automation->ElementFromHandle(status, &element)))
    return false;
  BSTR name = nullptr;
  HRESULT result = element->get_CurrentName(&name);
  keyrill::diagnostics::Publication publication;
  bool parsed =
      SUCCEEDED(result) && name &&
      keyrill::diagnostics::Parse(std::wstring_view(name, SysStringLen(name)), publication);
  SysFreeString(name);
  if (!parsed)
    return false;
  wchar_t known[keyrill::diagnostics::status_capacity]{};
  if (!keyrill::diagnostics::Format(publication, known))
    return false;
  wprintf(L"%ls\n", known);
  return true;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  if (argc != 3) {
    fwprintf(stderr, L"Usage: KeyrillProofInspect FIXTURE_PID "
                     L"correction|reversal|continuation|focus|empty|routing|service\n");
    return 2;
  }
  wchar_t *end = nullptr;
  unsigned long pid = wcstoul(argv[1], &end, 10);
  if (!pid || !end || *end)
    return 2;
  const wchar_t *expected = nullptr;
  const wchar_t *other_expected = L"";
  if (wcscmp(argv[2], L"correction") == 0)
    expected = L"\x043f\x0440\x0438\x0432\x0435\x0442 ";
  if (wcscmp(argv[2], L"reversal") == 0)
    expected = L"ghbdtn ";
  if (wcscmp(argv[2], L"continuation") == 0)
    expected = L"\x043f\x0440\x0438\x0432\x0435\x0442 x";
  if (wcscmp(argv[2], L"focus") == 0) {
    expected = L"\x043f\x0440\x0438\x0432\x0435\x0442 ghbdtn ";
    other_expected = L"x";
  }
  if (wcscmp(argv[2], L"empty") == 0)
    expected = L"";
  bool routing = wcscmp(argv[2], L"routing") == 0;
  bool service = wcscmp(argv[2], L"service") == 0;
  if (!expected && !routing && !service)
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
  if (routing || service) {
    bool matched = service ? ServiceStatus(search.window) : Routing(search.window);
    CoUninitialize();
    if (!matched)
      puts("Fixture diagnostic status unavailable or malformed. No text recorded.");
    return matched ? 0 : 1;
  }
  bool matched = Check(GetDlgItem(search.window, 101), expected, true);
  bool other = Check(GetDlgItem(search.window, 102), other_expected, false);
  matched = matched && other;
  CoUninitialize();
  puts(matched ? "Fixture document and caret oracle matched. No text recorded."
               : "Fixture oracle mismatch or UIA unavailable. No text recorded.");
  return matched ? 0 : 1;
}
