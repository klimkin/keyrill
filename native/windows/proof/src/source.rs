//! One-shot native EN-to-RU activation proof on the fixture's owned public word.
use crate::diagnostics::{Endpoint, SERVICE_ID, ServiceSnapshot};
use crate::native::wide;
use std::cell::{Cell, RefCell};
use windows::Win32::Foundation::*;
use windows::Win32::System::Com::*;
use windows::Win32::System::SystemInformation::GetTickCount64;
use windows::Win32::System::Threading::*;
use windows::Win32::UI::Controls::RichEdit::*;
use windows::Win32::UI::Input::KeyboardAndMouse::{GetFocus, IsWindowEnabled};
use windows::Win32::UI::TextServices::*;
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::{BOOL, GUID, HRESULT, Interface};

pub const MODE_ID: i32 = 109;
pub const RESULT_ID: i32 = 110;
pub const TITLE: &str = "Keyrill TSF owned source fixture";
pub const MODE_STATUS: &str = "Keyrill owned source v1; mode=ProveOwnedEnToRu";
pub const EN_PROFILE: GUID = GUID::from_u128(0x0d52a994_9eb5_44ab_b18f_603dc5f69671);
pub const RU_PROFILE: GUID = GUID::from_u128(0x4c1e583c_f8e5_4b7d_b66c_b7e7319aed51);
pub const STAGES: [&str; 5] = ["Waiting", "Countdown", "Running", "Verified", "Failed"];
pub const FAILURES: [&str; 27] = [
    "None",
    "Closed",
    "SetupUnavailable",
    "WrongThread",
    "ProviderMissing",
    "ProviderChanged",
    "ActivationChanged",
    "Busy",
    "InvalidEvidence",
    "Saturated",
    "WrongSource",
    "TargetUnavailable",
    "FocusChanged",
    "WordChanged",
    "OwnershipLost",
    "HistoryLost",
    "UnknownEffect",
    "NativeLanguageFailed",
    "LanguageNotChanged",
    "NativeActivationFailed",
    "TargetNotActive",
    "ServiceNotRu",
    "NoTerminal",
    "Detached",
    "UnresolvedTermination",
    "InputInterference",
    "Late",
];
#[repr(u32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Failure {
    Closed = 1,
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
}
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Status(pub [u32; 15]);
impl Status {
    pub fn valid(&self) -> bool {
        let s = self.0;
        if s[0] >= 5 || s[1] >= 27 || s[2] > 1 || s[3] > s[2] || s[12] > 2 || s[13] > 2 || s[14] > 2
        {
            return false;
        }
        match s[0] {
            3 => {
                s[1] == 0
                    && s[2] == 1
                    && s[4] == 0
                    && s[5] != 0
                    && s[5] != u32::MAX
                    && s[6] != 0
                    && s[6] != u32::MAX
                    && s[7] < s[10]
                    && s[10] <= s[9]
                    && s[7] <= s[8]
                    && s[8] <= s[9]
                    && s[9] - s[7] <= 16
                    && s[12] != 0
                    && s[13] == if s[10] <= s[8] { 1 } else { 2 }
                    && (s[13] != 2 || s[3] == 1)
                    && if s[11] != 0 {
                        s[11] > s[7]
                            && s[11] <= s[9]
                            && s[11] != s[10]
                            && s[14] == if s[10] < s[11] { 1 } else { 2 }
                    } else {
                        s[14] == 0
                    }
            }
            4 => s[1] != 0 && s[5..] == [0; 10],
            _ => s[1..] == [0; 14],
        }
    }
    pub fn format(&self) -> Option<String> {
        if !self.valid() {
            return None;
        }
        let s = self.0;
        let mut result = format!(
            "Keyrill source result v1; state={}; failure={}",
            STAGES[s[0] as usize], FAILURES[s[1] as usize]
        );
        for (label, value) in [
            "language_calls",
            "activation_calls",
            "native_result",
            "activation",
            "serial",
            "before",
            "after_language",
            "after",
            "terminal",
            "profile",
            "completion",
            "phase",
            "order",
        ]
        .into_iter()
        .zip(&s[2..])
        {
            result.push_str(&format!("; {label}={value}"));
        }
        Some(result)
    }
    pub fn parse(text: &str) -> Option<Self> {
        let fields: Vec<_> = text.split("; ").collect();
        if text.len() >= 1024 || fields.len() != 16 || fields[0] != "Keyrill source result v1" {
            return None;
        }
        let mut result = Self::default();
        result.0[0] = STAGES
            .iter()
            .position(|label| fields[1].strip_prefix("state=") == Some(label))?
            as u32;
        result.0[1] = FAILURES
            .iter()
            .position(|label| fields[2].strip_prefix("failure=") == Some(label))?
            as u32;
        for (value, field) in result.0[2..].iter_mut().zip(&fields[3..]) {
            *value = field.split_once('=')?.1.parse().ok()?;
        }
        (result.format().as_deref() == Some(text)).then_some(result)
    }
}
pub fn active(snapshot: &ServiceSnapshot) -> Result<(), Failure> {
    if !snapshot.valid() {
        return Err(Failure::InvalidEvidence);
    }
    if snapshot.saturated() {
        return Err(Failure::Saturated);
    }
    if snapshot.base[1] == 0 || snapshot.base[0] == 0 || snapshot.base[2] != 0 {
        return Err(Failure::InvalidEvidence);
    }
    if snapshot.base[20] != 0 {
        return Err(Failure::UnknownEffect);
    }
    Ok(())
}
pub fn same_input(a: &ServiceSnapshot, b: &ServiceSnapshot) -> bool {
    a.base[9..14] == b.base[9..14]
}
pub fn history(a: &ServiceSnapshot, b: &ServiceSnapshot) -> Result<(), Failure> {
    active(a)?;
    active(b)?;
    if a.base[0] != b.base[0] {
        return Err(Failure::ActivationChanged);
    }
    if a.base[9..47]
        .iter()
        .zip(&b.base[9..47])
        .any(|(old, new)| new < old)
    {
        return Err(Failure::InvalidEvidence);
    }
    if !crate::diagnostics::history(a, b) {
        return Err(Failure::HistoryLost);
    }
    Ok(())
}
pub fn armed(a: &ServiceSnapshot, b: &ServiceSnapshot) -> Result<(), Failure> {
    history(a, b)?;
    if !same_input(a, b) {
        return Err(Failure::InputInterference);
    }
    if b.base[3] != 1 {
        return Err(Failure::WrongSource);
    }
    if b.lifecycle[4] == 0 || b.lifecycle[5] != a.lifecycle[5] {
        return Err(Failure::OwnershipLost);
    }
    if a.lifecycle != b.lifecycle {
        return Err(Failure::InvalidEvidence);
    }
    Ok(())
}
pub fn word_history(empty: &ServiceSnapshot, now: &ServiceSnapshot) -> Result<(), Failure> {
    history(empty, now)?;
    let mut acquired = false;
    for event in now.events().filter(|event| event[0] > empty.lifecycle[0]) {
        if event[1] == 1 && !acquired && event[2] == 1 {
            acquired = true;
            continue;
        }
        if acquired || event[1] != 8 || [6, 7, 8].contains(&event[3]) {
            return Err(Failure::InvalidEvidence);
        }
    }
    if now.lifecycle[1] == 0 {
        return if now.lifecycle[4] == 0 {
            Ok(())
        } else {
            Err(Failure::InvalidEvidence)
        };
    }
    if acquired && now.lifecycle[1] == 1 && now.lifecycle[4] != 0 && now.lifecycle[5] == 1 {
        Ok(())
    } else {
        Err(Failure::OwnershipLost)
    }
}
#[derive(Default)]
pub struct Terminal {
    pub completion: u32,
    pub sequence: u32,
    pub profile: u32,
}
pub fn interval(before: &ServiceSnapshot, after: &ServiceSnapshot) -> Result<Terminal, Failure> {
    history(before, after)?;
    if !same_input(before, after) {
        return Err(Failure::InputInterference);
    }
    if after.lifecycle[1] != before.lifecycle[1] {
        return Err(Failure::InvalidEvidence);
    }
    let mut terminal = Terminal::default();
    let mut requested = false;
    for event in after
        .events()
        .filter(|event| event[0] > before.lifecycle[0])
    {
        match event[1] {
            1 => return Err(Failure::InvalidEvidence),
            9 => return Err(Failure::Detached),
            7 => return Err(Failure::UnresolvedTermination),
            8 => {
                if event[3] == 8 {
                    return Err(Failure::InvalidEvidence);
                }
                if event[3] == 6 && terminal.profile == 0 {
                    terminal.profile = event[0];
                }
            }
            _ => {}
        }
        if event[2] != before.lifecycle[5] {
            continue;
        }
        if event[1] == 2 {
            requested = true;
        }
        if terminal.sequence == 0 && (event[1] == 4 || (event[1] == 3 && requested)) {
            terminal.sequence = event[0];
            terminal.completion = if event[1] == 4 { 1 } else { 2 };
        }
    }
    if after.lifecycle[4] != 0 {
        if after.lifecycle[5] != before.lifecycle[5] || terminal.sequence != 0 {
            return Err(Failure::InvalidEvidence);
        }
    } else if terminal.sequence == 0 {
        return Err(Failure::NoTerminal);
    }
    Ok(terminal)
}
pub fn evaluate(
    before: &ServiceSnapshot,
    middle: &ServiceSnapshot,
    after: &ServiceSnapshot,
    status: &mut Status,
) -> Result<(), Failure> {
    interval(before, middle)?;
    history(middle, after)?;
    if !same_input(middle, after) {
        return Err(Failure::InputInterference);
    }
    let terminal = interval(before, after)?;
    if before.base[3] != 1 || before.lifecycle[4] == 0 {
        return Err(Failure::WrongSource);
    }
    if after.base[3] != 2 {
        return Err(Failure::ServiceNotRu);
    }
    if after.lifecycle[4] != 0 || terminal.sequence == 0 {
        return Err(Failure::NoTerminal);
    }
    status.0[5..].copy_from_slice(&[
        before.base[0],
        before.lifecycle[5],
        before.lifecycle[0],
        middle.lifecycle[0],
        after.lifecycle[0],
        terminal.sequence,
        terminal.profile,
        terminal.completion,
        if terminal.sequence <= middle.lifecycle[0] {
            1
        } else {
            2
        },
        if terminal.profile == 0 {
            0
        } else if terminal.sequence < terminal.profile {
            1
        } else {
            2
        },
    ]);
    Ok(())
}
fn exact_profile(profile: &TF_INPUTPROCESSORPROFILE, ru: bool) -> bool {
    profile.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR
        && profile.clsid == SERVICE_ID
        && profile.guidProfile == if ru { RU_PROFILE } else { EN_PROFILE }
        && profile.langid == if ru { 0x419 } else { 0x409 }
        && profile.catid == GUID_TFCAT_TIP_KEYBOARD
        && profile.dwFlags & TF_IPP_FLAG_ACTIVE != 0
}
struct Native {
    languages: ITfInputProcessorProfiles,
    profiles: ITfInputProcessorProfileMgr,
}
struct Capabilities {
    native: Native,
    endpoint: Endpoint,
    activation: u32,
}
enum State {
    AwaitingEmpty(Option<Native>),
    AwaitingWord(Capabilities, ServiceSnapshot, ServiceSnapshot, u32),
    Countdown(Capabilities, ServiceSnapshot, u64),
    InFlight,
    Done(Status),
}
pub struct Probe {
    state: RefCell<State>,
    window: HWND,
    manager: ITfThreadMgr,
    thread: u32,
    progress: Cell<[u32; 15]>,
    #[cfg(any(test, feature = "controlled-tests"))]
    hooks: Option<TestHooks>,
}
/// Controlled callbacks substitute the fixture's native capabilities and public word.
#[cfg(any(test, feature = "controlled-tests"))]
pub struct TestHooks {
    pub acquire: Box<
        dyn Fn(&Probe) -> Result<(ITfInputProcessorProfiles, ITfInputProcessorProfileMgr), HRESULT>,
    >,
    pub read: Box<dyn Fn(&Probe) -> Result<u32, Failure>>,
    pub clock: Box<dyn Fn(u64) -> u64>,
}
impl Probe {
    pub fn new(window: HWND, manager: ITfThreadMgr, thread: u32) -> Self {
        Self {
            state: RefCell::new(State::AwaitingEmpty(None)),
            window,
            manager,
            thread,
            progress: Cell::new([0; 15]),
            #[cfg(any(test, feature = "controlled-tests"))]
            hooks: None,
        }
    }
    /// Construct the same source state machine with controlled native boundaries.
    #[cfg(any(test, feature = "controlled-tests"))]
    pub fn with_hooks(window: HWND, manager: ITfThreadMgr, thread: u32, hooks: TestHooks) -> Self {
        let mut probe = Self::new(window, manager, thread);
        probe.hooks = Some(hooks);
        probe
    }
    pub fn close(&self) {
        if matches!(*self.state.borrow(), State::Done(_)) {
            return;
        }
        let mut status = self.progress.get();
        status[0] = 4;
        status[1] = Failure::Closed as u32;
        let old = self.state.replace(State::Done(Status(status)));
        drop(old);
    }
    fn running(&self) -> bool {
        matches!(*self.state.borrow(), State::InFlight)
    }
    fn guard(&self) -> Result<(), Failure> {
        if self.running() {
            Ok(())
        } else {
            Err(Failure::Closed)
        }
    }
    fn call<T>(
        &self,
        call: impl FnOnce() -> Result<T, HRESULT>,
        failure: Failure,
    ) -> Result<T, Failure> {
        self.guard()?;
        let result = call();
        self.guard()?;
        result.map_err(|error| {
            let mut progress = self.progress.get();
            progress[4] = error.0 as u32;
            self.progress.set(progress);
            failure
        })
    }
    fn fail(&self, failure: Failure) {
        if !self.running() {
            return;
        }
        let mut status = self.progress.get();
        status[0] = 4;
        status[1] = failure as u32;
        self.state.replace(State::Done(Status(status)));
    }
    fn value(&self) -> Status {
        match &*self.state.borrow() {
            State::Done(status) => status.clone(),
            State::Countdown(..) => {
                let mut status = Status::default();
                status.0[0] = 1;
                status
            }
            State::InFlight => {
                let mut status = Status::default();
                status.0[0] = 2;
                status
            }
            _ => Status::default(),
        }
    }
    pub fn tick(&self, now: u64) -> Status {
        if matches!(*self.state.borrow(), State::Done(_) | State::InFlight) {
            return self.value();
        }
        let old = self.state.replace(State::InFlight);
        match self.tick_batch(old, now) {
            Ok(state) if self.running() => {
                self.state.replace(state);
            }
            Err(failure) => self.fail(failure),
            _ => {}
        }
        self.value()
    }
    fn prepare(&self, native: Option<Native>) -> Result<Native, Failure> {
        if let Some(native) = native {
            return Ok(native);
        }
        #[cfg(any(test, feature = "controlled-tests"))]
        if let Some(hooks) = &self.hooks {
            let (languages, profiles) =
                self.call(|| (hooks.acquire)(self), Failure::SetupUnavailable)?;
            return Ok(Native {
                languages,
                profiles,
            });
        }
        let languages: ITfInputProcessorProfiles = self.call(
            || {
                unsafe {
                    CoCreateInstance(&CLSID_TF_InputProcessorProfiles, None, CLSCTX_INPROC_SERVER)
                }
                .map_err(|error| error.code())
            },
            Failure::SetupUnavailable,
        )?;
        let profiles = self.call(
            || languages.cast().map_err(|error| error.code()),
            Failure::SetupUnavailable,
        )?;
        Ok(Native {
            languages,
            profiles,
        })
    }
    fn selection(&self, native: &Native) -> Result<(u16, TF_INPUTPROCESSORPROFILE), Failure> {
        let language = self.call(
            || {
                exact_output(|output| unsafe {
                    (native.languages.vtable().GetCurrentLanguage)(
                        native.languages.as_raw(),
                        output,
                    )
                })
            },
            Failure::WrongSource,
        )?;
        let profile = self.call(
            || {
                exact_output(|output| unsafe {
                    (native.profiles.vtable().GetActiveProfile)(
                        native.profiles.as_raw(),
                        &GUID_TFCAT_TIP_KEYBOARD,
                        output,
                    )
                })
            },
            Failure::TargetNotActive,
        )?;
        Ok((language, profile))
    }
    fn read_word(&self) -> Result<u32, Failure> {
        #[cfg(any(test, feature = "controlled-tests"))]
        if let Some(hooks) = &self.hooks {
            let value = (hooks.read)(self);
            self.guard()?;
            return value;
        }
        read_public_word(
            || self.running(),
            || self.public_controls(),
            |id, message, wparam, lparam| {
                let Ok(field) = (unsafe { GetDlgItem(Some(self.window), id) }) else {
                    return -1;
                };
                unsafe { SendMessageW(field, message, Some(wparam), Some(lparam)).0 }
            },
        )
    }
    fn public_controls(&self) -> Result<(), Failure> {
        self.guard()?;
        if unsafe { GetCurrentThreadId() } != self.thread {
            return Err(Failure::WrongThread);
        }
        let root = crate::uia::describe(self.window);
        if !root.complete
            || root.tid != self.thread
            || root.pid != unsafe { GetCurrentProcessId() }
            || String::from_utf16_lossy(&root.class_name) != "KeyrillProofEditor"
            || unsafe { GetAncestor(self.window, GA_ROOT) } != self.window
        {
            return Err(Failure::SetupUnavailable);
        }
        for id in [101, 102] {
            let field = unsafe { GetDlgItem(Some(self.window), id) }
                .map_err(|_| Failure::SetupUnavailable)?;
            let facts = crate::uia::describe(field);
            if !facts.complete
                || facts.pid != root.pid
                || facts.tid != self.thread
                || facts.control != id
                || !String::from_utf16_lossy(&facts.class_name).eq_ignore_ascii_case("RICHEDIT50W")
                || unsafe { GetParent(field) }.ok() != Some(self.window)
                || facts.style & WS_CHILD.0 as isize == 0
                || facts.style
                    & (ES_READONLY as isize | ES_PASSWORD as isize | WS_DISABLED.0 as isize)
                    != 0
                || !unsafe { IsWindowEnabled(field) }.as_bool()
            {
                return Err(Failure::SetupUnavailable);
            }
        }
        if unsafe { GetForegroundWindow() } != self.window
            || unsafe { GetFocus() }
                != unsafe { GetDlgItem(Some(self.window), 101) }
                    .map_err(|_| Failure::FocusChanged)?
        {
            return Err(Failure::FocusChanged);
        }
        Ok(())
    }
    fn copy(&self, caps: &Capabilities) -> Result<ServiceSnapshot, Failure> {
        self.guard()?;
        let snapshot = caps.endpoint.copy();
        self.guard()?;
        let snapshot = snapshot.map_err(|error| {
            if error == crate::diagnostics::E_PENDING {
                Failure::Busy
            } else {
                Failure::InvalidEvidence
            }
        })?;
        active(&snapshot)?;
        if caps.activation != 0 && caps.activation != snapshot.base[0] {
            return Err(Failure::ActivationChanged);
        }
        Ok(snapshot)
    }
    fn discover(&self, caps: &Capabilities) -> Result<(), Failure> {
        self.guard()?;
        let same = caps
            .endpoint
            .same_provider_with(&self.manager, || self.running());
        self.guard()?;
        let same = same.map_err(|error| {
            if error == TF_E_NOPROVIDER {
                Failure::ProviderMissing
            } else {
                Failure::InvalidEvidence
            }
        })?;
        if same {
            Ok(())
        } else {
            Err(Failure::ProviderChanged)
        }
    }
    fn tick_batch(&self, state: State, now: u64) -> Result<State, Failure> {
        if unsafe { GetCurrentThreadId() } != self.thread {
            return Err(Failure::WrongThread);
        }
        let (mut caps, empty, last, prefix, deadline, binding, countdown) = match state {
            State::AwaitingEmpty(native) => {
                let native = self.prepare(native)?;
                let selection = self.selection(&native);
                if selection.as_ref().is_err()
                    || !selection.as_ref().is_ok_and(|(language, profile)| {
                        *language == 0x409 && exact_profile(profile, false)
                    })
                {
                    self.progress.set([0; 15]);
                    return Ok(State::AwaitingEmpty(Some(native)));
                }
                let discovered = Endpoint::discover_with(&self.manager, || self.running());
                self.guard()?;
                let endpoint = match discovered {
                    Ok(endpoint) => endpoint,
                    Err(TF_E_NOPROVIDER) => {
                        self.progress.set([0; 15]);
                        return Ok(State::AwaitingEmpty(Some(native)));
                    }
                    Err(_) => return Err(Failure::InvalidEvidence),
                };
                (
                    Capabilities {
                        native,
                        endpoint,
                        activation: 0,
                    },
                    ServiceSnapshot::default(),
                    ServiceSnapshot::default(),
                    0,
                    0,
                    true,
                    false,
                )
            }
            State::AwaitingWord(caps, empty, last, prefix) => {
                (caps, empty, last, prefix, 0, false, false)
            }
            State::Countdown(caps, ready, deadline) => (
                caps,
                ServiceSnapshot::default(),
                ready,
                6,
                deadline,
                false,
                true,
            ),
            _ => return Err(Failure::InvalidEvidence),
        };
        if countdown && now > deadline && now - deadline > 250 {
            return Err(Failure::Late);
        }
        if !binding {
            let (language, profile) = self.selection(&caps.native)?;
            if language != 0x409 || !exact_profile(&profile, false) {
                return Err(Failure::WrongSource);
            }
            self.discover(&caps)?;
        }
        if countdown && now >= deadline {
            self.target(&caps.native)?;
        }
        let word = self.read_word();
        let snapshot = self.copy(&caps)?;
        if snapshot.base[3] != 1 {
            return Err(Failure::WrongSource);
        }
        if binding {
            if snapshot.lifecycle[1] != 0 || snapshot.lifecycle[4] != 0 {
                return Err(Failure::OwnershipLost);
            }
            if word == Err(Failure::FocusChanged) {
                return Ok(State::AwaitingEmpty(Some(caps.native)));
            }
            if word? != 0 {
                return Err(Failure::WordChanged);
            }
            caps.activation = snapshot.base[0];
            return Ok(State::AwaitingWord(caps, snapshot.clone(), snapshot, 0));
        }
        if countdown {
            if word? != 6 {
                return Err(Failure::WordChanged);
            }
            armed(&last, &snapshot)?;
            if now < deadline {
                return Ok(State::Countdown(caps, last, deadline));
            }
            #[cfg(any(test, feature = "controlled-tests"))]
            let latest = self
                .hooks
                .as_ref()
                .map_or_else(|| unsafe { GetTickCount64() }, |hooks| (hooks.clock)(now));
            #[cfg(not(any(test, feature = "controlled-tests")))]
            let latest = unsafe { GetTickCount64() };
            if latest < now || latest < deadline || latest - deadline > 250 {
                return Err(Failure::Late);
            }
            return self.run_once(&caps, &snapshot).map(State::Done);
        }
        history(&last, &snapshot)?;
        word_history(&empty, &snapshot)?;
        if snapshot.lifecycle[1] == 0 && word == Err(Failure::FocusChanged) {
            return Ok(State::AwaitingWord(caps, empty, snapshot, 0));
        }
        let word = word?;
        if word < prefix {
            return Err(Failure::WordChanged);
        }
        if (word != 0) != (snapshot.lifecycle[4] != 0) {
            return Err(Failure::OwnershipLost);
        }
        if snapshot.lifecycle[1] == 0 {
            return Ok(State::AwaitingWord(caps, snapshot.clone(), snapshot, 0));
        }
        if word == 6 {
            return Ok(State::Countdown(
                caps,
                snapshot,
                now.checked_add(2000).ok_or(Failure::Late)?,
            ));
        }
        Ok(State::AwaitingWord(caps, empty, snapshot, word))
    }
    fn target(&self, native: &Native) -> Result<(), Failure> {
        let target: TF_INPUTPROCESSORPROFILE = self.call(
            || {
                exact_output(|output| unsafe {
                    (native.profiles.vtable().GetProfile)(
                        native.profiles.as_raw(),
                        TF_PROFILETYPE_INPUTPROCESSOR,
                        0x419,
                        &SERVICE_ID,
                        &RU_PROFILE,
                        windows::Win32::UI::Input::KeyboardAndMouse::HKL::default(),
                        output,
                    )
                })
            },
            Failure::TargetUnavailable,
        )?;
        if target.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR
            || target.clsid != SERVICE_ID
            || target.guidProfile != RU_PROFILE
            || target.langid != 0x419
            || target.catid != GUID_TFCAT_TIP_KEYBOARD
        {
            return Err(Failure::TargetUnavailable);
        }
        let enabled: BOOL = self.call(
            || {
                exact_output(|output| unsafe {
                    (native.languages.vtable().IsEnabledLanguageProfile)(
                        native.languages.as_raw(),
                        &SERVICE_ID,
                        0x419,
                        &RU_PROFILE,
                        output,
                    )
                })
            },
            Failure::TargetUnavailable,
        )?;
        if enabled == BOOL(1) {
            Ok(())
        } else {
            Err(Failure::TargetUnavailable)
        }
    }
    fn run_once(&self, caps: &Capabilities, before: &ServiceSnapshot) -> Result<Status, Failure> {
        let mut progress = self.progress.get();
        progress[2] = 1;
        self.progress.set(progress);
        self.call(
            || {
                exact_result(unsafe {
                    (caps.native.languages.vtable().ChangeCurrentLanguage)(
                        caps.native.languages.as_raw(),
                        0x419,
                    )
                })
            },
            Failure::NativeLanguageFailed,
        )?;
        let (language, selected) = self.selection(&caps.native)?;
        if language != 0x419 {
            return Err(Failure::LanguageNotChanged);
        }
        if selected.langid != language
            || selected.catid != GUID_TFCAT_TIP_KEYBOARD
            || selected.dwFlags & TF_IPP_FLAG_ACTIVE == 0
        {
            return Err(Failure::WrongSource);
        }
        self.discover(caps)?;
        if self.read_word()? != 6 {
            return Err(Failure::WordChanged);
        }
        let middle = self.copy(caps)?;
        let terminal = interval(before, &middle)?;
        let exact_ru = exact_profile(&selected, true);
        if exact_ru && middle.base[3] != 2 {
            return Err(Failure::ServiceNotRu);
        }
        if exact_ru && terminal.sequence == 0 {
            return Err(Failure::NoTerminal);
        }
        if !exact_ru {
            if middle.base[3] == 2 {
                return Err(Failure::WrongSource);
            }
            let mut progress = self.progress.get();
            progress[3] = 1;
            self.progress.set(progress);
            self.call(
                || {
                    exact_result(unsafe {
                        (caps.native.profiles.vtable().ActivateProfile)(
                            caps.native.profiles.as_raw(),
                            TF_PROFILETYPE_INPUTPROCESSOR,
                            0x419,
                            &SERVICE_ID,
                            &RU_PROFILE,
                            windows::Win32::UI::Input::KeyboardAndMouse::HKL::default(),
                            0,
                        )
                    })
                },
                Failure::NativeActivationFailed,
            )?;
        }
        let (language, selected) = self.selection(&caps.native)?;
        if language != 0x419 || !exact_profile(&selected, true) {
            return Err(Failure::TargetNotActive);
        }
        self.discover(caps)?;
        if self.read_word()? != 6 {
            return Err(Failure::WordChanged);
        }
        let after = self.copy(caps)?;
        let mut status = Status(self.progress.get());
        evaluate(before, &middle, &after, &mut status)?;
        status.0[0] = 3;
        if status.valid() {
            Ok(status)
        } else {
            Err(Failure::InvalidEvidence)
        }
    }
}
/// Read the two named public fixture fields with eligibility checks around each message.
pub fn read_public_word(
    alive: impl Fn() -> bool,
    mut eligibility: impl FnMut() -> Result<(), Failure>,
    mut read: impl FnMut(i32, u32, WPARAM, LPARAM) -> isize,
) -> Result<u32, Failure> {
    let mut eligible = || {
        let result = eligibility();
        if alive() {
            result
        } else {
            Err(Failure::Closed)
        }
    };
    eligible()?;
    let mut prefix = 0;
    for id in [101, 102] {
        eligible()?;
        let length = read(id, WM_GETTEXTLENGTH, WPARAM(0), LPARAM(0));
        if !alive() {
            return Err(Failure::Closed);
        }
        if length < 0 || length > if id == 101 { 6 } else { 0 } {
            return Err(Failure::WordChanged);
        }
        eligible()?;
        let mut text = [0u16; 8];
        let copied = read(
            id,
            WM_GETTEXT,
            WPARAM(text.len()),
            LPARAM(text.as_mut_ptr() as isize),
        );
        if !alive() {
            return Err(Failure::Closed);
        }
        let expected = wide("ghbdtn");
        if copied != length
            || text[..copied.max(0) as usize]
                != expected[..if id == 101 { length as usize } else { 0 }]
        {
            return Err(Failure::WordChanged);
        }
        if id == 101 {
            prefix = length as u32;
            eligible()?;
            let mut selection = CHARRANGE {
                cpMin: -1,
                cpMax: -1,
            };
            read(
                id,
                EM_EXGETSEL,
                WPARAM(0),
                LPARAM((&mut selection as *mut CHARRANGE) as isize),
            );
            if !alive() {
                return Err(Failure::Closed);
            }
            if selection.cpMin != length as i32 || selection.cpMax != length as i32 {
                return Err(Failure::WordChanged);
            }
        }
    }
    eligible()?;
    Ok(prefix)
}
/// Require the verified source result to remain bound to two fresh service publications.
pub fn matches(
    result: &Status,
    first: &crate::diagnostics::Publication,
    second: &crate::diagnostics::Publication,
) -> bool {
    if !result.valid()
        || result.0[0] != 3
        || !crate::diagnostics::comparable(first, second)
        || first.saturated != 0
        || second.saturated != 0
        || !same_input(&first.snapshot, &second.snapshot)
    {
        return false;
    }
    [first, second].into_iter().all(|publication| {
        let snapshot = &publication.snapshot;
        active(snapshot).is_ok()
            && snapshot.base[3] == 2
            && snapshot.base[0] == result.0[5]
            && snapshot.lifecycle[4] == 0
            && snapshot.lifecycle[1] == result.0[6]
            && snapshot.lifecycle[0] >= result.0[9]
    })
}
fn exact_result(result: HRESULT) -> Result<(), HRESULT> {
    if result == S_OK { Ok(()) } else { Err(result) }
}
fn exact_output<T: Default>(call: impl FnOnce(*mut T) -> HRESULT) -> Result<T, HRESULT> {
    let mut value = T::default();
    exact_result(call(&mut value))?;
    Ok(value)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn source_status_rejects_noncanonical_and_invalid_verdict() {
        let value = Status::default();
        let text = value.format().unwrap();
        assert_eq!(Status::parse(&text), Some(value));
        assert!(Status::parse(&text.replace("language_calls=0", "language_calls=00")).is_none());
        let mut invalid = Status::default();
        invalid.0[0] = 3;
        assert!(!invalid.valid());
    }
}
