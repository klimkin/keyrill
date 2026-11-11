#pragma once
#include "keys.h"
#include "lifecycle_diagnostics.h"
#include <optional>

namespace keyrill::diagnostics {
struct ObservationSequence {
  uint32_t value = 0;
  bool operator==(const ObservationSequence &) const = default;
};
struct LifecycleCursor {
  uint32_t value = 0;
  bool operator==(const LifecycleCursor &) const = default;
};
struct CommandId {
  ObservationSequence arrival;
  bool operator==(const CommandId &) const = default;
};
enum class CommandRoute : uint32_t { Ordinary, Preserved };
enum class ReleaseDelivery : uint32_t { Test, Actual };
enum class ReleaseModifier : uint32_t {
  LeftControl,
  RightControl,
  LeftAlt,
  RightAlt,
  ControlUnresolved,
  AltUnresolved
};
struct ModifierFacts {
  bool left_control = false;
  bool left_alt = false;
  bool right_control = false;
  bool right_alt = false;
  bool any_shift = false;
  bool any_windows = false;
  bool operator==(const ModifierFacts &) const = default;
};
struct UnknownModifiers {
  bool operator==(const UnknownModifiers &) const = default;
};
using ModifierKnowledge = std::variant<UnknownModifiers, ModifierFacts>;
inline bool Exact(const ModifierFacts &facts) noexcept {
  return facts.left_control && facts.left_alt && !facts.right_control && !facts.right_alt &&
         !facts.any_shift && !facts.any_windows;
}
inline ModifierFacts ProjectModifiers(const BYTE (&state)[256]) noexcept {
  return {(state[VK_LCONTROL] & 0x80) != 0,
          (state[VK_LMENU] & 0x80) != 0,
          (state[VK_RCONTROL] & 0x80) != 0,
          (state[VK_RMENU] & 0x80) != 0,
          ((state[VK_SHIFT] | state[VK_LSHIFT] | state[VK_RSHIFT]) & 0x80) != 0,
          ((state[VK_LWIN] | state[VK_RWIN]) & 0x80) != 0};
}
inline std::optional<ReleaseModifier> ClassifyRelevantRelease(WPARAM key, LPARAM flags) noexcept {
  const bool extended = (static_cast<ULONG_PTR>(flags) & (ULONG_PTR{1} << 24)) != 0;
  switch (key) {
  case VK_CONTROL:
    return extended ? ReleaseModifier::RightControl : ReleaseModifier::LeftControl;
  case VK_MENU:
    return extended ? ReleaseModifier::RightAlt : ReleaseModifier::LeftAlt;
  case VK_LCONTROL:
    return extended ? ReleaseModifier::ControlUnresolved : ReleaseModifier::LeftControl;
  case VK_RCONTROL:
    return extended ? ReleaseModifier::RightControl : ReleaseModifier::ControlUnresolved;
  case VK_LMENU:
    return extended ? ReleaseModifier::AltUnresolved : ReleaseModifier::LeftAlt;
  case VK_RMENU:
    return extended ? ReleaseModifier::RightAlt : ReleaseModifier::AltUnresolved;
  default:
    return std::nullopt;
  }
}
struct ObservedOwnership {
  bool present = false;
  uint32_t composition_serial = 0;
  bool operator==(const ObservedOwnership &) const = default;
};
struct ObservationStamp {
  LifecycleCursor after;
  ObservedOwnership owned;
  bool operator==(const ObservationStamp &) const = default;
};
struct PreservedIntent {
  ManualIntent intent;
};
struct OrdinaryIntent {
  ManualIntent intent;
  ModifierFacts modifiers;
};
using RecognizedCommand = std::variant<PreservedIntent, OrdinaryIntent>;
struct CommandIdentity {
  CommandId id;
  CommandRoute route;
  ManualIntent intent;
  bool operator==(const CommandIdentity &) const = default;
};
struct CommandTicket {
  std::optional<CommandIdentity> identity;
};
struct CommandArrival {
  ModifierKnowledge modifiers;
  bool operator==(const CommandArrival &) const = default;
};
struct AdmissionRefused {
  ModifierKnowledge modifiers;
  bool operator==(const AdmissionRefused &) const = default;
};
struct ExactModifiersRefused {
  ModifierFacts modifiers;
  bool operator==(const ExactModifiersRefused &) const = default;
};
struct DispatchRefused {
  ModifierFacts modifiers;
  bool operator==(const DispatchRefused &) const = default;
};
struct ExecutorDispatch {
  ModifierFacts modifiers;
  bool operator==(const ExecutorDispatch &) const = default;
};
struct CommandException {
  ModifierKnowledge modifiers;
  bool operator==(const CommandException &) const = default;
};
using CommandProgress = std::variant<AdmissionRefused, ExactModifiersRefused, DispatchRefused,
                                     ExecutorDispatch, CommandException>;
using CommandStage = std::variant<CommandArrival, AdmissionRefused, ExactModifiersRefused,
                                  DispatchRefused, ExecutorDispatch, CommandException>;
struct CommandObservation {
  CommandIdentity command;
  CommandStage stage;
  bool operator==(const CommandObservation &) const = default;
};
inline ModifierKnowledge Knowledge(const CommandStage &stage) noexcept {
  return std::visit([](const auto &value) -> ModifierKnowledge { return value.modifiers; }, stage);
}
struct ReleaseObservation {
  ReleaseDelivery delivery;
  ReleaseModifier modifier;
  enum class Context : uint32_t { Unchecked, Matched, Refused };
  Context context = Context::Unchecked;
  bool consumed = false;
  bool operator==(const ReleaseObservation &) const = default;
};
enum class ObserverStage : uint32_t { Arrival, Admitted, Refused };
struct ObserverObservation {
  ObserverStage stage;
  bool operator==(const ObserverObservation &) const = default;
};
enum class CommandCompletion : uint32_t { Refused, Unknown, Reserved, ManualMark };
struct CompletionObservation {
  CommandIdentity command;
  bool witnessed;
  CommandCompletion completion;
  bool operator==(const CompletionObservation &) const = default;
};
struct ObservationEvent {
  ObservationSequence sequence;
  ObservationStamp stamp;
  std::variant<CommandObservation, ReleaseObservation, ObserverObservation, CompletionObservation>
      body;
  bool operator==(const ObservationEvent &) const = default;
};
inline constexpr size_t observation_capacity = 32;
struct ObservationWindow {
  ObservationSequence sequence;
  bool saturated = false;
  uint32_t count = 0;
  std::array<std::optional<ObservationEvent>, observation_capacity> events{};
  bool operator==(const ObservationWindow &) const = default;
};
class ObservationJournal {
public:
  explicit ObservationJournal(uint32_t sequence = 0) noexcept {
    window_.sequence.value = sequence;
    window_.saturated = sequence == UINT32_MAX;
  }
  CommandTicket Begin(const RecognizedCommand &recognized, ObservationStamp stamp) noexcept {
    if (window_.saturated)
      return {};
    const auto route = std::holds_alternative<PreservedIntent>(recognized) ? CommandRoute::Preserved
                                                                           : CommandRoute::Ordinary;
    const auto intent = std::visit([](const auto &value) { return value.intent; }, recognized);
    const CommandIdentity identity{{{window_.sequence.value + 1}}, route, intent};
    ModifierKnowledge knowledge = UnknownModifiers{};
    if (const auto *ordinary = std::get_if<OrdinaryIntent>(&recognized))
      knowledge = ordinary->modifiers;
    Append({{}, stamp, CommandObservation{identity, CommandArrival{knowledge}}});
    return {identity};
  }
  void Record(const CommandTicket &ticket, const CommandProgress &progress,
              ObservationStamp stamp) noexcept {
    if (!ticket.identity)
      return;
    const CommandStage stage =
        std::visit([](const auto &value) -> CommandStage { return value; }, progress);
    Append({{}, stamp, CommandObservation{*ticket.identity, stage}});
  }
  void Record(ReleaseObservation release, ObservationStamp stamp) noexcept {
    Append({{}, stamp, release});
  }
  void Record(ObserverObservation observer, ObservationStamp stamp) noexcept {
    Append({{}, stamp, observer});
  }
  void Record(CompletionObservation completion, ObservationStamp stamp) noexcept {
    Append({{}, stamp, completion});
  }
  ObservationWindow Copy() const noexcept { return window_; }

private:
  void Append(ObservationEvent event) noexcept {
    if (window_.saturated)
      return;
    event.sequence.value = ++window_.sequence.value;
    window_.saturated = window_.sequence.value == UINT32_MAX;
    if (window_.count == observation_capacity)
      std::move(window_.events.begin() + 1, window_.events.end(), window_.events.begin());
    else
      ++window_.count;
    window_.events[window_.count - 1] = event;
  }
  ObservationWindow window_;
};
inline bool Valid(const ObservationWindow &window, const LifecycleSnapshot &lifecycle) noexcept {
  if (window.count !=
          (std::min)(window.sequence.value, static_cast<uint32_t>(observation_capacity)) ||
      window.saturated != (window.sequence.value == UINT32_MAX))
    return false;
  uint32_t cursor = 0;
  for (size_t i = 0; i != observation_capacity; ++i) {
    const auto &slot = window.events[i];
    if (i >= window.count) {
      if (slot)
        return false;
      continue;
    }
    if (!slot)
      return false;
    const auto &event = *slot;
    const auto &owned = event.stamp.owned;
    if (event.sequence.value != window.sequence.value - window.count + 1 + i ||
        event.stamp.after.value < cursor || event.stamp.after.value > lifecycle.sequence ||
        owned.composition_serial > lifecycle.allocated ||
        (!owned.present && owned.composition_serial) ||
        (owned.present && !owned.composition_serial && lifecycle.allocated != UINT32_MAX))
      return false;
    cursor = event.stamp.after.value;
    if (const auto *release = std::get_if<ReleaseObservation>(&event.body)) {
      if (release->delivery > ReleaseDelivery::Actual ||
          release->modifier > ReleaseModifier::AltUnresolved ||
          release->context > ReleaseObservation::Context::Refused ||
          (release->context != ReleaseObservation::Context::Unchecked &&
           release->modifier != ReleaseModifier::LeftAlt) ||
          (release->consumed && (release->modifier != ReleaseModifier::LeftAlt ||
                                 release->context != ReleaseObservation::Context::Matched)))
        return false;
      continue;
    }
    if (const auto *observer = std::get_if<ObserverObservation>(&event.body)) {
      if (observer->stage > ObserverStage::Refused)
        return false;
      continue;
    }
    if (const auto *completion = std::get_if<CompletionObservation>(&event.body)) {
      const auto &identity = completion->command;
      if (completion->completion > CommandCompletion::ManualMark || !identity.id.arrival.value ||
          identity.id.arrival.value >= event.sequence.value ||
          identity.route > CommandRoute::Preserved ||
          (identity.intent != ManualIntent::Convert && identity.intent != ManualIntent::Reverse))
        return false;
      const CommandObservation *dispatch = nullptr;
      for (size_t earlier = 0; earlier != i; ++earlier) {
        const auto &body = window.events[earlier]->body;
        if (const auto *other = std::get_if<CompletionObservation>(&body))
          if (other->command.id == identity.id)
            return false;
        if (const auto *other = std::get_if<CommandObservation>(&body))
          if (other->command.id == identity.id) {
            if (other->command != identity)
              return false;
            dispatch = other;
          }
      }
      if (dispatch ? !std::holds_alternative<ExecutorDispatch>(dispatch->stage)
                   : identity.id.arrival.value >= window.sequence.value - window.count + 1)
        return false;
      continue;
    }
    const auto &command = std::get<CommandObservation>(event.body);
    const auto &identity = command.command;
    const auto stage = command.stage.index();
    const auto knowledge = Knowledge(command.stage);
    const auto *facts = std::get_if<ModifierFacts>(&knowledge);
    if (!identity.id.arrival.value || identity.id.arrival.value > event.sequence.value ||
        identity.route > CommandRoute::Preserved ||
        (identity.intent != ManualIntent::Convert && identity.intent != ManualIntent::Reverse) ||
        (stage == 0 && identity.id.arrival != event.sequence) ||
        (stage != 0 && identity.id.arrival.value == event.sequence.value) ||
        (stage == 0 && identity.route == CommandRoute::Preserved && facts) ||
        (stage == 0 && identity.route == CommandRoute::Ordinary && (!facts || !Exact(*facts))) ||
        ((stage == 1 || stage == 2) && identity.route != CommandRoute::Preserved) ||
        (stage == 2 && (!facts || Exact(*facts))) ||
        ((stage == 3 || stage == 4) && (!facts || !Exact(*facts))) ||
        (identity.route == CommandRoute::Ordinary && (!facts || !Exact(*facts))))
      return false;
    const CommandObservation *previous = nullptr;
    for (size_t earlier = 0; earlier != i; ++earlier) {
      const auto *other = std::get_if<CommandObservation>(&window.events[earlier]->body);
      if (other && other->command.id == identity.id) {
        if (other->command != identity)
          return false;
        const auto old_knowledge = Knowledge(other->stage);
        if (const auto *known = std::get_if<ModifierFacts>(&old_knowledge))
          if (!facts || *known != *facts)
            return false;
        previous = other;
      }
    }
    if (previous) {
      const auto before = previous->stage.index();
      if (stage == 0 || (before != 0 && !(before == 4 && stage == 5)))
        return false;
    } else if (stage != 0 &&
               identity.id.arrival.value >= window.sequence.value - window.count + 1) {
      return false;
    }
  }
  return true;
}
} // namespace keyrill::diagnostics
