#pragma once
#include "command_observation.h"
#include "host_observation.h"
#include "runtime_diagnostics.h"
#include "service_diagnostics.h"

namespace keyrill::probe {
inline constexpr DWORD protocol = 3;
inline constexpr size_t page_bytes = 4096;
inline constexpr size_t payload_words = 512;
inline constexpr size_t observation_offset = 177;
using Payload = std::array<DWORD, payload_words>;
// Every shared access is one aligned DWORD. MSVC /volatile:ms loads and Windows
// barriers provide the read-only side; interlocked stores provide the writer side.
struct Page {
  volatile LONG header[8];
  volatile LONG allocated;
  volatile LONG claim;
  volatile LONG sequence;
  volatile LONG words[payload_words];
};
static_assert(sizeof(LONG) == 4 && alignof(Page) == 4 && offsetof(Page, words) == 44 &&
              sizeof(Page) <= page_bytes);
struct Snapshot {
  DWORD instance = 0;
  host::Kind kind = host::Kind::Unknown;
  std::array<DWORD, 4> version{};
  std::array<uint32_t, 8> build{};
  host::Metadata metadata;
  diagnostics::SnapshotV2 service;
  runtime::Snapshot runtime;
  diagnostics::ObservationWindow observations;
};
enum class Availability { Available, Missing, Busy, Incompatible, Stale };
bool Name(const host::Identity &, wchar_t (&name)[128]) noexcept;
void Initialize(Page &, const host::Identity &) noexcept;
bool Matches(const Page &, const host::Identity &) noexcept;
Payload Encode(const host::Identity &, const host::Metadata &, const diagnostics::SnapshotV2 &,
               const runtime::Snapshot &, DWORD instance,
               const diagnostics::ObservationWindow &) noexcept;
bool Decode(const Payload &, Snapshot &) noexcept;
bool CompleteInterval(const Snapshot &, const Snapshot &) noexcept;
Availability Read(const Page &, const host::Identity &, Payload &) noexcept;

class Publisher {
public:
  Publisher() = default;
  Publisher(const Publisher &) = delete;
  Publisher &operator=(const Publisher &) = delete;
  ~Publisher() { Close(); }
  void Attach(const host::Identity &) noexcept;
  void Close() noexcept;
  void Publish(const Payload &) noexcept;
  DWORD instance() const noexcept { return instance_; }

private:
  HANDLE mapping_ = nullptr;
  Page *page_ = nullptr;
  DWORD instance_ = 0;
};
class ArmedPage {
public:
  ArmedPage() = default;
  ArmedPage(const ArmedPage &) = delete;
  ArmedPage &operator=(const ArmedPage &) = delete;
  ~ArmedPage();
  bool Create(const host::Identity &) noexcept;
  Availability Read(Payload &) const noexcept;

private:
  host::Identity identity_;
  HANDLE mapping_ = nullptr;
  const Page *page_ = nullptr;
};
} // namespace keyrill::probe
