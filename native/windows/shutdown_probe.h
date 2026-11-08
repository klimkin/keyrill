#pragma once
#include "service_diagnostics.h"
#include <variant>

namespace keyrill::shutdown {
using diagnostics::ComPtr;
inline constexpr int mode_id = 108;
inline constexpr wchar_t title[] = L"Keyrill TSF owned shutdown fixture";
inline constexpr wchar_t mode_status[] = L"Keyrill owned shutdown v1; mode=ProveOwnedShutdown";
enum class Verdict : int {
  Verified = 0,
  NotCaptured = 10,
  Missing = 11,
  Busy = 12,
  InvalidEvidence = 13,
  ProviderChanged = 14,
  ActivationChanged = 15,
  NoOwnedMark = 16,
  HistoryLost = 17,
  UnknownEffect = 18,
  NoTerminalEvent = 19,
  Detached = 20,
  DeactivationFailed = 21
};
inline int ExitCode(Verdict verdict) noexcept { return static_cast<int>(verdict); }

namespace detail {
inline Verdict Active(const diagnostics::SnapshotV2 &s) noexcept {
  if (!diagnostics::Valid(s) || diagnostics::Saturated(s) || !s.base.active || !s.base.activation ||
      s.base.activation == UINT32_MAX || s.base.stopped)
    return Verdict::InvalidEvidence;
  if (s.base.counts[static_cast<size_t>(diagnostics::Counter::Unknown)])
    return Verdict::UnknownEffect;
  return Verdict::Verified;
}
inline Verdict Capturable(const diagnostics::SnapshotV2 &s) noexcept {
  const auto active = Active(s);
  if (active != Verdict::Verified)
    return active;
  return s.lifecycle.owned_present && s.lifecycle.owned_serial &&
                 s.lifecycle.owned_serial != UINT32_MAX
             ? Verdict::Verified
             : Verdict::NoOwnedMark;
}
inline Verdict Evaluate(const diagnostics::SnapshotV2 &before,
                        const diagnostics::SnapshotV2 &after) noexcept {
  using namespace diagnostics;
  const auto capture = Capturable(before);
  if (capture != Verdict::Verified)
    return capture;
  if (!Valid(after) || Saturated(after) || after.base.active || after.lifecycle.owned_present ||
      after.lifecycle.owned_serial)
    return Verdict::InvalidEvidence;
  if (before.base.activation != after.base.activation)
    return Verdict::ActivationChanged;
  if (after.base.counts[static_cast<size_t>(Counter::Unknown)])
    return Verdict::UnknownEffect;
  for (size_t i = 0; i != counter_count; ++i)
    if (after.base.counts[i] < before.base.counts[i])
      return Verdict::InvalidEvidence;
  for (size_t i = 0; i != stage_count; ++i)
    if (after.base.refusals[i] < before.base.refusals[i])
      return Verdict::InvalidEvidence;
  const auto &a = before.lifecycle;
  const auto &b = after.lifecycle;
  if (b.allocated != a.allocated)
    return Verdict::InvalidEvidence;
  if (b.sequence < a.sequence || b.sequence - a.sequence > b.count)
    return Verdict::HistoryLost;
  const auto oldest = b.sequence - b.count;
  for (size_t i = 0; i != a.count; ++i)
    if (a.events[i].sequence > oldest && a.events[i] != b.events[a.events[i].sequence - oldest - 1])
      return Verdict::InvalidEvidence;
  bool end_requested = false, terminal = false, deactivated = false;
  for (size_t i = 0; i != b.count; ++i) {
    const auto &event = b.events[i];
    if (event.sequence <= a.sequence)
      continue;
    if (event.kind == LifecycleKind::Acquired)
      return Verdict::InvalidEvidence;
    if (event.kind == LifecycleKind::Detached)
      return Verdict::Detached;
    if (event.kind == LifecycleKind::TerminationUnresolved)
      return Verdict::InvalidEvidence;
    if (event.kind == LifecycleKind::LifecycleInvalidation &&
        event.cause == LifecycleCause::Deactivate) {
      if (!deactivated && !terminal)
        return Verdict::NoTerminalEvent;
      deactivated = true;
    }
    if (event.subject != a.owned_serial || deactivated)
      continue;
    if (event.kind == LifecycleKind::EndRequested)
      end_requested = true;
    if (event.kind == LifecycleKind::ExactTermination ||
        (event.kind == LifecycleKind::FinishVerified && end_requested))
      terminal = true;
  }
  return terminal && deactivated ? Verdict::Verified : Verdict::NoTerminalEvent;
}
} // namespace detail

class Probe final {
public:
  Probe() = default;
  Probe(const Probe &) = delete;
  Probe &operator=(const Probe &) = delete;

  void Observe(ITfThreadMgr *manager) noexcept {
    const bool binding = std::holds_alternative<Awaiting>(state_);
    auto *watching = std::get_if<Watching>(&state_);
    if (!binding && !watching)
      return;
    Endpoint endpoint = watching ? std::move(watching->endpoint) : Endpoint{};
    const auto operation = binding ? Operation::Binding : Operation::Observing;
    state_.emplace<InFlight>(operation);
    try {
      const auto discovered = Discover(manager, endpoint, operation, binding);
      if (!Running(operation))
        return;
      if (binding && discovered == Verdict::Missing) {
        state_.emplace<Awaiting>();
        return;
      }
      diagnostics::SnapshotV2 snapshot;
      auto verdict = discovered;
      if (verdict == Verdict::Verified)
        verdict = Copy(endpoint, operation, snapshot);
      if (!Running(operation))
        return;
      if (verdict == Verdict::Verified)
        verdict = detail::Active(snapshot);
      if (verdict == Verdict::Verified && !binding &&
          snapshot.base.activation != endpoint.activation)
        verdict = Verdict::ActivationChanged;
      if (verdict != Verdict::Verified) {
        Reject(verdict);
        return;
      }
      endpoint.activation = snapshot.base.activation;
      state_.emplace<Watching>(std::move(endpoint));
    } catch (...) {
      Reject(Verdict::InvalidEvidence);
    }
  }

  void CaptureClose(ITfThreadMgr *manager) noexcept {
    if (Terminal())
      return;
    auto *watching = std::get_if<Watching>(&state_);
    if (!watching) {
      Reject(Verdict::NotCaptured);
      return;
    }
    Endpoint endpoint = std::move(watching->endpoint);
    state_.emplace<InFlight>(Operation::Capturing);
    try {
      auto verdict = Discover(manager, endpoint, Operation::Capturing, false);
      diagnostics::SnapshotV2 snapshot;
      if (Running(Operation::Capturing) && verdict == Verdict::Verified)
        verdict = Copy(endpoint, Operation::Capturing, snapshot);
      if (!Running(Operation::Capturing))
        return;
      if (verdict == Verdict::Verified)
        verdict = detail::Capturable(snapshot);
      if (verdict == Verdict::Verified && snapshot.base.activation != endpoint.activation)
        verdict = Verdict::ActivationChanged;
      if (verdict != Verdict::Verified) {
        Reject(verdict);
        return;
      }
      state_.emplace<Closing>(std::move(endpoint), snapshot);
    } catch (...) {
      Reject(Verdict::InvalidEvidence);
    }
  }

  Verdict Finish(HRESULT manager_deactivation) noexcept {
    if (const auto *completed = std::get_if<Completed>(&state_))
      return completed->verdict;
    auto *closing = std::get_if<Closing>(&state_);
    Verdict verdict = Verdict::NotCaptured;
    if (closing) {
      Endpoint endpoint = std::move(closing->endpoint);
      const auto before = closing->before;
      state_.emplace<InFlight>(Operation::Finishing);
      verdict = Verdict::DeactivationFailed;
      try {
        if (manager_deactivation == S_OK) {
          diagnostics::SnapshotV2 after;
          verdict = Copy(endpoint, Operation::Finishing, after);
          if (Running(Operation::Finishing) && verdict == Verdict::Verified)
            verdict = detail::Evaluate(before, after);
        }
      } catch (...) {
        Reject(Verdict::InvalidEvidence);
      }
      endpoint.reader.Reset();
      endpoint.identity.Reset();
    } else if (auto *watching = std::get_if<Watching>(&state_)) {
      Endpoint endpoint = std::move(watching->endpoint);
      state_.emplace<InFlight>(Operation::Finishing);
      endpoint.reader.Reset();
      endpoint.identity.Reset();
    }
    if (const auto *rejected = std::get_if<Rejected>(&state_))
      verdict = rejected->reason;
    state_.emplace<Completed>(verdict);
    return verdict;
  }

private:
  struct Endpoint {
    ComPtr<IUnknown> identity;
    ComPtr<diagnostics::SnapshotReaderV2> reader;
    uint32_t activation = 0;
  };
  struct Awaiting {};
  enum class Operation { Binding, Observing, Capturing, Finishing };
  struct InFlight {
    Operation operation;
  };
  struct Watching {
    Endpoint endpoint;
  };
  struct Closing {
    Endpoint endpoint;
    diagnostics::SnapshotV2 before;
  };
  struct Rejected {
    Verdict reason;
  };
  struct Completed {
    Verdict verdict;
  };
  std::variant<Awaiting, InFlight, Watching, Closing, Rejected, Completed> state_;

  bool Terminal() const noexcept {
    return std::holds_alternative<Rejected>(state_) || std::holds_alternative<Completed>(state_);
  }
  bool Running(Operation operation) const noexcept {
    const auto *flight = std::get_if<InFlight>(&state_);
    return flight && flight->operation == operation;
  }
  void Reject(Verdict reason) noexcept {
    if (Terminal())
      return;
    Endpoint endpoint;
    if (auto *closing = std::get_if<Closing>(&state_))
      endpoint = std::move(closing->endpoint);
    state_.emplace<Rejected>(reason);
  }
  Verdict Discover(ITfThreadMgr *manager, Endpoint &endpoint, Operation operation, bool bind) {
    ComPtr<ITfFunctionProvider> provider;
    HRESULT result = manager ? manager->GetFunctionProvider(service_id, &provider) : E_POINTER;
    if (!Running(operation))
      return Verdict::InvalidEvidence;
    if (result != S_OK || !provider)
      return result == TF_E_NOPROVIDER ? Verdict::Missing : Verdict::InvalidEvidence;
    ComPtr<IUnknown> identity;
    result = provider.As(&identity);
    if (!Running(operation) || result != S_OK || !identity)
      return Verdict::InvalidEvidence;
    if (!bind)
      return identity.Get() == endpoint.identity.Get() ? Verdict::Verified
                                                       : Verdict::ProviderChanged;
    ComPtr<IUnknown> function;
    result = provider->GetFunction(GUID_NULL, diagnostics::snapshot_v2_iid, &function);
    if (!Running(operation) || result != S_OK || !function)
      return Verdict::InvalidEvidence;
    ComPtr<diagnostics::SnapshotReaderV2> reader;
    result = function->QueryInterface(diagnostics::snapshot_v2_iid,
                                      reinterpret_cast<void **>(reader.GetAddressOf()));
    if (!Running(operation) || result != S_OK || !reader)
      return Verdict::InvalidEvidence;
    ComPtr<IUnknown> reader_identity;
    result = reader.As(&reader_identity);
    if (!Running(operation) || result != S_OK || !reader_identity)
      return Verdict::InvalidEvidence;
    if (identity.Get() != reader_identity.Get())
      return Verdict::ProviderChanged;
    endpoint.identity = std::move(identity);
    endpoint.reader = std::move(reader);
    return Verdict::Verified;
  }
  Verdict Copy(const Endpoint &endpoint, Operation operation, diagnostics::SnapshotV2 &snapshot) {
    const HRESULT result =
        endpoint.reader->Copy(sizeof(snapshot), diagnostics::version_v2, &snapshot);
    if (!Running(operation))
      return Verdict::InvalidEvidence;
    return result == S_OK        ? Verdict::Verified
           : result == E_PENDING ? Verdict::Busy
                                 : Verdict::InvalidEvidence;
  }
};
} // namespace keyrill::shutdown
