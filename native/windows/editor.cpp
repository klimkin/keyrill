#include "denial_fixture.h"
#include "editor_window.h"
#include "fixture_mode.h"
#include "routing.h"
#include "service_diagnostics.h"
#include <optional>
#include <shellapi.h>
#include <wrl/client.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return 2;
  const auto startup = keyrill::fixture::ParseMode(argc, argv);
  LocalFree(argv);
  if (startup == keyrill::fixture::Mode::Invalid)
    return 2;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    return 1;
  Microsoft::WRL::ComPtr<ITfThreadMgr> manager;
  TfClientId client = TF_CLIENTID_NULL;
  HRESULT result =
      CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
  if (FAILED(result) || FAILED(manager->Activate(&client))) {
    manager.Reset();
    CoUninitialize();
    return 1;
  }
  Microsoft::WRL::ComPtr<ITfKeystrokeMgr> keys;
  Microsoft::WRL::ComPtr<ITfMessagePump> pump;
  if (FAILED(manager.As(&keys)) || FAILED(manager.As(&pump))) {
    manager->Deactivate();
    keys.Reset();
    pump.Reset();
    manager.Reset();
    CoUninitialize();
    return 1;
  }
  keyrill::routing::Snapshot routing;
  keyrill::diagnostics::Poller service;
  std::optional<keyrill::shutdown::Probe> shutdown;
  std::optional<keyrill::source_proof::Probe> source;
  if (startup == keyrill::fixture::Mode::ProveOwnedShutdown)
    shutdown.emplace();
  keyrill::editor::WindowContext context{manager.Get(), shutdown ? &*shutdown : nullptr};
  wchar_t initial[keyrill::routing::status_capacity]{};
  HWND window = keyrill::CreateFixture(instance, keyrill::editor::Window, false);
  HWND status = nullptr;
  HWND service_status = nullptr;
  HWND denial_status = nullptr;
  HWND shutdown_status = nullptr;
  HWND source_mode = nullptr;
  HWND source_status = nullptr;
  const bool deny_locks = startup == keyrill::fixture::Mode::DenyTsfLocks;
  bool attached = false;
  if (window) {
    SetLastError(0);
    attached =
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&context)) != 0 ||
        GetLastError() == 0;
  }
  constexpr UINT_PTR status_timer = 1;
  if (window && attached && keyrill::routing::Format(routing, initial) &&
      SetWindowPos(window, nullptr, 0, 0, 1040, 1060, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
    if (startup == keyrill::fixture::Mode::ProveOwnedEnToRu) {
      source.emplace(window, *manager.Get(), GetCurrentThreadId());
      context.source = &*source;
      if (SetWindowTextW(window, keyrill::source_proof::title)) {
        source_mode = CreateWindowW(
            L"STATIC", keyrill::source_proof::mode_status, WS_CHILD | WS_VISIBLE, 16, 552, 990, 24,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(keyrill::source_proof::mode_id)),
            instance, nullptr);
        wchar_t value[keyrill::source_proof::status_capacity]{};
        if (keyrill::source_proof::Format({}, value))
          source_status = CreateWindowW(
              L"STATIC", value, WS_CHILD | WS_VISIBLE, 16, 580, 990, 96, window,
              reinterpret_cast<HMENU>(static_cast<INT_PTR>(keyrill::source_proof::result_id)),
              instance, nullptr);
      }
    }
    if (deny_locks) {
      HWND field = GetDlgItem(window, 101);
      if (field) {
        const auto implemented = SendMessageW(field, EM_SETEDITSTYLE, SES_CTFNOLOCK, SES_CTFNOLOCK);
        const auto observed = SendMessageW(field, EM_GETEDITSTYLE, 0, 0);
        if (implemented == observed && (observed & SES_CTFNOLOCK) &&
            SetWindowTextW(window, keyrill::denial::title))
          denial_status = CreateWindowW(
              L"STATIC", keyrill::denial::setup_status, WS_CHILD | WS_VISIBLE, 16, 552, 990, 24,
              window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(keyrill::denial::setup_id)),
              instance, nullptr);
      }
    }
    if (shutdown && SetWindowTextW(window, keyrill::shutdown::title))
      shutdown_status = CreateWindowW(
          L"STATIC", keyrill::shutdown::mode_status, WS_CHILD | WS_VISIBLE, 16, 552, 990, 24,
          window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(keyrill::shutdown::mode_id)),
          instance, nullptr);
    const bool mode_row = deny_locks || shutdown.has_value() || source.has_value();
    status = CreateWindowW(L"STATIC", initial, WS_CHILD | WS_VISIBLE, 16,
                           source     ? 680
                           : mode_row ? 580
                                      : 552,
                           720, mode_row ? 96 : 120, window, reinterpret_cast<HMENU>(105), instance,
                           nullptr);
    wchar_t service_initial[keyrill::diagnostics::status_capacity]{};
    if (keyrill::diagnostics::Format(service.value, service_initial))
      service_status = CreateWindowW(L"STATIC", service_initial, WS_CHILD | WS_VISIBLE, 16,
                                     source ? 780 : 680, 990, source ? 220 : 320, window,
                                     reinterpret_cast<HMENU>(106), instance, nullptr);
  }
  if (!status || !service_status || (deny_locks && !denial_status) ||
      (shutdown && !shutdown_status) || (source && (!source_mode || !source_status)) ||
      !SetTimer(window, status_timer, 250, nullptr)) {
    if (window)
      DestroyWindow(window);
    const HRESULT deactivated = manager->Deactivate();
    if (shutdown)
      shutdown->Finish(deactivated);
    source.reset();
    keys.Reset();
    pump.Reset();
    manager.Reset();
    CoUninitialize();
    return 1;
  }
  ShowWindow(window, SW_SHOW);
  SetFocus(GetDlgItem(window, 101));
  MSG message{};
  BOOL available = FALSE;
  HRESULT pumped = S_OK;
  while (SUCCEEDED(pumped = pump->GetMessageW(&message, nullptr, 0, 0, &available)) && available) {
    if (message.hwnd == window && message.message == WM_TIMER && message.wParam == status_timer) {
      if (source) {
        const auto outcome = source->Tick(GetTickCount64());
        wchar_t source_published[keyrill::source_proof::status_capacity]{};
        if (keyrill::source_proof::Format(outcome, source_published))
          SetWindowTextW(source_status, source_published);
      }
      keyrill::routing::Increment(routing.publication);
      wchar_t published[keyrill::routing::status_capacity]{};
      if (keyrill::routing::Format(routing, published))
        SetWindowTextW(status, published);
      service.Poll(manager.Get());
      if (shutdown)
        shutdown->Observe(manager.Get());
      wchar_t service_published[keyrill::diagnostics::status_capacity]{};
      if (keyrill::diagnostics::Format(service.value, service_published))
        SetWindowTextW(service_status, service_published);
      else
        SetWindowTextW(service_status, L"Keyrill service unavailable");
      continue;
    }
    if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
      const auto route = keyrill::routing::RouteDown(
          [&](BOOL *interest) {
            return keys->TestKeyDown(message.wParam, message.lParam, interest);
          },
          [&](BOOL *eaten) { return keys->KeyDown(message.wParam, message.lParam, eaten); });
      keyrill::routing::Record(routing, route);
      if (keyrill::routing::Consumed(route))
        continue;
    }
    BOOL interest = FALSE, eaten = FALSE;
    if ((message.message == WM_KEYUP || message.message == WM_SYSKEYUP) &&
        SUCCEEDED(keys->TestKeyUp(message.wParam, message.lParam, &interest)) && interest &&
        SUCCEEDED(keys->KeyUp(message.wParam, message.lParam, &eaten)) && eaten)
      continue;
    if (message.message == WM_KEYDOWN && message.wParam == VK_TAB) {
      HWND next = GetNextDlgTabItem(window, GetFocus(), GetKeyState(VK_SHIFT) < 0);
      if (next)
        SetFocus(next);
      continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  KillTimer(window, status_timer);
  if ((shutdown || source) && !context.destroyed && IsWindow(window))
    DestroyWindow(window);
  if (source) {
    source->Close();
    context.source = nullptr;
    source.reset();
  }
  service.Reset();
  const HRESULT deactivated = manager->Deactivate();
  int exit_code = 0;
  if (shutdown) {
    const auto verdict = shutdown->Finish(deactivated);
    exit_code = pumped == S_OK && !available && message.message == WM_QUIT && context.destroyed
                    ? keyrill::shutdown::ExitCode(verdict)
                    : 1;
  }
  keys.Reset();
  pump.Reset();
  manager.Reset();
  CoUninitialize();
  return exit_code;
}
