#pragma once
#include "keyrill.h"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <windows.h>
#include <winternl.h>

namespace keyrill::vocabulary {
enum class Error : uint32_t {
  None,
  Configuration,
  Unavailable,
  Missing,
  Unstable,
  Read,
  Parse,
  Model,
  Startup,
  Cancelled
};
enum class Status : uint32_t { Missing, Pending, Ready, Retained };
struct Path {
  std::wstring drive;
  std::vector<std::wstring> components;
  bool operator==(const Path &) const = default;
};
std::optional<Path> ParsePath(const std::wstring &);
struct DefaultSource {
  std::optional<Path> path;
};
struct ExplicitSource {
  Path path;
};
using Source = std::variant<DefaultSource, ExplicitSource>;
struct Model {
  std::array<Source, 2> sources;
};

using Dictionary = std::shared_ptr<kr_dictionary>;
struct SourceReady {
  Status status = Status::Missing;
  Error error = Error::None;
  Dictionary dictionary;
};
struct Ready {
  uint32_t activation = 0;
  uint64_t revision = 0;
  bool model = false;
  Error error = Error::None;
  std::array<SourceReady, 2> sources;
};

enum class ModuleOwner { Dll, Process };
struct NativeCalls {
  decltype(&NtCreateFile) open = reinterpret_cast<decltype(&NtCreateFile)>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
  decltype(&ReadFile) read = ReadFile;
  decltype(&GetOverlappedResult) completion = GetOverlappedResult;
  decltype(&CancelIoEx) cancel = CancelIoEx;
  decltype(&CreateEventW) event = CreateEventW;
  decltype(&GetModuleHandleExW) pin = GetModuleHandleExW;
  decltype(&TrySubmitThreadpoolCallback) submit = TrySubmitThreadpoolCallback;
  decltype(&CallbackMayRunLong) long_running = CallbackMayRunLong;
  decltype(&GetVolumePathNamesForVolumeNameW) volume_paths = GetVolumePathNamesForVolumeNameW;
};
enum class Boundary {
  BeforeWork,
  ParentOpened,
  BeforeRead,
  AfterRead,
  BeforeParse,
  BeforePublish,
  Published,
  Cleanup,
  AfterCount
};
struct Hooks {
  void (*call)(void *, Boundary, HANDLE, size_t) = nullptr;
  void *context = nullptr;
  NativeCalls native;
  void At(Boundary at, HANDLE handle = nullptr, size_t index = 0) const {
    if (call)
      call(context, at, handle, index);
  }
};
struct State;
class Worker {
public:
  Worker() = default;
  ~Worker();
  Worker(const Worker &) = delete;
  Worker &operator=(const Worker &) = delete;
  bool Start(const Model &, uint32_t activation, ModuleOwner, Hooks = {});
  void Stop() noexcept;
  std::optional<Ready> Poll();

private:
  std::shared_ptr<State> state_;
};
} // namespace keyrill::vocabulary
