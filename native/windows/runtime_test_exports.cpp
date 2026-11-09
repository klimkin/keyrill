#include "vocabulary.h"
extern "C" __declspec(dllexport) void WINAPI
KeyrillTestWorkerHooks(keyrill::vocabulary::Hooks hooks) {
  keyrill::vocabulary::test_hooks = hooks;
}
