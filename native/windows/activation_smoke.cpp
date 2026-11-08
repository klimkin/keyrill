#include "profiles.h"
#include <cstdio>
#include <string>

namespace {
std::wstring ModulePath(HMODULE module) {
  wchar_t path[32768]{};
  DWORD length = GetModuleFileNameW(module, path, ARRAYSIZE(path));
  return length && length < ARRAYSIZE(path) ? std::wstring(path, length) : std::wstring();
}

bool SameFile(const std::wstring &left, const std::wstring &right) {
  HANDLE first =
      CreateFileW(left.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE second =
      CreateFileW(right.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  BY_HANDLE_FILE_INFORMATION a{}, b{};
  bool same = first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE &&
              GetFileInformationByHandle(first, &a) && GetFileInformationByHandle(second, &b) &&
              a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
              a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
  if (first != INVALID_HANDLE_VALUE)
    CloseHandle(first);
  if (second != INVALID_HANDLE_VALUE)
    CloseHandle(second);
  return same;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  bool missing = argc == 2 && std::wstring(argv[1]) == L"--expect-missing";
  if ((argc != 1 && !missing) || FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    return 2;

  std::wstring expected = ModulePath(nullptr);
  size_t separator = expected.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    CoUninitialize();
    return 1;
  }
  expected.resize(separator + 1);
  expected += L"KeyrillTsf.dll";

  ACTCTX_SECTION_KEYED_DATA mapping{};
  mapping.cbSize = sizeof(mapping);
  bool mapped = FindActCtxSectionGuid(0, nullptr, ACTIVATION_CONTEXT_SECTION_COM_SERVER_REDIRECTION,
                                      &keyrill::service_id, &mapping) != FALSE;
  DWORD attributes = GetFileAttributesW(expected.c_str());
  bool absent = attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
  ITfTextInputProcessorEx *service = nullptr;
  HRESULT result =
      CoCreateInstance(keyrill::service_id, nullptr, CLSCTX_INPROC_SERVER,
                       IID_ITfTextInputProcessorEx, reinterpret_cast<void **>(&service));
  bool ok = false;
  if (missing) {
    ok = mapped && absent && FAILED(result) && !service && !GetModuleHandleW(L"KeyrillTsf.dll");
  } else if (mapped && SUCCEEDED(result) && service) {
    HMODULE actual = nullptr;
    const void *entry = (*reinterpret_cast<void ***>(service))[0];
    ok = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(entry), &actual) &&
         SameFile(expected, ModulePath(actual));
  }
  if (service)
    service->Release();
  CoUninitialize();
  puts(ok ? (missing ? "Missing adjacent DLL refused COM activation."
                     : "COM activated the adjacent proof DLL. Not live TSF routing.")
          : "Proof COM activation check failed.");
  return ok ? 0 : 1;
}
