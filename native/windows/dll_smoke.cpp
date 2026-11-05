#include "profiles.h"
#include <cstdio>

int wmain(int argc, wchar_t **argv) {
  if (argc != 2 || FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    return 2;
  HMODULE dll = LoadLibraryW(argv[1]);
  if (!dll) {
    CoUninitialize();
    return 1;
  }
  auto get = reinterpret_cast<HRESULT(WINAPI *)(REFCLSID, REFIID, void **)>(
      GetProcAddress(dll, "DllGetClassObject"));
  auto unload = reinterpret_cast<HRESULT(WINAPI *)()>(GetProcAddress(dll, "DllCanUnloadNow"));
  IClassFactory *factory = nullptr;
  ITfTextInputProcessorEx *service = nullptr;
  bool ok =
      get && unload && unload() == S_OK &&
      SUCCEEDED(get(keyrill::service_id, IID_IClassFactory, reinterpret_cast<void **>(&factory))) &&
      factory && unload() == S_FALSE &&
      SUCCEEDED(factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
                                        reinterpret_cast<void **>(&service))) &&
      service;
  if (service)
    service->Release();
  if (factory)
    factory->Release();
  ok = ok && unload() == S_OK;
  FreeLibrary(dll);
  CoUninitialize();
  puts(ok ? "Registration-free COM factory and real Rust linkage passed. Not live TSF routing."
          : "Registration-free COM factory failed.");
  return ok ? 0 : 1;
}
