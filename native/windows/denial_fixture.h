#pragma once
#include "service_diagnostics.h"

namespace keyrill::denial {
enum class Startup { Normal, DenyTsfLocks, Invalid };
inline Startup ParseStartup(int argc, const wchar_t *const *argv) noexcept {
  if (argc == 1)
    return Startup::Normal;
  if (argc == 2 && wcscmp(argv[1], L"--deny-tsf-locks") == 0)
    return Startup::DenyTsfLocks;
  return Startup::Invalid;
}
inline constexpr int setup_id = 107;
inline constexpr wchar_t title[] = L"Keyrill TSF native lock-denial fixture";
inline constexpr wchar_t setup_status[] = L"Keyrill native denial v1; setup=Confirmed";
enum class Setup { Unknown, Confirmed };
inline Setup ParseSetup(std::wstring_view input) noexcept {
  return input == setup_status ? Setup::Confirmed : Setup::Unknown;
}
inline bool Matches(Setup setup, const diagnostics::Publication &first,
                    const diagnostics::Publication &second) noexcept {
  using namespace diagnostics;
  if (setup != Setup::Confirmed || !Valid(first) || !Valid(second) || !first.publication ||
      second.publication <= first.publication || first.source != second.source ||
      first.snapshot.activation != second.snapshot.activation ||
      second.continuity != Continuity::Same)
    return false;
  for (const auto *publication : {&first, &second}) {
    const auto &s = publication->snapshot;
    if (publication->availability != Availability::Available || publication->saturated ||
        s.saturated || !s.activation || !s.active || s.stopped || s.mode != KR_EN ||
        !s.counts[static_cast<size_t>(Counter::Requested)] || !s.request_present)
      return false;
    for (auto counter : {Counter::SessionEntered, Counter::SessionCompleted, Counter::Attempted,
                         Counter::Verified, Counter::Unknown, Counter::Consumed})
      if (s.counts[static_cast<size_t>(counter)])
        return false;
    const bool request_failed = FAILED(static_cast<HRESULT>(s.request_result));
    const Stage stage = request_failed ? Stage::Request : Stage::Session;
    const uint32_t result = request_failed ? s.request_result : s.session_result;
    if ((!request_failed && (s.request_result != S_OK || !s.session_present ||
                             !FAILED(static_cast<HRESULT>(s.session_result)))) ||
        !s.latest.present || s.latest.stage != stage || s.latest.cause != Cause::Native ||
        s.latest.result != result || !s.refusals[static_cast<size_t>(stage)])
      return false;
  }
  return true;
}
} // namespace keyrill::denial
