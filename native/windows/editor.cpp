#include "fixture.h"
#include <wrl/client.h>

namespace {
LRESULT CALLBACK Window(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_DESTROY) {
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
  HWND window = keyrill::CreateFixture(instance, Window, true);
  if (!window) {
    manager->Deactivate();
    keys.Reset();
    pump.Reset();
    manager.Reset();
    CoUninitialize();
    return 1;
  }
  SetFocus(GetDlgItem(window, 101));
  MSG message{};
  BOOL available = FALSE;
  while (SUCCEEDED(pump->GetMessageW(&message, nullptr, 0, 0, &available)) && available) {
    BOOL interest = FALSE, eaten = FALSE;
    if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) &&
        SUCCEEDED(keys->TestKeyDown(message.wParam, message.lParam, &interest)) && interest &&
        SUCCEEDED(keys->KeyDown(message.wParam, message.lParam, &eaten)) && eaten)
      continue;
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
  manager->Deactivate();
  keys.Reset();
  pump.Reset();
  manager.Reset();
  CoUninitialize();
  return 0;
}
