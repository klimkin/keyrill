#pragma once
#include "lifecycle_diagnostics.h"
#include "profiles.h"
#include "routing.h"
#include <type_traits>
#include <wrl/client.h>

namespace keyrill::diagnostics {
using Microsoft::WRL::ComPtr;
inline constexpr uint32_t version = 1;
inline constexpr size_t status_capacity = 8192;
#define KR_DIAGNOSTIC_STAGES(X)                                                                    \
  X(Test)                                                                                          \
  X(Key)                                                                                           \
  X(Keyboard)                                                                                      \
  X(Profile)                                                                                       \
  X(Layout)                                                                                        \
  X(Mode)                                                                                          \
  X(Handle)                                                                                        \
  X(Request)                                                                                       \
  X(Session)                                                                                       \
  X(Selection)                                                                                     \
  X(Focus)                                                                                         \
  X(Context)                                                                                       \
  X(ProofWindow)                                                                                   \
  X(Status)                                                                                        \
  X(ContextDisabled)                                                                               \
  X(EmptyContext)                                                                                  \
  X(ThreadDisabled)                                                                                \
  X(AppProperty)                                                                                   \
  X(AppValue)                                                                                      \
  X(ScopeInterface)                                                                                \
  X(Scopes)                                                                                        \
  X(Foreign)                                                                                       \
  X(Boundary)                                                                                      \
  X(Plan)                                                                                          \
  X(Apply)                                                                                         \
  X(Acknowledge)

#define KR_DIAGNOSTIC_CAUSES(X)                                                                    \
  X(None)                                                                                          \
  X(Native)                                                                                        \
  X(Predicate)                                                                                     \
  X(Inactive)                                                                                      \
  X(Stopped)                                                                                       \
  X(ModeUnset)                                                                                     \
  X(Reentry)                                                                                       \
  X(Stale)                                                                                         \
  X(Missing)                                                                                       \
  X(WrongType)                                                                                     \
  X(WrongService)                                                                                  \
  X(NotActive)                                                                                     \
  X(WrongProfile)                                                                                  \
  X(Substitute)                                                                                    \
  X(Exception)

#define KR_DIAGNOSTIC_COUNTERS(X)                                                                  \
  X(TestEntered)                                                                                   \
  X(Interested)                                                                                    \
  X(KeyEntered)                                                                                    \
  X(Consumed)                                                                                      \
  X(HandleEntered)                                                                                 \
  X(Requested)                                                                                     \
  X(SessionEntered)                                                                                \
  X(SessionCompleted)                                                                              \
  X(Attempted)                                                                                     \
  X(Verified)                                                                                      \
  X(Refused)                                                                                       \
  X(Unknown)

#define KR_ENUM(name) name,
enum class Stage : uint32_t { KR_DIAGNOSTIC_STAGES(KR_ENUM) Count };
enum class Cause : uint32_t { KR_DIAGNOSTIC_CAUSES(KR_ENUM) Count };
enum class Counter : uint32_t { KR_DIAGNOSTIC_COUNTERS(KR_ENUM) Count };
#undef KR_ENUM
#define KR_WIDE_LITERAL(value) L##value
#define KR_WIDE(value) KR_WIDE_LITERAL(value)
#define KR_LABEL(name) KR_WIDE(#name),
inline constexpr const wchar_t *stage_labels[] = {KR_DIAGNOSTIC_STAGES(KR_LABEL)};
inline constexpr const wchar_t *cause_labels[] = {KR_DIAGNOSTIC_CAUSES(KR_LABEL)};
inline constexpr const wchar_t *counter_labels[] = {KR_DIAGNOSTIC_COUNTERS(KR_LABEL)};
#undef KR_LABEL
#undef KR_WIDE
#undef KR_WIDE_LITERAL
#undef KR_DIAGNOSTIC_STAGES
#undef KR_DIAGNOSTIC_CAUSES
#undef KR_DIAGNOSTIC_COUNTERS
inline constexpr size_t stage_count = static_cast<size_t>(Stage::Count);
inline constexpr size_t counter_count = static_cast<size_t>(Counter::Count);
struct Refusal {
  uint32_t present = 0;
  Stage stage = Stage::Test;
  Cause cause = Cause::None;
  uint32_t result = 0;
  bool operator==(const Refusal &) const = default;
};
struct Snapshot {
  uint32_t activation = 0;
  uint32_t active = 0;
  uint32_t stopped = 0;
  uint32_t mode = 0;
  uint32_t saturated = 0;
  uint32_t request_present = 0;
  uint32_t request_result = 0;
  uint32_t session_present = 0;
  uint32_t session_result = 0;
  std::array<uint32_t, counter_count> counts{};
  std::array<uint32_t, stage_count> refusals{};
  Refusal first;
  Refusal latest;
  bool operator==(const Snapshot &) const = default;
};
static_assert(std::is_trivially_copyable_v<Snapshot>);
static_assert(sizeof(Snapshot) == 220);
static_assert(offsetof(Snapshot, counts) == 36 && offsetof(Snapshot, first) == 188);
inline void Increment(uint32_t &value, uint32_t &saturated) noexcept {
  if (value == UINT32_MAX)
    saturated = 1;
  else if (++value == UINT32_MAX)
    saturated = 1;
}
inline void Count(Snapshot &snapshot, Counter counter) noexcept {
  Increment(snapshot.counts[static_cast<size_t>(counter)], snapshot.saturated);
}
inline void Refuse(Snapshot &snapshot, Stage stage, Cause cause, HRESULT result = S_OK) noexcept {
  Increment(snapshot.refusals[static_cast<size_t>(stage)], snapshot.saturated);
  snapshot.latest = {1, stage, cause, static_cast<uint32_t>(result)};
  if (!snapshot.first.present)
    snapshot.first = snapshot.latest;
}
inline bool Valid(const Refusal &value) noexcept {
  return value.present <= 1 && value.stage < Stage::Count && value.cause < Cause::Count &&
         (value.present
              ? value.cause != Cause::None &&
                    (value.cause == Cause::Native ? value.result != 0 : value.result == 0)
              : value.stage == Stage::Test && value.cause == Cause::None && value.result == 0);
}
inline bool Valid(const Snapshot &value) noexcept {
  if (value.active > 1 || value.stopped > 1 || value.mode > KR_RU || value.saturated > 1 ||
      value.request_present > 1 || value.session_present > 1 ||
      (!value.request_present && value.request_result) ||
      (!value.session_present && value.session_result) || !Valid(value.first) ||
      !Valid(value.latest) || value.first.present != value.latest.present)
    return false;
  bool maximum = value.activation == UINT32_MAX;
  bool refusal = false;
  for (uint32_t count : value.counts)
    maximum |= count == UINT32_MAX;
  for (uint32_t count : value.refusals) {
    maximum |= count == UINT32_MAX;
    refusal |= count != 0;
  }
  return (!maximum || value.saturated) && refusal == (value.first.present != 0) &&
         (!refusal || (value.refusals[static_cast<size_t>(value.first.stage)] &&
                       value.refusals[static_cast<size_t>(value.latest.stage)]));
}
inline bool SameActivation(bool active, uint32_t current, uint32_t captured) noexcept {
  return active && current != 0 && current != UINT32_MAX && current == captured;
}
inline constexpr GUID snapshot_iid = {
    0xf75f909b, 0x293a, 0x4a7b, {0xa1, 0x72, 0xf5, 0xa0, 0x61, 0x88, 0x03, 0x52}};
struct SnapshotReader : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE Copy(uint32_t size, uint32_t requested_version,
                                         Snapshot *output) = 0;
};
inline constexpr uint32_t version_v2 = 2;
inline constexpr GUID snapshot_v2_iid = {
    0x4f89429e, 0xa955, 0x4885, {0xbb, 0x33, 0xc7, 0x8a, 0xd1, 0x98, 0x24, 0x81}};
struct SnapshotV2 {
  Snapshot base;
  LifecycleSnapshot lifecycle;
  bool operator==(const SnapshotV2 &) const = default;
};
static_assert(std::is_trivially_copyable_v<SnapshotV2>);
struct SnapshotReaderV2 : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE Copy(uint32_t size, uint32_t requested_version,
                                         SnapshotV2 *output) = 0;
};
inline const Snapshot &Base(const Snapshot &s) noexcept { return s; }
inline Snapshot &Base(Snapshot &s) noexcept { return s; }
inline const Snapshot &Base(const SnapshotV2 &s) noexcept { return s.base; }
inline Snapshot &Base(SnapshotV2 &s) noexcept { return s.base; }
inline bool Valid(const SnapshotV2 &s) noexcept { return Valid(s.base) && Valid(s.lifecycle); }
inline bool Saturated(const Snapshot &s) noexcept { return s.saturated != 0; }
inline bool Saturated(const SnapshotV2 &s) noexcept {
  return s.base.saturated || s.lifecycle.saturated;
}
enum class Availability : uint32_t {
  Missing,
  Error,
  Inactive,
  Busy,
  Available,
  UnsupportedVersion,
  Count
};
inline constexpr const wchar_t *availability_labels[] = {
    L"Missing", L"Error", L"Inactive", L"Busy", L"Available", L"UnsupportedVersion"};
enum class Continuity : uint32_t { None, Reset, Same, Indeterminate, Count };
inline constexpr const wchar_t *continuity_labels[] = {L"None", L"Reset", L"Same",
                                                       L"Indeterminate"};
template <typename S> struct BasicPublication {
  uint32_t publication = 0;
  uint32_t saturated = 0;
  uint32_t source = 0;
  Availability availability = Availability::Missing;
  Continuity continuity = Continuity::None;
  uint32_t result = 0;
  S snapshot;
};
using Publication = BasicPublication<Snapshot>;
using PublicationV2 = BasicPublication<SnapshotV2>;
template <typename S> inline bool Valid(const BasicPublication<S> &value) noexcept {
  if constexpr (std::is_same_v<S, Snapshot>) {
    if (value.availability >= Availability::UnsupportedVersion)
      return false;
  } else if (value.availability == Availability::UnsupportedVersion &&
             value.result != static_cast<uint32_t>(E_NOINTERFACE)) {
    return false;
  }
  if (value.availability >= Availability::Count || value.continuity >= Continuity::Count ||
      value.saturated > 1 ||
      ((value.publication == UINT32_MAX || value.source == UINT32_MAX) && !value.saturated) ||
      !Valid(value.snapshot))
    return false;
  if (value.availability == Availability::Available)
    return Base(value.snapshot).active && value.result == 0 && value.source &&
           value.continuity != Continuity::None &&
           ((value.saturated || Saturated(value.snapshot)) ==
            (value.continuity == Continuity::Indeterminate));
  if (value.availability == Availability::Inactive)
    return !Base(value.snapshot).active && value.result == 0 &&
           value.continuity == Continuity::None;
  S empty;
  return value.continuity == Continuity::None && value.snapshot == empty;
}
template <typename S>
inline bool Format(const BasicPublication<S> &value, wchar_t (&output)[status_capacity]) noexcept {
  output[0] = 0;
  if (!Valid(value))
    return false;
  size_t used = 0;
  auto add = [&](const wchar_t *label, uint32_t number) {
    int size = swprintf_s(output + used, status_capacity - used, L"; %ls=%lu", label,
                          static_cast<unsigned long>(number));
    if (size < 0)
      return false;
    used += size;
    return true;
  };
  int size = swprintf_s(output, L"Keyrill service v%u; state=%ls; continuity=%ls",
                        std::is_same_v<S, Snapshot> ? version : version_v2,
                        availability_labels[static_cast<size_t>(value.availability)],
                        continuity_labels[static_cast<size_t>(value.continuity)]);
  if (size < 0)
    return false;
  used = size;
  const auto &s = Base(value.snapshot);
  if (!add(L"publication", value.publication) || !add(L"host_saturated", value.saturated) ||
      !add(L"source", value.source) || !add(L"result", value.result) ||
      !add(L"activation", s.activation) || !add(L"active", s.active) ||
      !add(L"stopped", s.stopped) || !add(L"mode", s.mode) || !add(L"saturated", s.saturated) ||
      !add(L"request_present", s.request_present) || !add(L"request_result", s.request_result) ||
      !add(L"session_present", s.session_present) || !add(L"session_result", s.session_result))
    return false;
  for (size_t i = 0; i != counter_count; ++i)
    if (!add(counter_labels[i], s.counts[i]))
      return false;
  for (size_t i = 0; i != stage_count; ++i)
    if (!add(stage_labels[i], s.refusals[i]))
      return false;
  for (const auto *refusal : {&s.first, &s.latest}) {
    size = swprintf_s(output + used, status_capacity - used, L"; %ls=%u/%ls/%ls/%lu",
                      refusal == &s.first ? L"first" : L"latest", refusal->present,
                      stage_labels[static_cast<size_t>(refusal->stage)],
                      cause_labels[static_cast<size_t>(refusal->cause)],
                      static_cast<unsigned long>(refusal->result));
    if (size < 0)
      return false;
    used += size;
  }
  if constexpr (std::is_same_v<S, SnapshotV2>) {
    const auto &l = value.snapshot.lifecycle;
    if (!add(L"event_sequence", l.sequence) || !add(L"mark_allocated", l.allocated) ||
        !add(L"lifecycle_saturated", l.saturated) || !add(L"event_count", l.count) ||
        !add(L"owned_present", l.owned_present) || !add(L"owned_serial", l.owned_serial))
      return false;
    for (const auto &e : l.events) {
      size = swprintf_s(output + used, status_capacity - used, L"; event=%lu/%lu/%lu/%lu/%lu",
                        static_cast<unsigned long>(e.sequence), static_cast<unsigned long>(e.kind),
                        static_cast<unsigned long>(e.subject), static_cast<unsigned long>(e.cause),
                        static_cast<unsigned long>(e.unresolved));
      if (size < 0)
        return false;
      used += size;
    }
  }
  return true;
}
template <typename Enum, size_t N>
inline bool Label(std::wstring_view &input, const wchar_t *const (&labels)[N],
                  Enum &value) noexcept {
  for (size_t i = 0; i != N; ++i) {
    auto candidate = input;
    if (routing::Take(candidate, labels[i]) &&
        (candidate.empty() || candidate.front() == L';' || candidate.front() == L'/')) {
      input = candidate;
      value = static_cast<Enum>(i);
      return true;
    }
  }
  return false;
}
template <typename S>
inline bool Parse(std::wstring_view input, BasicPublication<S> &output) noexcept {
  if (input.empty() || input.size() >= status_capacity)
    return false;
  BasicPublication<S> parsed;
  if (!routing::Take(input, std::is_same_v<S, Snapshot> ? L"Keyrill service v1; state="
                                                        : L"Keyrill service v2; state=") ||
      !Label(input, availability_labels, parsed.availability) ||
      !routing::Take(input, L"; continuity=") ||
      !Label(input, continuity_labels, parsed.continuity))
    return false;
  auto number = [&](const wchar_t *label, uint32_t &value) {
    return routing::Take(input, L"; ") && routing::Take(input, label) &&
           routing::Take(input, L"=") && routing::Number(input, value);
  };
  auto &s = Base(parsed.snapshot);
  if (!number(L"publication", parsed.publication) || !number(L"host_saturated", parsed.saturated) ||
      !number(L"source", parsed.source) || !number(L"result", parsed.result) ||
      !number(L"activation", s.activation) || !number(L"active", s.active) ||
      !number(L"stopped", s.stopped) || !number(L"mode", s.mode) ||
      !number(L"saturated", s.saturated) || !number(L"request_present", s.request_present) ||
      !number(L"request_result", s.request_result) ||
      !number(L"session_present", s.session_present) ||
      !number(L"session_result", s.session_result))
    return false;
  for (size_t i = 0; i != counter_count; ++i)
    if (!number(counter_labels[i], s.counts[i]))
      return false;
  for (size_t i = 0; i != stage_count; ++i)
    if (!number(stage_labels[i], s.refusals[i]))
      return false;
  for (auto *refusal : {&s.first, &s.latest})
    if (!number(refusal == &s.first ? L"first" : L"latest", refusal->present) ||
        !routing::Take(input, L"/") || !Label(input, stage_labels, refusal->stage) ||
        !routing::Take(input, L"/") || !Label(input, cause_labels, refusal->cause) ||
        !routing::Take(input, L"/") || !routing::Number(input, refusal->result))
      return false;
  if constexpr (std::is_same_v<S, SnapshotV2>) {
    auto &l = parsed.snapshot.lifecycle;
    if (!number(L"event_sequence", l.sequence) || !number(L"mark_allocated", l.allocated) ||
        !number(L"lifecycle_saturated", l.saturated) || !number(L"event_count", l.count) ||
        !number(L"owned_present", l.owned_present) || !number(L"owned_serial", l.owned_serial))
      return false;
    for (auto &e : l.events) {
      uint32_t kind = 0, cause = 0, unresolved = 0;
      if (!number(L"event", e.sequence) || !routing::Take(input, L"/") ||
          !routing::Number(input, kind) || !routing::Take(input, L"/") ||
          !routing::Number(input, e.subject) || !routing::Take(input, L"/") ||
          !routing::Number(input, cause) || !routing::Take(input, L"/") ||
          !routing::Number(input, unresolved))
        return false;
      e.kind = static_cast<LifecycleKind>(kind);
      e.cause = static_cast<LifecycleCause>(cause);
      e.unresolved = static_cast<Unresolved>(unresolved);
    }
  }
  if (!input.empty() || !Valid(parsed))
    return false;
  output = parsed;
  return true;
}

inline bool Comparable(const PublicationV2 &before, const PublicationV2 &after) noexcept {
  if (!Valid(before) || !Valid(after) || before.availability != Availability::Available ||
      after.availability != Availability::Available || !before.publication ||
      after.publication <= before.publication || before.source != after.source ||
      !before.snapshot.base.activation ||
      before.snapshot.base.activation != after.snapshot.base.activation ||
      after.continuity != Continuity::Same || before.saturated || after.saturated ||
      Saturated(before.snapshot) || Saturated(after.snapshot))
    return false;
  const auto &a = before.snapshot.lifecycle;
  const auto &b = after.snapshot.lifecycle;
  if (b.sequence < a.sequence || b.allocated < a.allocated || b.sequence - a.sequence > b.count)
    return false;
  const auto oldest = b.sequence - b.count;
  for (size_t i = 0; i != a.count; ++i)
    if (a.events[i].sequence > oldest && a.events[i] != b.events[a.events[i].sequence - oldest - 1])
      return false;
  return true;
}

class Poller {
public:
  PublicationV2 value;
  void Reset() noexcept {
    identity_.Reset();
    activation_ = 0;
    connected_ = false;
  }
  void Poll(ITfThreadMgr *manager) noexcept {
    Increment(value.publication, value.saturated);
    value.availability = Availability::Error;
    value.continuity = Continuity::None;
    value.result = static_cast<uint32_t>(E_FAIL);
    value.snapshot = {};
    try {
      ComPtr<ITfFunctionProvider> provider;
      HRESULT result = manager ? manager->GetFunctionProvider(service_id, &provider) : E_POINTER;
      value.result = static_cast<uint32_t>(result);
      if (result != S_OK || !provider) {
        value.availability =
            result == TF_E_NOPROVIDER ? Availability::Missing : Availability::Error;
        Reset();
        return;
      }
      ComPtr<IUnknown> identity;
      result = provider.As(&identity);
      ComPtr<IUnknown> function;
      const bool requested_reader = result == S_OK;
      if (result == S_OK)
        result = provider->GetFunction(GUID_NULL, snapshot_v2_iid, &function);
      ComPtr<SnapshotReaderV2> reader;
      if (result == S_OK && function)
        result = function->QueryInterface(snapshot_v2_iid,
                                          reinterpret_cast<void **>(reader.GetAddressOf()));
      const bool unsupported = requested_reader && result == E_NOINTERFACE;
      SnapshotV2 snapshot;
      if (result == S_OK && reader)
        result = reader->Copy(sizeof(snapshot), version_v2, &snapshot);
      value.result = static_cast<uint32_t>(result);
      if (result != S_OK || !identity || !reader || !Valid(snapshot)) {
        value.availability = result == E_PENDING ? Availability::Busy
                             : unsupported       ? Availability::UnsupportedVersion
                                                 : Availability::Error;
        Reset();
        return;
      }
      value.result = 0;
      value.snapshot = snapshot;
      if (!snapshot.base.active) {
        value.availability = Availability::Inactive;
        Reset();
        return;
      }
      bool same = connected_ && identity_.Get() == identity.Get() &&
                  activation_ == snapshot.base.activation;
      if (!same)
        Increment(value.source, value.saturated);
      value.availability = Availability::Available;
      value.continuity = value.saturated || Saturated(snapshot) ? Continuity::Indeterminate
                         : same                                 ? Continuity::Same
                                                                : Continuity::Reset;
      identity_ = identity;
      activation_ = snapshot.base.activation;
      connected_ = true;
    } catch (...) {
      value.snapshot = {};
      value.availability = Availability::Error;
      value.continuity = Continuity::None;
      value.result = static_cast<uint32_t>(E_UNEXPECTED);
      Reset();
    }
  }

private:
  ComPtr<IUnknown> identity_;
  uint32_t activation_ = 0;
  bool connected_ = false;
};
} // namespace keyrill::diagnostics
