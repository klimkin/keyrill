#include "fixture.h"
#include "routing.h"
#include "service_diagnostics.h"
#include <wrl/client.h>

namespace {
LRESULT CALLBACK Window(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_DESTROY) {
    keyrill::ResetFixtureInputScopes(window);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
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
  wchar_t initial[keyrill::routing::status_capacity]{};
  HWND window = keyrill::CreateFixture(instance, Window, false);
  HWND status = nullptr;
  HWND service_status = nullptr;
  constexpr UINT_PTR status_timer = 1;
  if (window && keyrill::routing::Format(routing, initial) &&
      SetWindowPos(window, nullptr, 0, 0, 1040, 1060, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
    status = CreateWindowW(L"STATIC", initial, WS_CHILD | WS_VISIBLE, 16, 552, 720, 120, window,
                           reinterpret_cast<HMENU>(105), instance, nullptr);
    wchar_t service_initial[keyrill::diagnostics::status_capacity]{};
    if (keyrill::diagnostics::Format(service.value, service_initial))
      service_status =
          CreateWindowW(L"STATIC", service_initial, WS_CHILD | WS_VISIBLE, 16, 680, 990, 320,
                        window, reinterpret_cast<HMENU>(106), instance, nullptr);
  }
  if (!status || !service_status || !SetTimer(window, status_timer, 250, nullptr)) {
    if (window)
      DestroyWindow(window);
    manager->Deactivate();
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
  while (SUCCEEDED(pump->GetMessageW(&message, nullptr, 0, 0, &available)) && available) {
    if (message.hwnd == window && message.message == WM_TIMER && message.wParam == status_timer) {
      keyrill::routing::Increment(routing.publication);
      wchar_t published[keyrill::routing::status_capacity]{};
      if (keyrill::routing::Format(routing, published))
        SetWindowTextW(status, published);
      service.Poll(manager.Get());
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
  service.Reset();
  manager->Deactivate();
  keys.Reset();
  pump.Reset();
  manager.Reset();
  CoUninitialize();
  return 0;
}
