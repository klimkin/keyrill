#include "vocabulary.h"
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <limits>
#include <mutex>
#include <utility>
#include <winternl.h>

namespace keyrill::vocabulary {
long worker_count = 0;
#ifdef KEYRILL_RUNTIME_TEST
Hooks test_hooks;
#endif
namespace {
constexpr size_t max_bytes = 1024 * 1024;
struct Handle {
  HANDLE value = nullptr;
  Handle() = default;
  explicit Handle(HANDLE h) : value(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
  Handle(Handle &&other) noexcept : value(std::exchange(other.value, nullptr)) {}
  Handle &operator=(Handle &&other) noexcept {
    if (this != &other) {
      if (value)
        CloseHandle(value);
      value = std::exchange(other.value, nullptr);
    }
    return *this;
  }
  ~Handle() {
    if (value)
      CloseHandle(value);
  }
};
std::optional<std::wstring> Environment(const wchar_t *name) {
  wchar_t buffer[4096]{};
  SetLastError(ERROR_SUCCESS);
  DWORD n = GetEnvironmentVariableW(name, buffer, 4096);
  if (!n && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
    return std::nullopt;
  if (!n || n >= 4096)
    throw Error::Configuration;
  return std::wstring(buffer, n);
}
bool DeviceName(std::wstring name) {
  name = name.substr(0, name.find(L'.'));
  std::transform(name.begin(), name.end(), name.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
  if (name == L"CON" || name == L"PRN" || name == L"AUX" || name == L"NUL" || name == L"CONIN$" ||
      name == L"CONOUT$")
    return true;
  return name.size() == 4 && (name.substr(0, 3) == L"COM" || name.substr(0, 3) == L"LPT") &&
         ((name[3] >= L'0' && name[3] <= L'9') || name[3] == L'¹' || name[3] == L'²' ||
          name[3] == L'³');
}
std::optional<std::wstring> Volume(const std::wstring &drive, const NativeCalls &calls) {
  wchar_t guid[64]{};
  HANDLE enumeration = FindFirstVolumeW(guid, 64);
  if (enumeration == INVALID_HANDLE_VALUE)
    return std::nullopt;
  struct Close {
    HANDLE h;
    ~Close() { FindVolumeClose(h); }
  } close{enumeration};
  std::optional<std::wstring> selected;
  for (size_t count = 0; count < 1024; ++count) {
    wchar_t paths[32768]{};
    DWORD length = 0;
    if (!calls.volume_paths(guid, paths, 32768, &length) || length > 32768)
      return std::nullopt;
    for (const wchar_t *p = paths; *p; p += wcslen(p) + 1) {
      if (_wcsicmp(p, drive.c_str()) == 0) {
        if (selected)
          return std::nullopt;
        selected = guid;
      }
    }
    if (!FindNextVolumeW(enumeration, guid, 64))
      return GetLastError() == ERROR_NO_MORE_FILES ? selected : std::nullopt;
  }
  return std::nullopt;
}
struct Metadata {
  FILE_ID_INFO id{};
  FILE_BASIC_INFO basic{};
  FILE_STANDARD_INFO standard{};
};
bool SameId(const Metadata &a, const Metadata &b) {
  return a.id.VolumeSerialNumber == b.id.VolumeSerialNumber &&
         memcmp(a.id.FileId.Identifier, b.id.FileId.Identifier, sizeof(a.id.FileId.Identifier)) ==
             0;
}
bool SameFile(const Metadata &a, const Metadata &b) {
  return SameId(a, b) && a.standard.EndOfFile.QuadPart == b.standard.EndOfFile.QuadPart &&
         a.basic.CreationTime.QuadPart == b.basic.CreationTime.QuadPart &&
         a.basic.LastWriteTime.QuadPart == b.basic.LastWriteTime.QuadPart &&
         a.basic.ChangeTime.QuadPart == b.basic.ChangeTime.QuadPart &&
         a.basic.FileAttributes == b.basic.FileAttributes;
}
Metadata Inspect(HANDLE h, bool directory, std::optional<ULONGLONG> volume = {}) {
  Metadata m;
  FILE_ATTRIBUTE_TAG_INFO tag{};
  constexpr DWORD prohibited = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
                               FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
  if (GetFileType(h) != FILE_TYPE_DISK ||
      !GetFileInformationByHandleEx(h, FileIdInfo, &m.id, sizeof(m.id)) ||
      !GetFileInformationByHandleEx(h, FileBasicInfo, &m.basic, sizeof(m.basic)) ||
      !GetFileInformationByHandleEx(h, FileStandardInfo, &m.standard, sizeof(m.standard)) ||
      !GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) ||
      m.standard.DeletePending || (m.standard.Directory != FALSE) != directory ||
      (tag.FileAttributes & prohibited) || (m.basic.FileAttributes & prohibited) ||
      tag.ReparseTag || (volume && *volume != m.id.VolumeSerialNumber))
    throw Error::Unavailable;
  return m;
}
Handle Open(HANDLE parent, const std::wstring &name, bool directory, Error missing,
            const NativeCalls &calls) {
  auto api = calls.open;
  if (!api || name.size() > 32760)
    throw Error::Unavailable;
  UNICODE_STRING text{};
  text.Buffer = const_cast<wchar_t *>(name.data());
  text.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
  text.MaximumLength = text.Length;
  OBJECT_ATTRIBUTES attributes{};
  attributes.Length = sizeof(attributes);
  attributes.RootDirectory = parent;
  attributes.ObjectName = &text;
  attributes.Attributes = OBJ_CASE_INSENSITIVE;
  IO_STATUS_BLOCK io{};
  HANDLE raw = nullptr;
  auto status =
      api(&raw, FILE_READ_ATTRIBUTES | SYNCHRONIZE | (directory ? FILE_TRAVERSE : FILE_READ_DATA),
          &attributes, &io, nullptr, 0,
          FILE_SHARE_READ | FILE_SHARE_WRITE | (directory ? 0 : FILE_SHARE_DELETE), FILE_OPEN,
          FILE_OPEN_REPARSE_POINT |
              (directory ? FILE_SYNCHRONOUS_IO_NONALERT : FILE_NON_DIRECTORY_FILE),
          nullptr, 0);
  Handle result(raw);
  if (status == 0x103) {
    // An incomplete native open still owns the IOSB and name storage on this worker.
    while (!result.value || WaitForSingleObject(result.value, INFINITE) != WAIT_OBJECT_0)
      Sleep(1000);
    status = io.Status;
  }
  if (status == static_cast<NTSTATUS>(0xc0000034L) || status == static_cast<NTSTATUS>(0xc000003aL))
    throw missing;
  if (status < 0 || !result.value)
    throw Error::Unavailable;
  return result;
}
struct Observation {
  Metadata metadata;
  std::vector<uint8_t> bytes;
};
struct Parent {
  Handle handle;
  Metadata metadata;
};
void CheckStop(HANDLE stop) {
  if (WaitForSingleObject(stop, 0) != WAIT_TIMEOUT)
    throw Error::Cancelled;
}
void Read(HANDLE file, HANDLE stop, std::vector<uint8_t> &bytes, size_t length,
          const NativeCalls &calls) {
  Handle event(calls.event(nullptr, TRUE, FALSE, nullptr));
  if (!event.value)
    throw Error::Read;
  bytes.resize(length + 1);
  size_t offset = 0;
  const ULONGLONG deadline = GetTickCount64() + 2000;
  while (offset <= length) {
    CheckStop(stop);
    OVERLAPPED operation{};
    operation.hEvent = event.value;
    operation.Offset = static_cast<DWORD>(offset);
    ResetEvent(event.value);
    DWORD read = 0;
    DWORD count = static_cast<DWORD>((std::min)(bytes.size() - offset, size_t{65536}));
    BOOL ok = calls.read(file, bytes.data() + offset, count, nullptr, &operation);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && error == ERROR_IO_PENDING) {
      HANDLE waits[]{stop, event.value};
      ULONGLONG now = GetTickCount64();
      DWORD waited = WaitForMultipleObjects(
          2, waits, FALSE, now < deadline ? static_cast<DWORD>(deadline - now) : 0);
      if (waited != WAIT_OBJECT_0 + 1) {
        calls.cancel(file, &operation);
        // Cancellation is a request. Storage remains alive through completion.
        calls.completion(file, &operation, &read, TRUE);
        throw waited == WAIT_OBJECT_0 ? Error::Cancelled : Error::Read;
      }
    }
    if (ok || error == ERROR_IO_PENDING) {
      ok = calls.completion(file, &operation, &read, FALSE);
      error = ok ? ERROR_SUCCESS : GetLastError();
    }
    if (!ok && error != ERROR_HANDLE_EOF)
      throw Error::Read;
    if (!read) {
      if (offset != length)
        throw Error::Unstable;
      bytes.resize(length);
      return;
    }
    offset += read;
    if (offset > length)
      throw Error::Unstable;
  }
}
Observation Observe(const Path &path, HANDLE stop, const Hooks &hooks) {
  CheckStop(stop);
  auto volume = Volume(path.drive, hooks.native);
  if (!volume || GetDriveTypeW(volume->c_str()) != DRIVE_FIXED)
    throw Error::Unavailable;
  std::wstring native = L"\\??\\" + volume->substr(4);
  std::vector<Parent> parents;
  auto root = Open(nullptr, native, true, Error::Unavailable, hooks.native);
  auto root_meta = Inspect(root.value, true);
  wchar_t fs[32]{};
  if (!GetVolumeInformationByHandleW(root.value, nullptr, 0, nullptr, nullptr, nullptr, fs, 32) ||
      _wcsicmp(fs, L"NTFS") != 0)
    throw Error::Unavailable;
  parents.push_back({std::move(root), root_meta});
  const auto revalidate = [&] {
    for (auto &parent : parents)
      if (!SameId(parent.metadata,
                  Inspect(parent.handle.value, true, root_meta.id.VolumeSerialNumber)))
        throw Error::Unstable;
    if (Volume(path.drive, hooks.native) != volume)
      throw Error::Unstable;
  };
  const auto child = [&](const std::wstring &name, bool directory) {
    Inspect(parents.back().handle.value, true, root_meta.id.VolumeSerialNumber);
    try {
      return Open(parents.back().handle.value, name, directory, Error::Missing, hooks.native);
    } catch (Error) {
      revalidate();
      throw;
    }
  };
  for (size_t i = 0; i + 1 < path.components.size(); ++i) {
    hooks.At(Boundary::ParentOpened, parents.back().handle.value, i);
    CheckStop(stop);
    auto parent = child(path.components[i], true);
    auto meta = Inspect(parent.value, true, root_meta.id.VolumeSerialNumber);
    parents.push_back({std::move(parent), meta});
  }
  hooks.At(Boundary::ParentOpened, parents.back().handle.value, path.components.size() - 1);
  auto file = child(path.components.back(), false);
  auto before = Inspect(file.value, false, root_meta.id.VolumeSerialNumber);
  for (auto &parent : parents)
    if (!SameId(parent.metadata,
                Inspect(parent.handle.value, true, root_meta.id.VolumeSerialNumber)))
      throw Error::Unstable;
  if (before.standard.EndOfFile.QuadPart < 0 || before.standard.EndOfFile.QuadPart > max_bytes)
    throw Error::Unavailable;
  hooks.At(Boundary::BeforeRead, file.value);
  Observation result{before, {}};
  Read(file.value, stop, result.bytes, static_cast<size_t>(before.standard.EndOfFile.QuadPart),
       hooks.native);
  hooks.At(Boundary::AfterRead, file.value);
  if (!SameFile(before, Inspect(file.value, false, root_meta.id.VolumeSerialNumber)))
    throw Error::Unstable;
  Handle cursor;
  HANDLE previous = parents.front().handle.value;
  for (size_t i = 0; i < path.components.size(); ++i) {
    bool directory = i + 1 < path.components.size();
    Handle next;
    try {
      next = Open(previous, path.components[i], directory, Error::Unstable, hooks.native);
    } catch (Error) {
      revalidate();
      throw;
    }
    auto meta = Inspect(next.value, directory, root_meta.id.VolumeSerialNumber);
    if (directory ? !SameId(meta, parents[i + 1].metadata) : !SameFile(meta, before))
      throw Error::Unstable;
    cursor = std::move(next);
    previous = cursor.value;
  }
  for (auto &parent : parents)
    if (!SameId(parent.metadata,
                Inspect(parent.handle.value, true, root_meta.id.VolumeSerialNumber)))
      throw Error::Unstable;
  if (Volume(path.drive, hooks.native) != volume)
    throw Error::Unstable;
  CheckStop(stop);
  return result;
}
Dictionary Parse(const std::vector<uint8_t> &bytes) {
  kr_dictionary *raw = nullptr;
  if (kr_dictionary_parse_utf8(KR_ABI_VERSION, bytes.data(), static_cast<uint32_t>(bytes.size()),
                               &raw) != KR_OK)
    throw Error::Parse;
  return Dictionary(raw, kr_dictionary_free);
}
struct Staged {
  Observation observation;
  Dictionary dictionary;
};
struct Published {
  Observation observation;
  Dictionary dictionary;
};
struct SourceState {
  std::optional<Staged> staged;
  std::optional<Published> published;
  Dictionary absent;
  SourceReady ready;
  void Step(const Source &source, HANDLE stop, const Hooks &hooks) {
    try {
      const auto *defaults = std::get_if<DefaultSource>(&source);
      const Path *path = defaults ? (defaults->path ? &*defaults->path : nullptr)
                                  : &std::get<ExplicitSource>(source).path;
      if (!path)
        throw Error::Unavailable;
      Observation observation = Observe(*path, stop, hooks);
      if (published && SameFile(observation.metadata, published->observation.metadata) &&
          observation.bytes == published->observation.bytes) {
        staged.reset();
        ready = {Status::Ready, Error::None, published->dictionary};
      } else if (staged && SameFile(observation.metadata, staged->observation.metadata) &&
                 observation.bytes == staged->observation.bytes) {
        published = Published{std::move(observation), staged->dictionary};
        staged.reset();
        absent.reset();
        ready = {Status::Ready, Error::None, published->dictionary};
      } else {
        staged.reset();
        hooks.At(Boundary::BeforeParse);
        CheckStop(stop);
        auto dictionary = Parse(observation.bytes);
        CheckStop(stop);
        staged = Staged{std::move(observation), std::move(dictionary)};
        ready = {published || absent ? Status::Retained : Status::Pending, Error::None,
                 published ? published->dictionary : absent};
      }
    } catch (Error error) {
      staged.reset();
      if (error == Error::Cancelled)
        throw;
      if (error == Error::Missing && std::holds_alternative<DefaultSource>(source) && !published) {
        if (!absent)
          absent = Parse({});
        ready = {Status::Ready, Error::None, absent};
      } else {
        ready = {published || absent ? Status::Retained : Status::Missing, error,
                 published ? published->dictionary : absent};
      }
    }
  }
};
} // namespace

std::optional<Path> ParsePath(const std::wstring &text) {
  if (text.size() < 4 || text.size() > 4095 ||
      !((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) ||
      text[1] != L':' || text[2] != L'\\')
    return std::nullopt;
  Path path{text.substr(0, 3), {}};
  for (size_t start = 3; start < text.size();) {
    size_t end = text.find(L'\\', start);
    if (end == std::wstring::npos)
      end = text.size();
    std::wstring part = text.substr(start, end - start);
    if (part.empty() || part.size() > 255 || part == L"." || part == L".." || part.back() == L'.' ||
        part.back() == L' ' || DeviceName(part) ||
        std::any_of(part.begin(), part.end(),
                    [](wchar_t c) { return c < 32 || wcschr(L"/:*?\"<>|", c); }) ||
        path.components.size() >= 64)
      return std::nullopt;
    path.components.push_back(std::move(part));
    if (end == text.size())
      break;
    start = end + 1;
    if (start == text.size())
      return std::nullopt;
  }
  return path;
}
std::optional<Config> CaptureConfig() try {
  auto policy = Environment(L"KEYRILL_WINDOWS_POLICY");
  auto english = Environment(L"KEYRILL_WINDOWS_EN_DICTIONARY");
  auto russian = Environment(L"KEYRILL_WINDOWS_RU_DICTIONARY");
  std::optional<Path> en, ru;
  if (english && !(en = ParsePath(*english)))
    return std::nullopt;
  if (russian && !(ru = ParsePath(*russian)))
    return std::nullopt;
  if (!policy || *policy == L"fixture")
    return Fixture{};
  if (*policy == L"off")
    return Off{};
  if (*policy != L"model")
    return std::nullopt;
  auto profile = Environment(L"USERPROFILE");
  auto source = [&](const std::optional<Path> &explicit_path, const wchar_t *language) -> Source {
    if (explicit_path)
      return ExplicitSource{*explicit_path};
    return DefaultSource{
        profile ? ParsePath(*profile + L"\\vimfiles\\spell\\" + language + L".utf-8.add")
                : std::nullopt};
  };
  return Model{{source(en, L"en"), source(ru, L"ru")}};
} catch (...) {
  return std::nullopt;
}
uint32_t Policy(const Config &config) noexcept {
  return std::holds_alternative<Model>(config)     ? KR_POLICY_MODEL
         : std::holds_alternative<Fixture>(config) ? KR_POLICY_FIXTURE
                                                   : KR_POLICY_OFF;
}

struct State {
  Model config;
  uint32_t activation;
  Handle stop;
  std::atomic<bool> revoked{false};
  std::mutex mutex;
  std::optional<Ready> mailbox;
  Hooks hooks;
  State(const Model &m, uint32_t a, Hooks h)
      : config(m), activation(a), stop(h.native.event(nullptr, TRUE, FALSE, nullptr)), hooks(h) {}
  void Publish(Ready ready) {
    hooks.At(Boundary::BeforePublish);
    std::optional<Ready> replaced;
    {
      std::lock_guard lock(mutex);
      if (!revoked.load()) {
        replaced.swap(mailbox);
        mailbox = std::move(ready);
      }
    }
    hooks.At(Boundary::Published);
  }
};
namespace {
struct Launch {
  std::shared_ptr<State> state;
  HMODULE module;
};
void CALLBACK Work(PTP_CALLBACK_INSTANCE instance, void *context) noexcept {
  auto *raw = static_cast<Launch *>(context);
  if (raw->module)
    FreeLibraryWhenCallbackReturns(instance, raw->module);
  const Hooks hooks = raw->state->hooks;
  {
    std::unique_ptr<Launch> launch(raw);
    auto &state = *launch->state;
    Ready ready{state.activation};
    try {
      hooks.At(Boundary::BeforeWork);
      CheckStop(state.stop.value);
      if (!hooks.native.long_running(instance))
        throw Error::Startup;
      if (kr_prepare_model(KR_ABI_VERSION) != KR_OK)
        throw Error::Model;
      CheckStop(state.stop.value);
      std::array<SourceState, 2> sources;
      ready.model = true;
      do {
        if (ready.revision == std::numeric_limits<uint64_t>::max())
          throw Error::Startup;
        ++ready.revision;
        for (size_t i = 0; i < sources.size(); ++i) {
          sources[i].Step(state.config.sources[i], state.stop.value, hooks);
          ready.sources[i] = sources[i].ready;
        }
        CheckStop(state.stop.value);
        state.Publish(ready);
      } while (WaitForSingleObject(state.stop.value, 500) == WAIT_TIMEOUT);
    } catch (Error error) {
      if (error != Error::Cancelled && ready.revision != std::numeric_limits<uint64_t>::max()) {
        try {
          ++ready.revision;
          ready.error = error;
          state.Publish(ready);
        } catch (...) {
        }
      }
    } catch (...) {
      try {
        if (ready.revision != std::numeric_limits<uint64_t>::max()) {
          ++ready.revision;
          ready.error = Error::Startup;
          state.Publish(ready);
        }
      } catch (...) {
      }
    }
    try {
      hooks.At(Boundary::Cleanup);
    } catch (...) {
    }
  }
  InterlockedDecrement(&worker_count);
  try {
    hooks.At(Boundary::AfterCount);
  } catch (...) {
  }
}
} // namespace
Worker::~Worker() { Stop(); }
bool Worker::Start(const Model &model, uint32_t activation, ModuleOwner owner, Hooks hooks) {
  Stop();
#ifdef KEYRILL_RUNTIME_TEST
  if (!hooks.call)
    hooks = test_hooks;
#endif
  try {
    auto state = std::make_shared<State>(model, activation, hooks);
    if (!state->stop.value)
      return false;
    auto launch = std::make_unique<Launch>(Launch{state, nullptr});
    if (owner == ModuleOwner::Dll &&
        !hooks.native.pin(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&Work),
                          &launch->module))
      return false;
    InterlockedIncrement(&worker_count);
    Launch *raw = launch.release();
    if (!hooks.native.submit(Work, raw, nullptr)) {
      HMODULE module = raw->module;
      delete raw;
      InterlockedDecrement(&worker_count);
      if (module)
        FreeLibrary(module);
      return false;
    }
    state_ = std::move(state);
    return true;
  } catch (...) {
    return false;
  }
}
void Worker::Stop() noexcept {
  if (state_) {
    state_->revoked.store(true);
    SetEvent(state_->stop.value);
    state_.reset();
  }
}
std::optional<Ready> Worker::Poll() {
  std::optional<Ready> ready;
  if (state_) {
    std::unique_lock lock(state_->mutex, std::try_to_lock);
    if (lock.owns_lock())
      ready.swap(state_->mailbox);
  }
  return ready;
}
} // namespace keyrill::vocabulary
