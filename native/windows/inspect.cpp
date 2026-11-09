#include "denial_fixture.h"
#include "profiles.h"
#include "routing.h"
#include "runtime_diagnostics.h"
#include "service_diagnostics.h"
#include "shutdown_probe.h"
#include "source_activation_probe.h"
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
bool ReadServicePublication(HWND root, keyrill::diagnostics::PublicationV2 &publication) {
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
  bool parsed =
      SUCCEEDED(result) && name &&
      keyrill::diagnostics::Parse(std::wstring_view(name, SysStringLen(name)), publication);
  SysFreeString(name);
  return parsed;
}
bool ServiceStatus(HWND root) {
  keyrill::diagnostics::PublicationV2 publication;
  if (!ReadServicePublication(root, publication))
    return false;
  wchar_t known[keyrill::diagnostics::status_capacity]{};
  if (!keyrill::diagnostics::Format(publication, known))
    return false;
  wprintf(L"%ls\n", known);
  return true;
}
keyrill::denial::Setup DenialSetup(HWND root) {
  HWND status = GetDlgItem(root, keyrill::denial::setup_id);
  wchar_t type[16]{};
  if (!status || !GetClassNameW(status, type, 16) || _wcsicmp(type, L"STATIC"))
    return keyrill::denial::Setup::Unknown;
  ComPtr<IUIAutomation> automation;
  ComPtr<IUIAutomationElement> element;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&automation))) ||
      FAILED(automation->ElementFromHandle(status, &element)))
    return keyrill::denial::Setup::Unknown;
  BSTR name = nullptr;
  HRESULT result = element->get_CurrentName(&name);
  const auto setup = SUCCEEDED(result) && name
                         ? keyrill::denial::ParseSetup(std::wstring_view(name, SysStringLen(name)))
                         : keyrill::denial::Setup::Unknown;
  SysFreeString(name);
  return setup;
}
bool NativeDenial(HWND root) {
  if (DenialSetup(root) != keyrill::denial::Setup::Confirmed)
    return false;
  keyrill::diagnostics::PublicationV2 first, second;
  if (!ReadServicePublication(root, first) || !Check(GetDlgItem(root, 101), L"ghbdtn ", true) ||
      !Check(GetDlgItem(root, 102), L"", false))
    return false;
  Sleep(600);
  return ReadServicePublication(root, second) &&
         keyrill::denial::Matches(DenialSetup(root), first, second) &&
         Check(GetDlgItem(root, 101), L"ghbdtn ", true) && Check(GetDlgItem(root, 102), L"", false);
}
bool ShutdownMode(HWND root) {
  HWND status = GetDlgItem(root, keyrill::shutdown::mode_id);
  wchar_t type[16]{};
  if (!status || !GetClassNameW(status, type, 16) || _wcsicmp(type, L"STATIC"))
    return false;
  ComPtr<IUIAutomation> automation;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&automation))))
    return false;
  for (const auto &entry : {std::pair{root, keyrill::shutdown::title},
                            std::pair{status, keyrill::shutdown::mode_status}}) {
    ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->ElementFromHandle(entry.first, &element)))
      return false;
    BSTR name = nullptr;
    const HRESULT result = element->get_CurrentName(&name);
    const bool matches =
        SUCCEEDED(result) && name && std::wstring_view(name, SysStringLen(name)) == entry.second;
    SysFreeString(name);
    if (!matches)
      return false;
  }
  return true;
}
bool ReadSourceResult(HWND root, keyrill::source_proof::Status &status) {
  using namespace keyrill::source_proof;
  ComPtr<IUIAutomation> automation;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&automation))))
    return false;
  for (int id : {0, mode_id, result_id}) {
    HWND window = id ? GetDlgItem(root, id) : root;
    wchar_t type[64]{};
    if (!window || (id && (!GetClassNameW(window, type, 64) || _wcsicmp(type, L"STATIC"))))
      return false;
    ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->ElementFromHandle(window, &element)))
      return false;
    BSTR name = nullptr;
    const auto result = element->get_CurrentName(&name);
    const auto value = name ? std::wstring_view(name, SysStringLen(name)) : std::wstring_view{};
    const bool matches =
        SUCCEEDED(result) && name &&
        (id == result_id ? Parse(value, status) : value == (id ? mode_status : title));
    SysFreeString(name);
    if (!matches)
      return false;
  }
  return true;
}
bool RuntimeStatus(HWND root, bool require_ready) {
  HWND status = GetDlgItem(root, keyrill::runtime::status_id);
  wchar_t type[16]{};
  ComPtr<IUIAutomation> automation;
  ComPtr<IUIAutomationElement> element;
  if (!status || !GetClassNameW(status, type, 16) || _wcsicmp(type, L"STATIC") ||
      FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&automation))) ||
      FAILED(automation->ElementFromHandle(status, &element)))
    return false;
  BSTR name = nullptr;
  const HRESULT result = element->get_CurrentName(&name);
  keyrill::runtime::Snapshot value;
  const bool parsed = result == S_OK && name &&
                      keyrill::runtime::Parse(std::wstring_view(name, SysStringLen(name)), value);
  if (parsed && !require_ready)
    wprintf(L"%ls\n", name);
  SysFreeString(name);
  if (!parsed || !require_ready)
    return parsed;
  const auto ready = [](keyrill::vocabulary::Status source) {
    return source == keyrill::vocabulary::Status::Ready ||
           source == keyrill::vocabulary::Status::Retained;
  };
  keyrill::diagnostics::PublicationV2 service;
  return value.requested == KR_POLICY_MODEL && value.effective == KR_POLICY_MODEL &&
         value.model_ready && ready(value.sources[0]) && ready(value.sources[1]) &&
         ReadServicePublication(root, service) &&
         service.availability == keyrill::diagnostics::Availability::Available &&
         !service.saturated && !service.snapshot.base.saturated && service.snapshot.base.active &&
         (service.snapshot.base.mode == KR_EN || service.snapshot.base.mode == KR_RU) &&
         !service.snapshot.base.stopped && service.snapshot.base.activation == value.activation;
}
bool SourceActivated(HWND root) {
  keyrill::source_proof::Status result, repeated;
  keyrill::diagnostics::PublicationV2 first, second;
  if (!ReadSourceResult(root, result) || result.stage != keyrill::source_proof::Stage::Verified ||
      !ReadServicePublication(root, first) || !Check(GetDlgItem(root, 101), L"ghbdtn", true) ||
      !Check(GetDlgItem(root, 102), L"", false))
    return false;
  Sleep(600);
  return ReadServicePublication(root, second) &&
         keyrill::source_proof::Matches(result, first, second) &&
         Check(GetDlgItem(root, 101), L"ghbdtn", true) &&
         Check(GetDlgItem(root, 102), L"", false) && ReadSourceResult(root, repeated) &&
         repeated == result;
}
bool ShutdownReady(HWND root) {
  using namespace keyrill::diagnostics;
  PublicationV2 first, second;
  if (!ShutdownMode(root) || !ReadServicePublication(root, first) ||
      first.availability != Availability::Available || first.saturated ||
      keyrill::shutdown::detail::Capturable(first.snapshot) != keyrill::shutdown::Verdict::Verified)
    return false;
  const auto mode = first.snapshot.base.mode;
  const wchar_t *expected = mode == KR_EN   ? L"ghbdtn"
                            : mode == KR_RU ? L"\x0440\x0443\x0434\x0434\x0449"
                                            : nullptr;
  return expected && Check(GetDlgItem(root, 101), expected, true) &&
         Check(GetDlgItem(root, 102), L"", false) && ReadServicePublication(root, second) &&
         second.availability == Availability::Available && !second.saturated &&
         keyrill::shutdown::detail::Capturable(second.snapshot) ==
             keyrill::shutdown::Verdict::Verified &&
         second.source == first.source &&
         second.snapshot.base.activation == first.snapshot.base.activation &&
         second.snapshot.base.mode == mode &&
         second.snapshot.lifecycle.owned_serial == first.snapshot.lifecycle.owned_serial &&
         ShutdownMode(root);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  if (argc != 3) {
    fwprintf(stderr,
             L"Usage: KeyrillProofInspect FIXTURE_PID "
             L"correction|reversal|continuation|focus|empty|routing|service|denial|"
             L"shutdown-ready|source-activated|runtime|model-ready|manual|manual-reversed|"
             L"manual-provisional|manual-pc|model|model-ru|model-reversed|model-ru-reversed|"
             L"vocabulary\n");
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
  if (wcscmp(argv[2], L"manual") == 0)
    expected = L"привет";
  if (wcscmp(argv[2], L"manual-reversed") == 0)
    expected = L"ghbdtn";
  if (wcscmp(argv[2], L"manual-provisional") == 0)
    expected = L"хорошо";
  if (wcscmp(argv[2], L"manual-pc") == 0)
    expected = L"ёлка";
  if (wcscmp(argv[2], L"model") == 0)
    expected = L"список ";
  if (wcscmp(argv[2], L"model-ru") == 0)
    expected = L"world ";
  if (wcscmp(argv[2], L"model-reversed") == 0 || wcscmp(argv[2], L"vocabulary") == 0)
    expected = L"cgbcjr ";
  if (wcscmp(argv[2], L"model-ru-reversed") == 0)
    expected = L"цщкдв ";
  const bool runtime = wcscmp(argv[2], L"runtime") == 0;
  const bool model_ready = wcscmp(argv[2], L"model-ready") == 0;
  bool routing = wcscmp(argv[2], L"routing") == 0;
  bool service = wcscmp(argv[2], L"service") == 0;
  bool denial = wcscmp(argv[2], L"denial") == 0;
  bool shutdown = wcscmp(argv[2], L"shutdown-ready") == 0;
  bool source = wcscmp(argv[2], L"source-activated") == 0;
  if (!expected && !routing && !service && !denial && !shutdown && !source && !runtime &&
      !model_ready)
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
  if (runtime || model_ready) {
    const bool matched = RuntimeStatus(search.window, model_ready);
    CoUninitialize();
    if (model_ready || !matched)
      puts(matched ? "Fixture model and vocabulary are ready. No text recorded."
                   : "Fixture runtime evidence unavailable or not ready. No text recorded.");
    return matched ? 0 : 1;
  }
  if (source) {
    const bool matched = SourceActivated(search.window);
    CoUninitialize();
    puts(matched ? "Fixture native source and no-Space UIA oracle matched. No text recorded."
                 : "Fixture native source or no-Space UIA oracle mismatch. No text recorded.");
    return matched ? 0 : 1;
  }
  if (shutdown) {
    const bool matched = ShutdownReady(search.window);
    CoUninitialize();
    puts(matched
             ? "Fixture owned shutdown ready oracle matched. No text recorded."
             : "Fixture owned shutdown ready mismatch or unavailable evidence. No text recorded.");
    return matched ? 0 : 1;
  }
  if (denial) {
    bool matched = NativeDenial(search.window);
    CoUninitialize();
    puts(matched ? "Fixture native denial oracle matched. No text recorded."
                 : "Fixture native denial mismatch or unavailable evidence. No text recorded.");
    return matched ? 0 : 1;
  }
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
