#pragma once
#include "fixture.h"
#include "service_diagnostics.h"
#include <variant>

namespace keyrill::source_proof {
using diagnostics::ComPtr;
using diagnostics::SnapshotV2;
inline constexpr int mode_id = 109;
inline constexpr int result_id = 110;
inline constexpr wchar_t title[] = L"Keyrill TSF owned source fixture";
inline constexpr wchar_t mode_status[] = L"Keyrill owned source v1; mode=ProveOwnedEnToRu";
inline constexpr size_t status_capacity = 1024;
inline constexpr uint64_t countdown_ms = 2000, late_ms = 250;
enum class Stage : uint32_t { Waiting, Countdown, Running, Verified, Failed, Count };
enum class Failure : uint32_t {
  None,
  Closed,
  SetupUnavailable,
  WrongThread,
  ProviderMissing,
  ProviderChanged,
  ActivationChanged,
  Busy,
  InvalidEvidence,
  Saturated,
  WrongSource,
  TargetUnavailable,
  FocusChanged,
  WordChanged,
  OwnershipLost,
  HistoryLost,
  UnknownEffect,
  NativeLanguageFailed,
  LanguageNotChanged,
  NativeActivationFailed,
  TargetNotActive,
  ServiceNotRu,
  NoTerminal,
  Detached,
  UnresolvedTermination,
  InputInterference,
  Late,
  Count
};
inline constexpr const wchar_t *stage_labels[] = {L"Waiting", L"Countdown", L"Running", L"Verified",
                                                  L"Failed"};
inline constexpr const wchar_t *failure_labels[] = {L"None",
                                                    L"Closed",
                                                    L"SetupUnavailable",
                                                    L"WrongThread",
                                                    L"ProviderMissing",
                                                    L"ProviderChanged",
                                                    L"ActivationChanged",
                                                    L"Busy",
                                                    L"InvalidEvidence",
                                                    L"Saturated",
                                                    L"WrongSource",
                                                    L"TargetUnavailable",
                                                    L"FocusChanged",
                                                    L"WordChanged",
                                                    L"OwnershipLost",
                                                    L"HistoryLost",
                                                    L"UnknownEffect",
                                                    L"NativeLanguageFailed",
                                                    L"LanguageNotChanged",
                                                    L"NativeActivationFailed",
                                                    L"TargetNotActive",
                                                    L"ServiceNotRu",
                                                    L"NoTerminal",
                                                    L"Detached",
                                                    L"UnresolvedTermination",
                                                    L"InputInterference",
                                                    L"Late"};
static_assert(std::size(stage_labels) == static_cast<size_t>(Stage::Count));
static_assert(std::size(failure_labels) == static_cast<size_t>(Failure::Count));
enum class Completion : uint32_t { None, ExactTermination, VerifiedFinish };
enum class Phase : uint32_t { None, LanguageSelection, ProfileActivation };
enum class Order : uint32_t { NoProfileEvent, BeforeProfile, AfterProfile };
struct Status {
  Stage stage = Stage::Waiting;
  Failure failure = Failure::None;
  uint32_t language_calls = 0, activation_calls = 0, native_result = 0;
  uint32_t activation = 0, serial = 0, before = 0, after_language = 0, after = 0;
  uint32_t terminal = 0, profile = 0;
  Completion completion = Completion::None;
  Phase phase = Phase::None;
  Order order = Order::NoProfileEvent;
  bool operator==(const Status &) const = default;
};
inline bool Valid(const Status &s) noexcept {
  if (s.stage >= Stage::Count || s.failure >= Failure::Count || s.language_calls > 1 ||
      s.activation_calls > s.language_calls || s.completion > Completion::VerifiedFinish ||
      s.phase > Phase::ProfileActivation || s.order > Order::AfterProfile)
    return false;
  if (s.stage == Stage::Verified)
    return s.failure == Failure::None && s.language_calls == 1 && !s.native_result &&
           s.activation && s.activation != UINT32_MAX && s.serial && s.serial != UINT32_MAX &&
           s.before < s.terminal && s.terminal <= s.after && s.before <= s.after_language &&
           s.after_language <= s.after && s.after - s.before <= diagnostics::lifecycle_capacity &&
           s.completion != Completion::None &&
           s.phase == (s.terminal <= s.after_language ? Phase::LanguageSelection
                                                      : Phase::ProfileActivation) &&
           (s.phase != Phase::ProfileActivation || s.activation_calls == 1) &&
           (s.profile ? s.profile > s.before && s.profile <= s.after && s.profile != s.terminal &&
                            s.order == (s.terminal < s.profile ? Order::BeforeProfile
                                                               : Order::AfterProfile)
                      : s.order == Order::NoProfileEvent);
  if (s.stage == Stage::Failed)
    return s.failure != Failure::None && !s.activation && !s.serial && !s.before &&
           !s.after_language && !s.after && !s.terminal && !s.profile &&
           s.completion == Completion::None && s.phase == Phase::None &&
           s.order == Order::NoProfileEvent;
  Status empty;
  empty.stage = s.stage;
  return s == empty;
}
inline bool Format(const Status &s, wchar_t (&out)[status_capacity]) noexcept {
  out[0] = 0;
  if (!Valid(s))
    return false;
  return swprintf_s(out,
                    L"Keyrill source result v1; state=%ls; failure=%ls; language_calls=%u; "
                    L"activation_calls=%u; native_result=%u; activation=%u; serial=%u; before=%u; "
                    L"after_language=%u; after=%u; terminal=%u; profile=%u; completion=%u; "
                    L"phase=%u; order=%u",
                    stage_labels[static_cast<size_t>(s.stage)],
                    failure_labels[static_cast<size_t>(s.failure)], s.language_calls,
                    s.activation_calls, s.native_result, s.activation, s.serial, s.before,
                    s.after_language, s.after, s.terminal, s.profile,
                    static_cast<uint32_t>(s.completion), static_cast<uint32_t>(s.phase),
                    static_cast<uint32_t>(s.order)) > 0;
}
inline bool Parse(std::wstring_view input, Status &out) noexcept {
  Status s;
  if (input.size() >= status_capacity ||
      !routing::Take(input, L"Keyrill source result v1; state=") ||
      !diagnostics::Label(input, stage_labels, s.stage) || !routing::Take(input, L"; failure=") ||
      !diagnostics::Label(input, failure_labels, s.failure))
    return false;
  auto number = [&](const wchar_t *label, uint32_t &value) {
    return routing::Take(input, L"; ") && routing::Take(input, label) &&
           routing::Take(input, L"=") && routing::Number(input, value);
  };
  uint32_t completion = 0, phase = 0, order = 0;
  if (!number(L"language_calls", s.language_calls) ||
      !number(L"activation_calls", s.activation_calls) ||
      !number(L"native_result", s.native_result) || !number(L"activation", s.activation) ||
      !number(L"serial", s.serial) || !number(L"before", s.before) ||
      !number(L"after_language", s.after_language) || !number(L"after", s.after) ||
      !number(L"terminal", s.terminal) || !number(L"profile", s.profile) ||
      !number(L"completion", completion) || !number(L"phase", phase) || !number(L"order", order))
    return false;
  s.completion = static_cast<Completion>(completion);
  s.phase = static_cast<Phase>(phase);
  s.order = static_cast<Order>(order);
  if (!input.empty() || !Valid(s))
    return false;
  out = s;
  return true;
}

namespace detail {
static_assert(std::size(profiles) == 2 && profiles[0].mode == KR_EN && profiles[1].mode == KR_RU);
inline bool Exact(const TF_INPUTPROCESSORPROFILE &p, const Profile &expected) noexcept {
  return p.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR && p.clsid == service_id &&
         p.guidProfile == expected.id && p.langid == expected.language &&
         p.catid == GUID_TFCAT_TIP_KEYBOARD && (p.dwFlags & TF_IPP_FLAG_ACTIVE);
}
inline Failure Active(const SnapshotV2 &s) noexcept {
  if (!diagnostics::Valid(s))
    return Failure::InvalidEvidence;
  if (diagnostics::Saturated(s))
    return Failure::Saturated;
  if (!s.base.active || !s.base.activation || s.base.stopped)
    return Failure::InvalidEvidence;
  if (s.base.counts[static_cast<size_t>(diagnostics::Counter::Unknown)])
    return Failure::UnknownEffect;
  return Failure::None;
}
inline bool SameInput(const SnapshotV2 &a, const SnapshotV2 &b) noexcept {
  using diagnostics::Counter;
  for (auto counter : {Counter::TestEntered, Counter::Interested, Counter::KeyEntered,
                       Counter::Consumed, Counter::HandleEntered})
    if (a.base.counts[static_cast<size_t>(counter)] != b.base.counts[static_cast<size_t>(counter)])
      return false;
  return true;
}
inline Failure History(const SnapshotV2 &a, const SnapshotV2 &b) noexcept {
  auto failure = Active(a);
  if (failure == Failure::None)
    failure = Active(b);
  if (failure != Failure::None)
    return failure;
  if (a.base.activation != b.base.activation)
    return Failure::ActivationChanged;
  for (size_t i = 0; i != diagnostics::counter_count; ++i)
    if (b.base.counts[i] < a.base.counts[i])
      return Failure::InvalidEvidence;
  for (size_t i = 0; i != diagnostics::stage_count; ++i)
    if (b.base.refusals[i] < a.base.refusals[i])
      return Failure::InvalidEvidence;
  const auto &x = a.lifecycle;
  const auto &y = b.lifecycle;
  if (y.allocated < x.allocated || y.sequence < x.sequence || y.sequence - x.sequence > y.count)
    return Failure::HistoryLost;
  const auto oldest = y.sequence - y.count;
  for (size_t i = 0; i != x.count; ++i)
    if (x.events[i].sequence > oldest && x.events[i] != y.events[x.events[i].sequence - oldest - 1])
      return Failure::InvalidEvidence;
  return Failure::None;
}
inline Failure Armed(const SnapshotV2 &a, const SnapshotV2 &b) noexcept {
  auto failure = History(a, b);
  if (failure != Failure::None)
    return failure;
  if (!SameInput(a, b))
    return Failure::InputInterference;
  if (b.base.mode != KR_EN)
    return Failure::WrongSource;
  if (!b.lifecycle.owned_present || b.lifecycle.owned_serial != a.lifecycle.owned_serial)
    return Failure::OwnershipLost;
  return a.lifecycle == b.lifecycle ? Failure::None : Failure::InvalidEvidence;
}
inline Failure WordHistory(const SnapshotV2 &empty, const SnapshotV2 &now) noexcept {
  using namespace diagnostics;
  auto failure = History(empty, now);
  if (failure != Failure::None)
    return failure;
  bool acquired = false;
  for (size_t i = 0; i != now.lifecycle.count; ++i) {
    const auto &e = now.lifecycle.events[i];
    if (e.sequence <= empty.lifecycle.sequence)
      continue;
    if (e.kind == LifecycleKind::Acquired && !acquired && e.subject == 1) {
      acquired = true;
      continue;
    }
    if (acquired || e.kind != LifecycleKind::LifecycleInvalidation ||
        e.cause == LifecycleCause::ProfileChanged || e.cause == LifecycleCause::Deactivate ||
        e.cause == LifecycleCause::KeyAdmission)
      return Failure::InvalidEvidence;
  }
  if (now.lifecycle.allocated == 0)
    return now.lifecycle.owned_present ? Failure::InvalidEvidence : Failure::None;
  return acquired && now.lifecycle.allocated == 1 && now.lifecycle.owned_present &&
                 now.lifecycle.owned_serial == 1
             ? Failure::None
             : Failure::OwnershipLost;
}
struct Terminal {
  Completion completion = Completion::None;
  uint32_t sequence = 0, profile = 0;
};
inline Failure Interval(const SnapshotV2 &before, const SnapshotV2 &after,
                        Terminal &terminal) noexcept {
  using namespace diagnostics;
  auto failure = History(before, after);
  if (failure != Failure::None)
    return failure;
  if (!SameInput(before, after))
    return Failure::InputInterference;
  if (after.lifecycle.allocated != before.lifecycle.allocated)
    return Failure::InvalidEvidence;
  bool requested = false;
  for (size_t i = 0; i != after.lifecycle.count; ++i) {
    const auto &e = after.lifecycle.events[i];
    if (e.sequence <= before.lifecycle.sequence)
      continue;
    if (e.kind == LifecycleKind::Acquired)
      return Failure::InvalidEvidence;
    if (e.kind == LifecycleKind::Detached)
      return Failure::Detached;
    if (e.kind == LifecycleKind::TerminationUnresolved)
      return Failure::UnresolvedTermination;
    if (e.kind == LifecycleKind::LifecycleInvalidation) {
      if (e.cause == LifecycleCause::Deactivate)
        return Failure::InvalidEvidence;
      if (e.cause == LifecycleCause::ProfileChanged && !terminal.profile)
        terminal.profile = e.sequence;
    }
    if (e.subject != before.lifecycle.owned_serial)
      continue;
    if (e.kind == LifecycleKind::EndRequested)
      requested = true;
    if (!terminal.sequence && (e.kind == LifecycleKind::ExactTermination ||
                               (e.kind == LifecycleKind::FinishVerified && requested))) {
      terminal.sequence = e.sequence;
      terminal.completion = e.kind == LifecycleKind::ExactTermination ? Completion::ExactTermination
                                                                      : Completion::VerifiedFinish;
    }
  }
  if (after.lifecycle.owned_present)
    return after.lifecycle.owned_serial == before.lifecycle.owned_serial && !terminal.sequence
               ? Failure::None
               : Failure::InvalidEvidence;
  return terminal.sequence ? Failure::None : Failure::NoTerminal;
}
inline Failure Evaluate(const SnapshotV2 &before, const SnapshotV2 &middle, const SnapshotV2 &after,
                        Status &status) noexcept {
  Terminal first, combined;
  auto failure = Interval(before, middle, first);
  if (failure == Failure::None)
    failure = History(middle, after);
  if (failure == Failure::None && !SameInput(middle, after))
    failure = Failure::InputInterference;
  if (failure == Failure::None)
    failure = Interval(before, after, combined);
  if (failure != Failure::None)
    return failure;
  if (before.base.mode != KR_EN || !before.lifecycle.owned_present)
    return Failure::WrongSource;
  if (after.base.mode != KR_RU)
    return Failure::ServiceNotRu;
  if (after.lifecycle.owned_present || !combined.sequence)
    return Failure::NoTerminal;
  status.activation = before.base.activation;
  status.serial = before.lifecycle.owned_serial;
  status.before = before.lifecycle.sequence;
  status.after_language = middle.lifecycle.sequence;
  status.after = after.lifecycle.sequence;
  status.terminal = combined.sequence;
  status.profile = combined.profile;
  status.completion = combined.completion;
  status.phase = combined.sequence <= middle.lifecycle.sequence ? Phase::LanguageSelection
                                                                : Phase::ProfileActivation;
  status.order = !combined.profile                      ? Order::NoProfileEvent
                 : combined.sequence < combined.profile ? Order::BeforeProfile
                                                        : Order::AfterProfile;
  return Failure::None;
}
struct PublicWord {
  Failure failure = Failure::None;
  uint32_t prefix = 0;
};
inline Failure PublicControls(HWND root, DWORD thread) noexcept {
  wchar_t type[64]{};
  DWORD process = 0;
  if (!root || GetWindowThreadProcessId(root, &process) != thread ||
      process != GetCurrentProcessId() || GetAncestor(root, GA_ROOT) != root ||
      !GetClassNameW(root, type, 64) || wcscmp(type, fixture_class))
    return Failure::SetupUnavailable;
  for (int id : {101, 102}) {
    HWND field = GetDlgItem(root, id);
    if (!field || GetWindowThreadProcessId(field, &process) != thread ||
        process != GetCurrentProcessId() || GetParent(field) != root || GetDlgCtrlID(field) != id ||
        !GetClassNameW(field, type, 64) || _wcsicmp(type, MSFTEDIT_CLASS))
      return Failure::SetupUnavailable;
    const auto style = GetWindowLongPtrW(field, GWL_STYLE);
    if (!(style & WS_CHILD) || (style & (ES_READONLY | ES_PASSWORD | WS_DISABLED)) ||
        !IsWindowEnabled(field))
      return Failure::SetupUnavailable;
  }
  return GetForegroundWindow() == root && GetFocus() == GetDlgItem(root, 101)
             ? Failure::None
             : Failure::FocusChanged;
}
template <typename Guard, typename Eligibility, typename Read>
PublicWord ReadPublicWord(Guard alive, Eligibility eligibility, Read read) {
  auto eligible = [&] {
    const auto failure = eligibility();
    return alive() ? failure : Failure::Closed;
  };
  if (auto failure = eligible(); failure != Failure::None)
    return {failure};
  uint32_t prefix = 0;
  for (int id : {101, 102}) {
    if (auto failure = eligible(); failure != Failure::None)
      return {failure};
    const auto length = read(id, WM_GETTEXTLENGTH, 0, 0);
    if (!alive())
      return {Failure::Closed};
    if (length < 0 || length > (id == 101 ? 6 : 0))
      return {Failure::WordChanged};
    if (auto failure = eligible(); failure != Failure::None)
      return {failure};
    wchar_t text[8]{};
    const auto copied = read(id, WM_GETTEXT, std::size(text), reinterpret_cast<LPARAM>(text));
    if (!alive())
      return {Failure::Closed};
    if (copied != length ||
        std::wstring_view(text, static_cast<size_t>(copied)) !=
            std::wstring_view(L"ghbdtn", id == 101 ? static_cast<size_t>(length) : 0))
      return {Failure::WordChanged};
    if (id == 101) {
      prefix = static_cast<uint32_t>(length);
      if (auto failure = eligible(); failure != Failure::None)
        return {failure};
      CHARRANGE selection{-1, -1};
      read(id, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
      if (!alive())
        return {Failure::Closed};
      if (selection.cpMin != length || selection.cpMax != length)
        return {Failure::WordChanged};
    }
  }
  return {eligible(), prefix};
}
} // namespace detail

class Probe final {
public:
  Probe(HWND window, ITfThreadMgr &manager, DWORD thread) noexcept
      : window_(window), manager_(manager), thread_(thread) {}
  Probe(const Probe &) = delete;
  Probe &operator=(const Probe &) = delete;
  ~Probe() { Close(); }
  Status Tick(uint64_t now) noexcept {
    if (!std::holds_alternative<Done>(state_) && !std::holds_alternative<InFlight>(state_)) {
      try {
        TickBatch(now);
      } catch (...) {
        Reject(Failure::InvalidEvidence);
      }
    }
    return Value();
  }
  void Close() noexcept {
    if (std::holds_alternative<Done>(state_))
      return;
    auto held = std::move(state_);
    Status status = progress_;
    status.stage = Stage::Failed;
    status.failure = Failure::Closed;
    state_.emplace<Done>(status);
  }

private:
  friend struct ProbeTestAccess;
  struct Native {
    ComPtr<ITfInputProcessorProfiles> languages;
    ComPtr<ITfInputProcessorProfileMgr> profiles;
  };
  struct Endpoint {
    ComPtr<IUnknown> identity;
    ComPtr<diagnostics::SnapshotReaderV2> reader;
    uint32_t activation = 0;
  };
  struct Capabilities {
    Native native;
    Endpoint endpoint;
  };
  struct AwaitingEmpty {
    Native native;
  };
  struct AwaitingWord {
    Capabilities caps;
    SnapshotV2 empty, last;
    uint32_t prefix = 0;
  };
  struct Countdown {
    Capabilities caps;
    SnapshotV2 ready;
    uint64_t deadline;
  };
  struct InFlight {};
  struct Done {
    Status status;
  };
  std::variant<AwaitingEmpty, AwaitingWord, Countdown, InFlight, Done> state_;
  HWND window_;
  ITfThreadMgr &manager_;
  DWORD thread_;
  Status progress_;
  void *test_context_ = nullptr;
  HRESULT (*acquire_)(void *, Native &) = nullptr;
  detail::PublicWord (*read_)(void *, const Probe &) = nullptr;
  uint64_t (*clock_)(void *, uint64_t) = nullptr;
  bool Running() const noexcept { return std::holds_alternative<InFlight>(state_); }
  Status Value() const noexcept {
    if (const auto *done = std::get_if<Done>(&state_))
      return done->status;
    Status result;
    result.stage = std::holds_alternative<Countdown>(state_) ? Stage::Countdown
                   : Running()                               ? Stage::Running
                                                             : Stage::Waiting;
    return result;
  }
  void Reject(Failure failure, HRESULT result = S_OK) noexcept {
    if (std::holds_alternative<Done>(state_))
      return;
    auto held = std::move(state_);
    auto status = progress_;
    status.stage = Stage::Failed;
    status.failure = failure;
    status.native_result = static_cast<uint32_t>(result);
    state_.emplace<Done>(status);
  }
  bool Accept(Failure failure) noexcept {
    if (!Running())
      return false;
    if (failure == Failure::None)
      return true;
    Reject(failure);
    return false;
  }
  bool Result(HRESULT result, Failure failure) noexcept {
    if (!Running())
      return false;
    if (result == S_OK)
      return true;
    Reject(failure, result);
    return false;
  }
  detail::PublicWord ReadWord() {
    return read_ ? read_(test_context_, *this)
                 : detail::ReadPublicWord([&] { return Running(); },
                                          [&] { return detail::PublicControls(window_, thread_); },
                                          [&](int id, UINT message, WPARAM w, LPARAM l) {
                                            return SendMessageW(GetDlgItem(window_, id), message, w,
                                                                l);
                                          });
  }
  struct Discovery {
    ComPtr<ITfFunctionProvider> provider;
    ComPtr<IUnknown> identity, function, reader_identity;
    ComPtr<diagnostics::SnapshotReaderV2> reader;
  };
  Failure Discover(Endpoint &endpoint, Discovery &d, bool bind) {
    HRESULT result = manager_.GetFunctionProvider(service_id, &d.provider);
    if (!Running())
      return Failure::Closed;
    if (result != S_OK || !d.provider)
      return result == TF_E_NOPROVIDER ? Failure::ProviderMissing : Failure::InvalidEvidence;
    result = d.provider.As(&d.identity);
    if (!Running() || result != S_OK || !d.identity)
      return Failure::InvalidEvidence;
    if (!bind)
      return endpoint.identity.Get() == d.identity.Get() ? Failure::None : Failure::ProviderChanged;
    result = d.provider->GetFunction(GUID_NULL, diagnostics::snapshot_v2_iid, &d.function);
    if (!Running() || result != S_OK || !d.function)
      return Failure::InvalidEvidence;
    result = d.function->QueryInterface(diagnostics::snapshot_v2_iid,
                                        reinterpret_cast<void **>(d.reader.GetAddressOf()));
    if (!Running() || result != S_OK || !d.reader)
      return Failure::InvalidEvidence;
    result = d.reader.As(&d.reader_identity);
    if (!Running() || result != S_OK || !d.reader_identity)
      return Failure::InvalidEvidence;
    if (d.identity.Get() != d.reader_identity.Get())
      return Failure::ProviderChanged;
    endpoint.identity = std::move(d.identity);
    endpoint.reader = std::move(d.reader);
    return Failure::None;
  }
  Failure Copy(const Endpoint &endpoint, SnapshotV2 &out) {
    const auto result = endpoint.reader->Copy(sizeof(out), diagnostics::version_v2, &out);
    if (!Running())
      return Failure::Closed;
    if (result != S_OK)
      return result == E_PENDING ? Failure::Busy : Failure::InvalidEvidence;
    const auto failure = detail::Active(out);
    if (failure != Failure::None)
      return failure;
    return !endpoint.activation || endpoint.activation == out.base.activation
               ? Failure::None
               : Failure::ActivationChanged;
  }
  bool Prepare(Native &native) {
    if (native.languages && native.profiles)
      return true;
    if (acquire_) {
      if (!Result(acquire_(test_context_, native), Failure::SetupUnavailable))
        return false;
    } else {
      if (!Result(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&native.languages)),
                  Failure::SetupUnavailable))
        return false;
      if (!Result(native.languages.As(&native.profiles), Failure::SetupUnavailable))
        return false;
    }
    return Accept(native.languages && native.profiles ? Failure::None : Failure::SetupUnavailable);
  }
  bool Target(Native &native) {
    TF_INPUTPROCESSORPROFILE target{};
    const auto &ru = profiles[1];
    if (!Result(native.profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, ru.language, service_id,
                                            ru.id, nullptr, &target),
                Failure::TargetUnavailable))
      return false;
    if (!Accept(target.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR &&
                        target.clsid == service_id && target.guidProfile == ru.id &&
                        target.langid == ru.language && target.catid == GUID_TFCAT_TIP_KEYBOARD
                    ? Failure::None
                    : Failure::TargetUnavailable))
      return false;
    BOOL enabled = FALSE;
    return Result(
               native.languages->IsEnabledLanguageProfile(service_id, ru.language, ru.id, &enabled),
               Failure::TargetUnavailable) &&
           Accept(enabled == TRUE ? Failure::None : Failure::TargetUnavailable);
  }
  struct Selection {
    LANGID language = 0;
    TF_INPUTPROCESSORPROFILE profile{};
  };
  bool ReadSelection(Native &native, Selection &out, bool unbound = false) {
    const auto language = native.languages->GetCurrentLanguage(&out.language);
    if (!Running() || (unbound && language != S_OK) || !Result(language, Failure::WrongSource))
      return false;
    const auto profile = native.profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &out.profile);
    return Running() && !(unbound && profile != S_OK) && Result(profile, Failure::TargetNotActive);
  }
  void TickBatch(uint64_t now) {
    Capabilities caps;
    SnapshotV2 empty, last;
    uint32_t prefix = 0;
    uint64_t deadline = 0;
    const bool binding = std::holds_alternative<AwaitingEmpty>(state_);
    const bool countdown = std::holds_alternative<Countdown>(state_);
    if (auto *waiting = std::get_if<AwaitingEmpty>(&state_))
      caps.native = std::move(waiting->native);
    if (auto *waiting = std::get_if<AwaitingWord>(&state_)) {
      caps = std::move(waiting->caps);
      empty = waiting->empty;
      last = waiting->last;
      prefix = waiting->prefix;
    }
    if (auto *armed = std::get_if<Countdown>(&state_)) {
      caps = std::move(armed->caps);
      last = armed->ready;
      deadline = armed->deadline;
    }
    state_.emplace<InFlight>();
    if (!Accept(GetCurrentThreadId() == thread_ ? Failure::None : Failure::WrongThread) ||
        !Prepare(caps.native))
      return;
    if (countdown && now > deadline && now - deadline > late_ms) {
      Reject(Failure::Late);
      return;
    }
    Selection selection;
    if (!ReadSelection(caps.native, selection, binding)) {
      if (binding && Running())
        state_.emplace<AwaitingEmpty>(std::move(caps.native));
      return;
    }
    if (selection.language != profiles[0].language ||
        !detail::Exact(selection.profile, profiles[0])) {
      if (binding)
        state_.emplace<AwaitingEmpty>(std::move(caps.native));
      else
        Reject(Failure::WrongSource);
      return;
    }
    if (countdown && now >= deadline && !Target(caps.native))
      return;
    Discovery discovery;
    auto failure = Discover(caps.endpoint, discovery, binding);
    if (binding && failure == Failure::ProviderMissing && Running()) {
      state_.emplace<AwaitingEmpty>(std::move(caps.native));
      return;
    }
    if (!Accept(failure))
      return;
    const auto word = ReadWord();
    if (!Running())
      return;
    SnapshotV2 snapshot;
    if (!Accept(Copy(caps.endpoint, snapshot)))
      return;
    if (!Accept(snapshot.base.mode == KR_EN ? Failure::None : Failure::WrongSource))
      return;
    if (binding) {
      if (!Accept(snapshot.lifecycle.allocated == 0 && !snapshot.lifecycle.owned_present
                      ? Failure::None
                      : Failure::OwnershipLost))
        return;
      if (word.failure == Failure::FocusChanged) {
        state_.emplace<AwaitingEmpty>(std::move(caps.native));
        return;
      }
      if (!Accept(word.failure) || !Accept(word.prefix == 0 ? Failure::None : Failure::WordChanged))
        return;
      caps.endpoint.activation = snapshot.base.activation;
      state_.emplace<AwaitingWord>(std::move(caps), snapshot, snapshot, 0);
      return;
    }
    if (countdown) {
      if (!Accept(word.failure) ||
          !Accept(word.prefix == 6 ? Failure::None : Failure::WordChanged) ||
          !Accept(detail::Armed(last, snapshot)))
        return;
      if (now < deadline)
        state_.emplace<Countdown>(std::move(caps), last, deadline);
      else {
        const auto latest = clock_ ? clock_(test_context_, now) : GetTickCount64();
        if (!Accept(latest >= now && latest >= deadline && latest - deadline <= late_ms
                        ? Failure::None
                        : Failure::Late))
          return;
        RunOnce(caps, snapshot);
      }
      return;
    }
    if (!Accept(detail::History(last, snapshot)) || !Accept(detail::WordHistory(empty, snapshot)))
      return;
    if (!snapshot.lifecycle.allocated && word.failure == Failure::FocusChanged) {
      state_.emplace<AwaitingWord>(std::move(caps), empty, snapshot, 0);
      return;
    }
    if (!Accept(word.failure) ||
        !Accept(word.prefix >= prefix ? Failure::None : Failure::WordChanged) ||
        !Accept((word.prefix != 0) == (snapshot.lifecycle.owned_present != 0)
                    ? Failure::None
                    : Failure::OwnershipLost))
      return;
    if (!snapshot.lifecycle.allocated) {
      state_.emplace<AwaitingWord>(std::move(caps), snapshot, snapshot, 0);
    } else if (word.prefix == 6) {
      if (!Accept(now <= UINT64_MAX - countdown_ms ? Failure::None : Failure::Late))
        return;
      state_.emplace<Countdown>(std::move(caps), snapshot, now + countdown_ms);
    } else {
      state_.emplace<AwaitingWord>(std::move(caps), empty, snapshot, word.prefix);
    }
  }
  void RunOnce(Capabilities &caps, const SnapshotV2 &before) {
    progress_.language_calls = 1;
    if (!Result(caps.native.languages->ChangeCurrentLanguage(profiles[1].language),
                Failure::NativeLanguageFailed))
      return;
    Selection selected;
    if (!ReadSelection(caps.native, selected) ||
        !Accept(selected.language == profiles[1].language ? Failure::None
                                                          : Failure::LanguageNotChanged))
      return;
    if (!Accept(selected.profile.langid == selected.language &&
                        selected.profile.catid == GUID_TFCAT_TIP_KEYBOARD &&
                        (selected.profile.dwFlags & TF_IPP_FLAG_ACTIVE)
                    ? Failure::None
                    : Failure::WrongSource))
      return;
    Discovery middle_discovery;
    if (!Accept(Discover(caps.endpoint, middle_discovery, false)))
      return;
    const auto word = ReadWord();
    if (!Accept(word.failure) || !Accept(word.prefix == 6 ? Failure::None : Failure::WordChanged))
      return;
    SnapshotV2 middle;
    if (!Accept(Copy(caps.endpoint, middle)))
      return;
    detail::Terminal terminal;
    if (!Accept(detail::Interval(before, middle, terminal)))
      return;
    const bool exact_ru = detail::Exact(selected.profile, profiles[1]);
    if (!Accept(!exact_ru || middle.base.mode == KR_RU ? Failure::None : Failure::ServiceNotRu))
      return;
    if (exact_ru && !Accept(terminal.sequence ? Failure::None : Failure::NoTerminal))
      return;
    if (!exact_ru) {
      if (!Accept(middle.base.mode != KR_RU ? Failure::None : Failure::WrongSource))
        return;
      progress_.activation_calls = 1;
      if (!Result(caps.native.profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,
                                                        profiles[1].language, service_id,
                                                        profiles[1].id, nullptr, 0),
                  Failure::NativeActivationFailed))
        return;
    }
    Selection final_selection;
    if (!ReadSelection(caps.native, final_selection) ||
        !Accept(final_selection.language == profiles[1].language &&
                        detail::Exact(final_selection.profile, profiles[1])
                    ? Failure::None
                    : Failure::TargetNotActive))
      return;
    Discovery final_discovery;
    if (!Accept(Discover(caps.endpoint, final_discovery, false)))
      return;
    const auto final_word = ReadWord();
    if (!Accept(final_word.failure) ||
        !Accept(final_word.prefix == 6 ? Failure::None : Failure::WordChanged))
      return;
    SnapshotV2 after;
    if (!Accept(Copy(caps.endpoint, after)))
      return;
    Status status = progress_;
    if (!Accept(detail::Evaluate(before, middle, after, status)))
      return;
    status.stage = Stage::Verified;
    if (!Accept(Valid(status) ? Failure::None : Failure::InvalidEvidence))
      return;
    state_.emplace<Done>(status);
  }
};

inline bool Matches(const Status &result, const diagnostics::PublicationV2 &first,
                    const diagnostics::PublicationV2 &second) noexcept {
  using namespace diagnostics;
  if (!Valid(result) || result.stage != Stage::Verified || !Comparable(first, second) ||
      first.saturated || second.saturated || !detail::SameInput(first.snapshot, second.snapshot))
    return false;
  for (const auto *publication : {&first, &second}) {
    const auto &s = publication->snapshot;
    if (detail::Active(s) != Failure::None || s.base.mode != KR_RU ||
        s.base.activation != result.activation || s.lifecycle.owned_present ||
        s.lifecycle.allocated != result.serial || s.lifecycle.sequence < result.after)
      return false;
  }
  return true;
}
} // namespace keyrill::source_proof
