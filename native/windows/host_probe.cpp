#include "host_probe.h"
#include "host_build.h"
#include <sddl.h>
#include <vector>

namespace keyrill::probe {
namespace {
constexpr LONG magic = 0x4b525031;
DWORD Load(const volatile LONG &value) noexcept { return static_cast<DWORD>(value); }
void Store(volatile LONG &field, DWORD value) noexcept {
  InterlockedExchange(&field, static_cast<LONG>(value));
}
} // namespace
bool Name(const host::Identity &id, wchar_t (&name)[128]) noexcept {
  return swprintf_s(name, L"Local\\KeyrillProbe-v%lu-%lu-%016llx-%lu", protocol, id.process,
                    id.started, id.thread) > 0;
}
void Initialize(Page &page, const host::Identity &id) noexcept {
  const DWORD header[] = {static_cast<DWORD>(magic),
                          protocol,
                          page_bytes,
                          payload_words,
                          id.process,
                          static_cast<DWORD>(id.started),
                          static_cast<DWORD>(id.started >> 32),
                          id.thread};
  Store(page.header[0], 0);
  for (size_t i = 1; i != std::size(header); ++i)
    Store(page.header[i], header[i]);
  Store(page.claim, 0);
  Store(page.allocated, 0);
  Store(page.sequence, 0);
  for (auto &word : page.words)
    Store(word, 0);
  Store(page.header[0], header[0]);
}
bool Matches(const Page &page, const host::Identity &id) noexcept {
  return Load(page.header[0]) == static_cast<DWORD>(magic) && Load(page.header[1]) == protocol &&
         Load(page.header[2]) == page_bytes && Load(page.header[3]) == payload_words &&
         Load(page.header[4]) == id.process &&
         Load(page.header[5]) == static_cast<DWORD>(id.started) &&
         Load(page.header[6]) == static_cast<DWORD>(id.started >> 32) &&
         Load(page.header[7]) == id.thread;
}
Payload Encode(const host::Identity &id, const host::Metadata &metadata,
               const diagnostics::SnapshotV2 &service_v2, const runtime::Snapshot &runtime,
               DWORD instance) noexcept {
  Payload words{};
  const auto &service = service_v2.base;
  size_t index = 0;
  auto add = [&](DWORD word) { words[index++] = word; };
  add(instance);
  add(static_cast<DWORD>(id.kind));
  for (auto part : id.version)
    add(part);
  for (auto part : build_digest)
    add(part);
  add(static_cast<DWORD>(metadata.reason));
  add(metadata.result);
  add(static_cast<DWORD>(metadata.focus));
  add(static_cast<DWORD>(metadata.scope));
  add(metadata.request);
  add(metadata.session);
  add(metadata.entered);
  add(metadata.completed);
  add(metadata.selection);
  add(metadata.acp);
  add(service.activation);
  add(service.active);
  add(service.stopped);
  add(service.mode);
  add(service.saturated);
  add(service.request_present);
  add(service.request_result);
  add(service.session_present);
  add(service.session_result);
  for (auto count : service.counts)
    add(count);
  for (auto count : service.refusals)
    add(count);
  for (auto refusal : {service.first, service.latest}) {
    add(refusal.present);
    add(static_cast<DWORD>(refusal.stage));
    add(static_cast<DWORD>(refusal.cause));
    add(refusal.result);
  }
  const auto &lifecycle = service_v2.lifecycle;
  add(lifecycle.sequence);
  add(lifecycle.allocated);
  add(lifecycle.saturated);
  add(lifecycle.count);
  add(lifecycle.owned_present);
  add(lifecycle.owned_serial);
  for (const auto &event : lifecycle.events) {
    add(event.sequence);
    add(static_cast<DWORD>(event.kind));
    add(event.subject);
    add(static_cast<DWORD>(event.cause));
    add(static_cast<DWORD>(event.unresolved));
  }
  add(runtime.activation);
  add(runtime.requested);
  add(runtime.effective);
  add(runtime.model_ready);
  add(static_cast<DWORD>(runtime.revision));
  add(static_cast<DWORD>(runtime.revision >> 32));
  add(static_cast<DWORD>(runtime.error));
  add(static_cast<DWORD>(runtime.configuration_error));
  for (size_t i = 0; i != runtime.sources.size(); ++i) {
    add(static_cast<DWORD>(runtime.sources[i]));
    add(static_cast<DWORD>(runtime.source_errors[i]));
  }
  return words;
}
bool Decode(const Payload &words, Snapshot &output) noexcept {
  static_assert(14 + 10 + 9 + diagnostics::counter_count + diagnostics::stage_count + 8 + 6 +
                    5 * diagnostics::lifecycle_capacity + 12 <=
                payload_words);
  Snapshot candidate;
  size_t index = 0;
  auto next = [&]() { return words[index++]; };
  candidate.instance = next();
  candidate.kind = static_cast<host::Kind>(next());
  for (auto &part : candidate.version)
    part = next();
  for (auto &part : candidate.build)
    part = next();
  auto &m = candidate.metadata;
  m.reason = static_cast<host::Reason>(next());
  m.result = next();
  m.focus = static_cast<host::Focus>(next());
  m.scope = static_cast<host::Scope>(next());
  m.request = next();
  m.session = next();
  m.entered = next();
  m.completed = next();
  m.selection = next();
  m.acp = next();
  auto &s = candidate.service.base;
  s.activation = next();
  s.active = next();
  s.stopped = next();
  s.mode = next();
  s.saturated = next();
  s.request_present = next();
  s.request_result = next();
  s.session_present = next();
  s.session_result = next();
  for (auto &count : s.counts)
    count = next();
  for (auto &count : s.refusals)
    count = next();
  for (auto *refusal : {&s.first, &s.latest}) {
    refusal->present = next();
    refusal->stage = static_cast<diagnostics::Stage>(next());
    refusal->cause = static_cast<diagnostics::Cause>(next());
    refusal->result = next();
  }
  auto &l = candidate.service.lifecycle;
  l.sequence = next();
  l.allocated = next();
  l.saturated = next();
  l.count = next();
  l.owned_present = next();
  l.owned_serial = next();
  for (auto &event : l.events) {
    event.sequence = next();
    event.kind = static_cast<diagnostics::LifecycleKind>(next());
    event.subject = next();
    event.cause = static_cast<diagnostics::LifecycleCause>(next());
    event.unresolved = static_cast<diagnostics::Unresolved>(next());
  }
  auto &r = candidate.runtime;
  r.activation = next();
  r.requested = next();
  r.effective = next();
  r.model_ready = next();
  const auto low = next();
  r.revision = low | (uint64_t{next()} << 32);
  r.error = static_cast<vocabulary::Error>(next());
  r.configuration_error = static_cast<vocabulary::Error>(next());
  for (size_t i = 0; i != r.sources.size(); ++i) {
    r.sources[i] = static_cast<vocabulary::Status>(next());
    r.source_errors[i] = static_cast<vocabulary::Error>(next());
  }
  if (!candidate.instance || candidate.kind > host::Kind::PackagedNotepad ||
      m.reason > host::Reason::Exception || m.focus > host::Focus::Windowless ||
      m.scope > host::Scope::Ambiguous || m.entered > 1 || m.completed > m.entered ||
      m.selection > 1 || m.acp > m.selection || !diagnostics::Valid(candidate.service) ||
      r.requested > KR_POLICY_MODEL || r.effective > KR_POLICY_MODEL || r.model_ready > 1 ||
      r.error > vocabulary::Error::Cancelled ||
      r.configuration_error > vocabulary::Error::Cancelled)
    return false;
  for (size_t i = 0; i != r.sources.size(); ++i)
    if (r.sources[i] > vocabulary::Status::Retained ||
        r.source_errors[i] > vocabulary::Error::Cancelled)
      return false;
  for (; index != payload_words; ++index)
    if (words[index])
      return false;
  output = candidate;
  return true;
}
Availability Read(const Page &page, const host::Identity &id, Payload &output) noexcept {
  output = {};
  for (int attempt = 0; attempt != 3; ++attempt) {
    if (!Matches(page, id))
      return Availability::Incompatible;
    const auto claim = Load(page.claim);
    const auto before = Load(page.sequence);
    MemoryBarrier();
    if (before >= MAXDWORD - 2)
      return Availability::Incompatible;
    if (!claim || !before)
      return Availability::Missing;
    if (before & 1)
      continue;
    Payload candidate{};
    for (size_t i = 0; i != payload_words; ++i)
      candidate[i] = Load(page.words[i]);
    MemoryBarrier();
    const auto after = Load(page.sequence);
    if (before == after && claim == Load(page.claim) && Matches(page, id) &&
        candidate[0] == claim) {
      Snapshot decoded;
      if (!Decode(candidate, decoded))
        return Availability::Incompatible;
      output = candidate;
      return Availability::Available;
    }
  }
  return Availability::Busy;
}
void Publisher::Attach(const host::Identity &id) noexcept {
  Close();
  wchar_t name[128]{};
  if (!Name(id, name))
    return;
  mapping_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
  if (!mapping_)
    return;
  page_ = static_cast<Page *>(
      MapViewOfFile(mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, page_bytes));
  if (!page_ || !Matches(*page_, id)) {
    Close();
    return;
  }
  const auto allocated = Load(page_->allocated);
  if (allocated >= LONG_MAX ||
      InterlockedCompareExchange(&page_->allocated, static_cast<LONG>(allocated + 1),
                                 static_cast<LONG>(allocated)) != static_cast<LONG>(allocated)) {
    Close();
    return;
  }
  const auto instance = allocated + 1;
  if (InterlockedCompareExchange(&page_->claim, static_cast<LONG>(instance), 0) != 0) {
    Close();
    return;
  }
  instance_ = instance;
}
void Publisher::Close() noexcept {
  if (page_) {
    if (instance_ && Load(page_->claim) == instance_) {
      InterlockedCompareExchange(&page_->claim, 0, static_cast<LONG>(instance_));
    }
    UnmapViewOfFile(page_);
  }
  if (mapping_)
    CloseHandle(mapping_);
  page_ = nullptr;
  mapping_ = nullptr;
  instance_ = 0;
}
void Publisher::Publish(const Payload &words) noexcept {
  if (!page_ || Load(page_->claim) != instance_)
    return;
  const auto sequence = Load(page_->sequence);
  if ((sequence & 1) || sequence >= MAXDWORD - 2)
    return;
  Store(page_->sequence, sequence + 1);
  for (size_t i = 0; i != payload_words; ++i)
    Store(page_->words[i], words[i]);
  Store(page_->sequence, sequence + 2);
}
ArmedPage::~ArmedPage() {
  if (page_)
    UnmapViewOfFile(page_);
  if (mapping_)
    CloseHandle(mapping_);
}
bool ArmedPage::Create(const host::Identity &id) noexcept {
  if (mapping_)
    return false;
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return false;
  DWORD needed = 0;
  GetTokenInformation(token, TokenLogonSid, nullptr, 0, &needed);
  if (!needed || needed > 65536) {
    CloseHandle(token);
    return false;
  }
  try {
    std::vector<BYTE> storage(needed);
    const bool acquired =
        GetTokenInformation(token, TokenLogonSid, storage.data(), needed, &needed) != FALSE;
    CloseHandle(token);
    token = nullptr;
    auto *groups = reinterpret_cast<TOKEN_GROUPS *>(storage.data());
    LPWSTR sid = nullptr;
    if (!acquired || groups->GroupCount != 1 ||
        !ConvertSidToStringSidW(groups->Groups[0].Sid, &sid))
      return false;
    const std::wstring descriptor = std::wstring(L"D:P(A;;GRGW;;;") + sid + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR security = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1,
                                                              &security, nullptr))
      return false;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
    wchar_t name[128]{};
    if (Name(id, name))
      mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0,
                                    page_bytes, name);
    const auto error = GetLastError();
    LocalFree(security);
    if (!mapping_)
      return false;
    if (error == ERROR_ALREADY_EXISTS) {
      CloseHandle(mapping_);
      mapping_ = nullptr;
      return false;
    }
    auto *write = static_cast<Page *>(MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, page_bytes));
    if (!write)
      return false;
    Initialize(*write, id);
    UnmapViewOfFile(write);
    HANDLE read_only = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    CloseHandle(mapping_);
    mapping_ = read_only;
    if (!mapping_)
      return false;
    page_ = static_cast<const Page *>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, page_bytes));
    identity_ = id;
    return page_ != nullptr;
  } catch (...) {
    if (token)
      CloseHandle(token);
    return false;
  }
}
Availability ArmedPage::Read(Payload &output) const noexcept {
  return page_ ? probe::Read(*page_, identity_, output) : Availability::Missing;
}
} // namespace keyrill::probe
