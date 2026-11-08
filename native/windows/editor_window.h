#pragma once
#include "fixture.h"
#include "shutdown_probe.h"

namespace keyrill::editor {
struct WindowContext {
  ITfThreadMgr *manager;
  shutdown::Probe *shutdown;
  bool destroyed = false;
};
inline LRESULT CALLBACK Window(HWND window, UINT message, WPARAM w, LPARAM l) {
  auto *context = reinterpret_cast<WindowContext *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_CLOSE && context && context->shutdown)
    context->shutdown->CaptureClose(context->manager);
  if (message == WM_DESTROY) {
    ResetFixtureInputScopes(window);
    if (context)
      context->destroyed = true;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
} // namespace keyrill::editor
