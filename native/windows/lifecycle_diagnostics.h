#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace keyrill::diagnostics {
enum class LifecycleKind : uint32_t {
  None,
  Acquired,
  EndRequested,
  FinishVerified,
  ExactTermination,
  DifferentTermination,
  TerminationWithoutOwned,
  TerminationUnresolved,
  LifecycleInvalidation,
  Detached,
  Count
};
enum class LifecycleCause : uint32_t {
  None,
  ForegroundLost,
  DocumentFocus,
  DocumentUninit,
  ContextPush,
  ContextPop,
  ProfileChanged,
  KeyAdmission,
  Deactivate,
  Count
};
enum class Unresolved : uint32_t { None, NullArgument, IdentityFailure, Stale, Exception, Count };
struct LifecycleEvent {
  uint32_t sequence = 0;
  LifecycleKind kind = LifecycleKind::None;
  uint32_t subject = 0;
  LifecycleCause cause = LifecycleCause::None;
  Unresolved unresolved = Unresolved::None;
  bool operator==(const LifecycleEvent &) const = default;
};
inline constexpr size_t lifecycle_capacity = 16;
struct LifecycleSnapshot {
  uint32_t sequence = 0;
  uint32_t allocated = 0;
  uint32_t saturated = 0;
  uint32_t count = 0;
  uint32_t owned_present = 0;
  uint32_t owned_serial = 0;
  std::array<LifecycleEvent, lifecycle_capacity> events{};
  bool operator==(const LifecycleSnapshot &) const = default;
};
class LifecycleJournal {
public:
  uint32_t sequence() const noexcept { return window_.sequence; }
  uint32_t Acquire() noexcept {
    uint32_t serial = 0;
    if (window_.allocated != UINT32_MAX) {
      serial = ++window_.allocated;
      if (serial == UINT32_MAX)
        window_.saturated = 1;
    } else {
      window_.saturated = 1;
    }
    Record(LifecycleKind::Acquired, serial);
    return serial;
  }
  void Record(LifecycleKind kind, uint32_t subject, LifecycleCause cause = LifecycleCause::None,
              Unresolved unresolved = Unresolved::None) noexcept {
    if (window_.sequence == UINT32_MAX) {
      window_.saturated = 1;
      return;
    }
    const auto sequence = ++window_.sequence;
    if (sequence == UINT32_MAX)
      window_.saturated = 1;
    if (window_.count == lifecycle_capacity)
      std::move(window_.events.begin() + 1, window_.events.end(), window_.events.begin());
    else
      ++window_.count;
    window_.events[window_.count - 1] = {sequence, kind, subject, cause, unresolved};
  }
  LifecycleSnapshot Copy(bool owned, uint32_t serial) const noexcept {
    auto result = window_;
    result.owned_present = owned;
    result.owned_serial = serial;
    return result;
  }
  explicit LifecycleJournal(uint32_t sequence = 0, uint32_t allocated = 0) noexcept {
    window_.sequence = sequence;
    window_.allocated = allocated;
    window_.saturated = sequence == UINT32_MAX || allocated == UINT32_MAX;
  }

private:
  LifecycleSnapshot window_;
};
inline bool Valid(const LifecycleSnapshot &s) noexcept {
  if (s.saturated > 1 || s.owned_present > 1 || s.count > lifecycle_capacity ||
      s.count != (std::min)(s.sequence, static_cast<uint32_t>(lifecycle_capacity)) ||
      ((!s.owned_present && s.owned_serial) || s.owned_serial > s.allocated) ||
      (s.owned_present && !s.owned_serial && s.allocated != UINT32_MAX) ||
      ((s.sequence == UINT32_MAX || s.allocated == UINT32_MAX) && !s.saturated))
    return false;
  for (size_t i = 0; i != lifecycle_capacity; ++i) {
    const auto &e = s.events[i];
    if (i >= s.count) {
      if (e != LifecycleEvent{})
        return false;
      continue;
    }
    if (e.sequence != s.sequence - s.count + 1 + i || e.kind <= LifecycleKind::None ||
        e.kind >= LifecycleKind::Count || e.cause >= LifecycleCause::Count ||
        e.unresolved >= Unresolved::Count || e.subject > s.allocated)
      return false;
    if ((e.kind == LifecycleKind::LifecycleInvalidation) != (e.cause != LifecycleCause::None) ||
        (e.kind == LifecycleKind::TerminationUnresolved) != (e.unresolved != Unresolved::None))
      return false;
    if (e.kind == LifecycleKind::TerminationWithoutOwned && e.subject)
      return false;
    if ((e.kind == LifecycleKind::Acquired || e.kind == LifecycleKind::EndRequested ||
         e.kind == LifecycleKind::FinishVerified || e.kind == LifecycleKind::ExactTermination ||
         e.kind == LifecycleKind::DifferentTermination || e.kind == LifecycleKind::Detached) &&
        !e.subject && s.allocated != UINT32_MAX)
      return false;
  }
  return true;
}
} // namespace keyrill::diagnostics
