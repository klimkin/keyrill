//! Content-free diagnostic counters and bounded lifecycle and command journals.

use crate::vocabulary_contract::{Error, Status};
use windows_core::{GUID, HRESULT};

pub(crate) const VERSION: u32 = 1;
pub(crate) const VERSION_V2: u32 = 2;
pub(crate) const RUNTIME_VERSION: u32 = 1;
pub(crate) const SNAPSHOT_IID: GUID = GUID::from_u128(0xf75f909b_293a_4a7b_a172_f5a061880352);
pub(crate) const SNAPSHOT_V2_IID: GUID = GUID::from_u128(0x4f89429e_a955_4885_bb33_c78ad1982481);
pub(crate) const RUNTIME_IID: GUID = GUID::from_u128(0xcda9ed25_1dbc_4fb6_b2a5_bc132945e082);
pub(crate) const LIFECYCLE_CAPACITY: usize = 16;
pub(crate) const OBSERVATION_CAPACITY: usize = 32;

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum Stage {
    #[default]
    Test,
    Key,
    Keyboard,
    Profile,
    Layout,
    Mode,
    Handle,
    Request,
    Session,
    Selection,
    Focus,
    Context,
    ProofWindow,
    Status,
    ContextDisabled,
    EmptyContext,
    ThreadDisabled,
    AppProperty,
    AppValue,
    ScopeInterface,
    Scopes,
    Foreign,
    Boundary,
    Plan,
    Apply,
    Acknowledge,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum Cause {
    #[default]
    None,
    Native,
    Predicate,
    Inactive,
    Stopped,
    ModeUnset,
    Reentry,
    Stale,
    Missing,
    WrongType,
    WrongService,
    NotActive,
    WrongProfile,
    Substitute,
    Exception,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum Counter {
    #[default]
    TestEntered,
    Interested,
    KeyEntered,
    Consumed,
    HandleEntered,
    Requested,
    SessionEntered,
    SessionCompleted,
    Attempted,
    Verified,
    Refused,
    Unknown,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct Refusal {
    pub(crate) present: u32,
    pub(crate) stage: Stage,
    pub(crate) cause: Cause,
    pub(crate) result: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct Snapshot {
    pub(crate) activation: u32,
    pub(crate) active: u32,
    pub(crate) stopped: u32,
    pub(crate) mode: u32,
    pub(crate) saturated: u32,
    pub(crate) request_present: u32,
    pub(crate) request_result: u32,
    pub(crate) session_present: u32,
    pub(crate) session_result: u32,
    pub(crate) counts: [u32; 12],
    pub(crate) refusals: [u32; 26],
    pub(crate) first: Refusal,
    pub(crate) latest: Refusal,
}

pub(crate) fn increment(value: &mut u32, saturated: &mut u32) {
    *value = value.saturating_add(1);
    if *value == u32::MAX {
        *saturated = 1;
    }
}

pub(crate) fn count(snapshot: &mut Snapshot, counter: Counter) {
    increment(
        &mut snapshot.counts[counter as usize],
        &mut snapshot.saturated,
    );
}

pub(crate) fn refuse(snapshot: &mut Snapshot, stage: Stage, cause: Cause, result: HRESULT) {
    increment(
        &mut snapshot.refusals[stage as usize],
        &mut snapshot.saturated,
    );
    snapshot.latest = Refusal {
        present: 1,
        stage,
        cause,
        result: result.0 as u32,
    };
    if snapshot.first.present == 0 {
        snapshot.first = snapshot.latest;
    }
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum LifecycleKind {
    #[default]
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
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum LifecycleCause {
    #[default]
    None,
    ForegroundLost,
    DocumentFocus,
    DocumentUninit,
    ContextPush,
    ContextPop,
    ProfileChanged,
    KeyAdmission,
    Deactivate,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum Unresolved {
    #[default]
    None,
    NullArgument,
    IdentityFailure,
    Stale,
    Exception,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct LifecycleEvent {
    pub(crate) sequence: u32,
    pub(crate) kind: LifecycleKind,
    pub(crate) subject: u32,
    pub(crate) cause: LifecycleCause,
    pub(crate) unresolved: Unresolved,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct LifecycleSnapshot {
    pub(crate) sequence: u32,
    pub(crate) allocated: u32,
    pub(crate) saturated: u32,
    pub(crate) count: u32,
    pub(crate) owned_present: u32,
    pub(crate) owned_serial: u32,
    pub(crate) events: [LifecycleEvent; LIFECYCLE_CAPACITY],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct SnapshotV2 {
    pub(crate) base: Snapshot,
    pub(crate) lifecycle: LifecycleSnapshot,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct RuntimeSnapshot {
    pub(crate) activation: u32,
    pub(crate) requested: u32,
    pub(crate) effective: u32,
    pub(crate) model_ready: u32,
    pub(crate) revision: u64,
    pub(crate) error: Error,
    pub(crate) configuration_error: Error,
    pub(crate) sources: [Status; 2],
    pub(crate) source_errors: [Error; 2],
}

#[derive(Default)]
pub(crate) struct LifecycleJournal {
    window: LifecycleSnapshot,
}

impl LifecycleJournal {
    pub(crate) fn new(sequence: u32, allocated: u32) -> Self {
        Self {
            window: LifecycleSnapshot {
                sequence,
                allocated,
                saturated: u32::from(sequence == u32::MAX || allocated == u32::MAX),
                ..LifecycleSnapshot::default()
            },
        }
    }

    pub(crate) fn sequence(&self) -> u32 {
        self.window.sequence
    }

    pub(crate) fn acquire(&mut self) -> u32 {
        let serial = if self.window.allocated == u32::MAX {
            self.window.saturated = 1;
            0
        } else {
            increment(&mut self.window.allocated, &mut self.window.saturated);
            self.window.allocated
        };
        self.record(
            LifecycleKind::Acquired,
            serial,
            LifecycleCause::None,
            Unresolved::None,
        );
        serial
    }

    pub(crate) fn record(
        &mut self,
        kind: LifecycleKind,
        subject: u32,
        cause: LifecycleCause,
        unresolved: Unresolved,
    ) {
        if self.window.sequence == u32::MAX {
            self.window.saturated = 1;
            return;
        }
        increment(&mut self.window.sequence, &mut self.window.saturated);
        if self.window.count as usize == LIFECYCLE_CAPACITY {
            self.window.events.copy_within(1.., 0);
        } else {
            self.window.count += 1;
        }
        self.window.events[self.window.count as usize - 1] = LifecycleEvent {
            sequence: self.window.sequence,
            kind,
            subject,
            cause,
            unresolved,
        };
    }

    pub(crate) fn copy(&self, owned: bool, serial: u32) -> LifecycleSnapshot {
        LifecycleSnapshot {
            owned_present: u32::from(owned),
            owned_serial: serial,
            ..self.window
        }
    }
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum CommandRoute {
    #[default]
    Ordinary,
    Preserved,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum ManualIntent {
    #[default]
    Convert,
    Reverse,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum ReleaseDelivery {
    #[default]
    Test,
    Actual,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum ReleaseModifier {
    #[default]
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
    ControlUnresolved,
    AltUnresolved,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum ReleaseContext {
    #[default]
    Unchecked,
    Matched,
    Refused,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum ObserverStage {
    #[default]
    Arrival,
    Admitted,
    Refused,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum CommandCompletion {
    #[default]
    Refused,
    Unknown,
    Reserved,
    ManualMark,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct ModifierFacts {
    pub(crate) left_control: bool,
    pub(crate) left_alt: bool,
    pub(crate) right_control: bool,
    pub(crate) right_alt: bool,
    pub(crate) any_shift: bool,
    pub(crate) any_windows: bool,
}

impl ModifierFacts {
    pub(crate) fn exact(self) -> bool {
        self.left_control
            && self.left_alt
            && !self.right_control
            && !self.right_alt
            && !self.any_shift
            && !self.any_windows
    }

    pub(crate) fn project(state: &[u8; 256]) -> Self {
        Self {
            left_control: state[0xa2] & 0x80 != 0,
            left_alt: state[0xa4] & 0x80 != 0,
            right_control: state[0xa3] & 0x80 != 0,
            right_alt: state[0xa5] & 0x80 != 0,
            any_shift: (state[0x10] | state[0xa0] | state[0xa1]) & 0x80 != 0,
            any_windows: (state[0x5b] | state[0x5c]) & 0x80 != 0,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum ModifierKnowledge {
    #[default]
    Unknown,
    Known(ModifierFacts),
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct ObservationSequence {
    pub(crate) value: u32,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct LifecycleCursor {
    pub(crate) value: u32,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct CommandId {
    pub(crate) arrival: ObservationSequence,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct ObservedOwnership {
    pub(crate) present: bool,
    pub(crate) composition_serial: u32,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct ObservationStamp {
    pub(crate) after: LifecycleCursor,
    pub(crate) owned: ObservedOwnership,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum RecognizedCommand {
    Preserved {
        intent: ManualIntent,
    },
    Ordinary {
        intent: ManualIntent,
        modifiers: ModifierFacts,
    },
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct CommandIdentity {
    pub(crate) id: CommandId,
    pub(crate) route: CommandRoute,
    pub(crate) intent: ManualIntent,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct CommandTicket {
    pub(crate) identity: Option<CommandIdentity>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum CommandStage {
    Arrival(ModifierKnowledge),
    AdmissionRefused(ModifierKnowledge),
    ExactModifiersRefused(ModifierFacts),
    DispatchRefused(ModifierFacts),
    ExecutorDispatch(ModifierFacts),
    Exception(ModifierKnowledge),
}
impl CommandStage {
    pub(crate) fn knowledge(self) -> ModifierKnowledge {
        match self {
            Self::Arrival(value) | Self::AdmissionRefused(value) | Self::Exception(value) => value,
            Self::ExactModifiersRefused(value)
            | Self::DispatchRefused(value)
            | Self::ExecutorDispatch(value) => ModifierKnowledge::Known(value),
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct CommandObservation {
    pub(crate) command: CommandIdentity,
    pub(crate) stage: CommandStage,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct ReleaseObservation {
    pub(crate) delivery: ReleaseDelivery,
    pub(crate) modifier: ReleaseModifier,
    pub(crate) context: ReleaseContext,
    pub(crate) consumed: bool,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct ObserverObservation {
    pub(crate) stage: ObserverStage,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct CompletionObservation {
    pub(crate) command: CommandIdentity,
    pub(crate) witnessed: bool,
    pub(crate) completion: CommandCompletion,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum ObservationBody {
    Command(CommandObservation),
    Release(ReleaseObservation),
    Observer(ObserverObservation),
    Completion(CompletionObservation),
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct ObservationEvent {
    pub(crate) sequence: ObservationSequence,
    pub(crate) stamp: ObservationStamp,
    pub(crate) body: ObservationBody,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct ObservationWindow {
    pub(crate) sequence: ObservationSequence,
    pub(crate) saturated: bool,
    pub(crate) count: u32,
    pub(crate) events: [Option<ObservationEvent>; OBSERVATION_CAPACITY],
}
#[derive(Default)]
pub(crate) struct ObservationJournal {
    window: ObservationWindow,
}
impl ObservationJournal {
    pub(crate) fn new(sequence: u32) -> Self {
        Self {
            window: ObservationWindow {
                sequence: ObservationSequence { value: sequence },
                saturated: sequence == u32::MAX,
                ..ObservationWindow::default()
            },
        }
    }

    pub(crate) fn begin(
        &mut self,
        recognized: RecognizedCommand,
        stamp: ObservationStamp,
    ) -> CommandTicket {
        if self.window.saturated {
            return CommandTicket::default();
        }
        let (route, intent, knowledge) = match recognized {
            RecognizedCommand::Preserved { intent } => {
                (CommandRoute::Preserved, intent, ModifierKnowledge::Unknown)
            }
            RecognizedCommand::Ordinary { intent, modifiers } => (
                CommandRoute::Ordinary,
                intent,
                ModifierKnowledge::Known(modifiers),
            ),
        };
        let identity = CommandIdentity {
            id: CommandId {
                arrival: ObservationSequence {
                    value: self.window.sequence.value + 1,
                },
            },
            route,
            intent,
        };
        self.append(
            ObservationBody::Command(CommandObservation {
                command: identity,
                stage: CommandStage::Arrival(knowledge),
            }),
            stamp,
        );
        CommandTicket {
            identity: Some(identity),
        }
    }

    pub(crate) fn record(
        &mut self,
        ticket: CommandTicket,
        stage: CommandStage,
        stamp: ObservationStamp,
    ) {
        if let Some(command) = ticket.identity {
            self.append(
                ObservationBody::Command(CommandObservation { command, stage }),
                stamp,
            );
        }
    }
    pub(crate) fn release(&mut self, value: ReleaseObservation, stamp: ObservationStamp) {
        self.append(ObservationBody::Release(value), stamp);
    }
    pub(crate) fn observer(&mut self, value: ObserverObservation, stamp: ObservationStamp) {
        self.append(ObservationBody::Observer(value), stamp);
    }
    pub(crate) fn complete(&mut self, value: CompletionObservation, stamp: ObservationStamp) {
        self.append(ObservationBody::Completion(value), stamp);
    }
    pub(crate) fn copy(&self) -> ObservationWindow {
        self.window
    }
    fn append(&mut self, body: ObservationBody, stamp: ObservationStamp) {
        if self.window.saturated {
            return;
        }
        self.window.sequence.value += 1;
        self.window.saturated = self.window.sequence.value == u32::MAX;
        if self.window.count as usize == OBSERVATION_CAPACITY {
            self.window.events.copy_within(1.., 0);
        } else {
            self.window.count += 1;
        }
        self.window.events[self.window.count as usize - 1] = Some(ObservationEvent {
            sequence: self.window.sequence,
            stamp,
            body,
        });
    }
}

pub(crate) fn classify_relevant_release(key: usize, flags: isize) -> Option<ReleaseModifier> {
    let extended = flags as usize & (1 << 24) != 0;
    match (key, extended) {
        (0x11 | 0xa3, true) => Some(ReleaseModifier::RightControl),
        (0x11 | 0xa2, false) => Some(ReleaseModifier::LeftControl),
        (0x12 | 0xa5, true) => Some(ReleaseModifier::RightAlt),
        (0x12 | 0xa4, false) => Some(ReleaseModifier::LeftAlt),
        (0xa2, true) | (0xa3, false) => Some(ReleaseModifier::ControlUnresolved),
        (0xa4, true) | (0xa5, false) => Some(ReleaseModifier::AltUnresolved),
        _ => None,
    }
}

const _: () = {
    assert!(size_of::<Snapshot>() == 220);
    assert!(std::mem::offset_of!(Snapshot, counts) == 36);
    assert!(std::mem::offset_of!(Snapshot, first) == 188);
    assert!(size_of::<LifecycleEvent>() == 20);
    assert!(size_of::<LifecycleSnapshot>() == 344);
    assert!(size_of::<SnapshotV2>() == 564);
    assert!(std::mem::offset_of!(SnapshotV2, lifecycle) == 220);
    assert!(size_of::<RuntimeSnapshot>() == 48);
    assert!(std::mem::offset_of!(RuntimeSnapshot, revision) == 16);
};
