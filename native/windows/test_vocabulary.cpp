#include "test_vocabulary.h"
#include "rust/worker_test_api.h"
#include "test_service.h"
#include <atomic>

namespace keyrill::vocabulary {
namespace {
struct HookOwner {
  std::atomic<uint32_t> references{1};
  Hooks value;
  static void WINAPI Retain(void *context) { ++static_cast<HookOwner *>(context)->references; }
  static void WINAPI Release(void *context) {
    auto *owner = static_cast<HookOwner *>(context);
    if (!--owner->references)
      delete owner;
  }
  static void Call(void *context, uint32_t boundary, HANDLE file, size_t index) {
    static_cast<HookOwner *>(context)->value.At(static_cast<Boundary>(boundary), file, index);
  }
};
struct HookTable {
  HookOwner *owner;
  KrWorkerHooks value;
  explicit HookTable(const Hooks &hooks) : owner(new HookOwner{{1}, hooks}), value{} {
    value.call = HookOwner::Call;
    value.context = owner;
    value.retain_context = HookOwner::Retain;
    value.release_context = HookOwner::Release;
    value.native = {hooks.native.open,   hooks.native.read,         hooks.native.completion,
                    hooks.native.cancel, hooks.native.event,        hooks.native.pin,
                    hooks.native.submit, hooks.native.long_running, hooks.native.volume_paths};
  }
  ~HookTable() { HookOwner::Release(owner); }
};
std::wstring TextPath(const Path &path) {
  std::wstring text = path.drive;
  for (const auto &component : path.components) {
    text += component;
    text += L'\\';
  }
  if (!path.components.empty())
    text.pop_back();
  return text;
}
} // namespace

std::optional<Path> ParsePath(const std::wstring &text) {
  auto parse = reinterpret_cast<decltype(&KeyrillTestWorkerParsePath)>(
      TestExport("KeyrillTestWorkerParsePath"));
  if (!parse(reinterpret_cast<const uint16_t *>(text.data()), static_cast<uint32_t>(text.size())))
    return std::nullopt;
  Path path{text.substr(0, 3), {}};
  size_t start = 3;
  while (start < text.size()) {
    const size_t end = text.find(L'\\', start);
    path.components.push_back(text.substr(start, end - start));
    if (end == std::wstring::npos)
      break;
    start = end + 1;
  }
  return path;
}

struct State {
  void *handle = nullptr;
  ~State() {
    reinterpret_cast<decltype(&KeyrillTestWorkerFree)>(TestExport("KeyrillTestWorkerFree"))(handle);
  }
};
Worker::~Worker() { Stop(); }
bool Worker::Start(const Model &model, uint32_t activation, ModuleOwner module, Hooks hooks) {
  Stop();
  auto state = std::make_shared<State>();
  state->handle =
      reinterpret_cast<decltype(&KeyrillTestWorkerCreate)>(TestExport("KeyrillTestWorkerCreate"))();
  if (!state->handle)
    return false;
  std::array<std::wstring, 2> paths;
  KrWorkerSource sources[2]{};
  for (size_t index = 0; index < paths.size(); ++index) {
    if (const auto *defaults = std::get_if<DefaultSource>(&model.sources[index])) {
      sources[index].default_source = 1;
      if (defaults->path)
        paths[index] = TextPath(*defaults->path);
    } else {
      paths[index] = TextPath(std::get<ExplicitSource>(model.sources[index]).path);
    }
    sources[index].path = reinterpret_cast<const uint16_t *>(paths[index].data());
    sources[index].length = static_cast<uint32_t>(paths[index].size());
  }
  HookTable transport(hooks);
  auto start =
      reinterpret_cast<decltype(&KeyrillTestWorkerStart)>(TestExport("KeyrillTestWorkerStart"));
  if (!start(state->handle, sources, activation, module == ModuleOwner::Dll, &transport.value))
    return false;
  state_ = std::move(state);
  return true;
}
void Worker::Stop() noexcept {
  if (!state_)
    return;
  reinterpret_cast<decltype(&KeyrillTestWorkerStop)>(TestExport("KeyrillTestWorkerStop"))(
      state_->handle);
  state_.reset();
}
std::optional<Ready> Worker::Poll() {
  if (!state_)
    return std::nullopt;
  KrWorkerReady packet{};
  auto poll =
      reinterpret_cast<decltype(&KeyrillTestWorkerPoll)>(TestExport("KeyrillTestWorkerPoll"));
  if (!poll(state_->handle, &packet))
    return std::nullopt;
  Ready result{};
  result.activation = packet.activation;
  result.revision = packet.revision;
  result.model = packet.model != 0;
  result.error = static_cast<Error>(packet.error);
  const auto release = reinterpret_cast<decltype(&KeyrillTestWorkerDictionaryRelease)>(
      TestExport("KeyrillTestWorkerDictionaryRelease"));
  for (size_t index = 0; index < result.sources.size(); ++index) {
    const auto &source = packet.sources[index];
    result.sources[index].status = static_cast<Status>(source.status);
    result.sources[index].error = static_cast<Error>(source.error);
    if (source.owner)
      result.sources[index].dictionary = Dictionary(
          const_cast<kr_dictionary *>(static_cast<const kr_dictionary *>(source.dictionary)),
          [release, owner = source.owner](kr_dictionary *) { release(owner); });
  }
  return result;
}

BOOL SetTestHooks(HMODULE module, const Hooks &hooks) {
  auto set = reinterpret_cast<BOOL(WINAPI *)(const KrWorkerHooks *)>(
      GetProcAddress(module, "KeyrillTestWorkerHooks"));
  if (!set)
    return FALSE;
  HookTable transport(hooks);
  return set(&transport.value);
}
} // namespace keyrill::vocabulary
