use crate::diagnostics::{self, LifecycleJournal, RuntimeSnapshot, Snapshot, SnapshotV2};
use crate::engine::Engine;
use crate::foreign::tsf;
use crate::foreign::{self, Call, DependencyLease, Lease};
use crate::keys::BINDINGS;
use crate::profiles::{PROFILES, SERVICE_ID};
use crate::vocabulary;
use crate::vocabulary_contract::Config;
use std::cell::{Cell, RefCell};
use std::ffi::c_void;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::rc::Rc;
use std::rc::Weak;
use std::sync::atomic::{AtomicU32, Ordering};
use windows::Win32::Foundation::{E_ACCESSDENIED, E_INVALIDARG};
use windows::Win32::Foundation::{E_FAIL, RPC_E_WRONG_THREAD, S_OK};
use windows::Win32::System::Ole::CONNECT_E_NOCONNECTION;
use windows::Win32::System::Threading::GetCurrentThreadId;
use windows::Win32::UI::TextServices::*;
use windows_core::{HRESULT, Interface};

pub(crate) static OBJECTS: AtomicU32 = AtomicU32::new(0);
pub(crate) static LOCKS: AtomicU32 = AtomicU32::new(0);
pub(crate) static WORKERS: AtomicU32 = AtomicU32::new(0);

pub(crate) struct ObjectLease;

impl ObjectLease {
    pub(crate) fn new() -> Self {
        OBJECTS.fetch_add(1, Ordering::AcqRel);
        Self
    }
}

impl Drop for ObjectLease {
    fn drop(&mut self) {
        OBJECTS.fetch_sub(1, Ordering::AcqRel);
    }
}

#[derive(Clone, Copy, Default, PartialEq, Eq)]
enum Activation {
    #[default]
    Inactive,
    Preparing,
    Cancelling,
    Active,
    Draining,
    Faulted,
}

#[derive(Default)]
struct State {
    generation: u64,
    epoch: u64,
    activation: Activation,
    activation_open: bool,
    resources: Option<Rc<Resources>>,
    diagnostic_source: Option<Lease<ITfSourceSingle>>,
    engine: Option<Engine>,
    accepted_config: Option<Config>,
    mode: u32,
    stopped: bool,
    diagnostic_depth: u32,
    diagnostics: Snapshot,
    lifecycle: LifecycleJournal,
    runtime: RuntimeSnapshot,
}

pub(crate) struct Sinks {
    pub(crate) thread: *mut c_void,
    pub(crate) profile: *mut c_void,
    pub(crate) keys: *mut c_void,
    pub(crate) provider: *mut c_void,
}

struct Resources {
    profiles: Option<Lease<ITfInputProcessorProfileMgr>>,
    keys: Option<Lease<ITfKeystrokeMgr>>,
    source: Option<Lease<ITfSource>>,
    manager: Option<Lease<ITfThreadMgr>>,
    owner: Weak<Service>,
    epoch: u64,
    client: u32,
    thread_cookie: Cell<u32>,
    profile_cookie: Cell<u32>,
    key_sink: Cell<bool>,
    bindings: Cell<usize>,
    uncertain: Cell<bool>,
    cleanup_error: Cell<HRESULT>,
}

impl Resources {
    fn new(owner: &Rc<Service>, epoch: u64, client: u32) -> Self {
        Self {
            profiles: None,
            keys: None,
            source: None,
            manager: None,
            owner: Rc::downgrade(owner),
            epoch,
            client,
            thread_cookie: Cell::new(TF_INVALID_COOKIE),
            profile_cookie: Cell::new(TF_INVALID_COOKIE),
            key_sink: Cell::new(false),
            bindings: Cell::new(0),
            uncertain: Cell::new(false),
            cleanup_error: Cell::new(S_OK),
        }
    }

    fn record_cleanup(&self, call: Call, absent_allowed: bool) {
        if call.exact() {
            return;
        }
        if self.cleanup_error.get() == S_OK {
            self.cleanup_error.set(if call.result.is_err() {
                call.result
            } else {
                E_FAIL
            });
        }
        if call.exception != 0 || !absent_allowed {
            self.uncertain.set(true);
        }
    }

    fn drain(&self) -> HRESULT {
        if let Some(keys) = &self.keys {
            while self.bindings.get() != 0 {
                let index = self.bindings.get() - 1;
                self.bindings.set(index);
                let call = tsf::unpreserve(keys, &BINDINGS[index]);
                self.record_cleanup(call, call.result == CONNECT_E_NOCONNECTION);
            }
            if self.key_sink.replace(false) {
                self.record_cleanup(tsf::unadvise_keys(keys, self.client), false);
            }
        }
        if let Some(source) = &self.source {
            for cookie in [&self.thread_cookie, &self.profile_cookie] {
                let value = cookie.replace(TF_INVALID_COOKIE);
                if value != TF_INVALID_COOKIE {
                    self.record_cleanup(tsf::unadvise(source, value), false);
                }
            }
        }
        self.cleanup_error.get()
    }
}

impl Drop for Resources {
    fn drop(&mut self) {
        let _ = self.drain();
        let witnesses = [
            self.profiles.as_ref().map(Lease::release_witness),
            self.keys.as_ref().map(Lease::release_witness),
            self.source.as_ref().map(Lease::release_witness),
            self.manager.as_ref().map(Lease::release_witness),
        ];
        // Release can reenter. Draining stays visible until every captured lease has been released.
        drop(self.profiles.take());
        drop(self.keys.take());
        drop(self.source.take());
        drop(self.manager.take());
        if witnesses.iter().flatten().any(|witness| witness.failed()) {
            self.uncertain.set(true);
        }
        if self.uncertain.get() {
            foreign::quarantine();
        }
        if let Some(owner) = self.owner.upgrade() {
            let mut state = owner.state.borrow_mut();
            if state.epoch == self.epoch && state.activation == Activation::Draining {
                state.activation = if self.uncertain.get() {
                    Activation::Faulted
                } else {
                    Activation::Inactive
                };
            }
        }
    }
}

struct DiagnosticDepth<'a>(&'a Service);
impl Drop for DiagnosticDepth<'_> {
    fn drop(&mut self) {
        self.0.state.borrow_mut().diagnostic_depth -= 1;
    }
}

struct ActivationCall<'a>(&'a Service);
impl Drop for ActivationCall<'_> {
    fn drop(&mut self) {
        self.0.state.borrow_mut().activation_open = false;
    }
}

pub(crate) struct Service {
    owner_thread: u32,
    poisoned: Cell<bool>,
    state: RefCell<State>,
    #[cfg(feature = "test-api")]
    panic_at: Cell<u32>,
    _dependencies: Option<DependencyLease>,
    _object: ObjectLease,
}

impl Service {
    pub(crate) fn new() -> Rc<Self> {
        Self::with_dependencies(None)
    }

    pub(crate) fn with_dependencies(dependencies: Option<DependencyLease>) -> Rc<Self> {
        Rc::new(Self {
            // SAFETY: GetCurrentThreadId has no pointer or lifetime preconditions.
            owner_thread: unsafe { GetCurrentThreadId() },
            poisoned: Cell::new(false),
            state: RefCell::new(State::default()),
            #[cfg(feature = "test-api")]
            panic_at: Cell::new(0),
            _dependencies: dependencies,
            _object: ObjectLease::new(),
        })
    }

    #[cfg(feature = "test-api")]
    pub(crate) fn maintain(&self) -> HRESULT {
        self.checked(|| S_OK)
    }

    pub(crate) fn owner_thread(&self) -> bool {
        // SAFETY: GetCurrentThreadId has no pointer or lifetime preconditions.
        unsafe { GetCurrentThreadId() == self.owner_thread }
    }

    pub(crate) fn entry(&self, call: impl FnOnce() -> HRESULT) -> HRESULT {
        if self.poisoned.get() {
            return E_FAIL;
        }
        match catch_unwind(AssertUnwindSafe(call)) {
            Ok(result) => result,
            Err(_) => {
                self.poisoned.set(true);
                self.invalidate();
                E_FAIL
            }
        }
    }

    pub(crate) fn checked(&self, call: impl FnOnce() -> HRESULT) -> HRESULT {
        if !self.owner_thread() {
            return RPC_E_WRONG_THREAD;
        }
        self.entry(|| {
            self.state.borrow_mut().diagnostic_depth += 1;
            let _depth = DiagnosticDepth(self);
            call()
        })
    }

    pub(crate) fn invalidate(&self) {
        let mut state = self.state.borrow_mut();
        state.generation = state.generation.wrapping_add(1);
        if let Some(engine) = state.engine.as_mut() {
            engine.reset();
        }
    }

    fn preparing(&self, epoch: u64) -> bool {
        let state = self.state.borrow();
        !self.poisoned.get() && state.epoch == epoch && state.activation == Activation::Preparing
    }

    fn acquisition(&self, epoch: u64, call: Call) -> Result<(), HRESULT> {
        if !call.exact() {
            return Err(if call.result.is_err() {
                call.result
            } else {
                E_FAIL
            });
        }
        if !self.preparing(epoch) {
            return Err(E_FAIL);
        }
        Ok(())
    }

    fn acquire(
        self: &Rc<Self>,
        manager: *mut c_void,
        native: &mut Resources,
        sinks: &Sinks,
    ) -> Result<(), HRESULT> {
        // SAFETY: the inbound COM caller retains its manager for the complete Activate callback.
        native.manager =
            Some(unsafe { foreign::retain::<ITfThreadMgr>(manager) }.map_err(tsf::failure)?);
        if !self.preparing(native.epoch) {
            return Err(E_FAIL);
        }
        let manager = native.manager.as_ref().ok_or(E_FAIL)?;
        native.source = Some(manager.query().map_err(tsf::failure)?);
        if !self.preparing(native.epoch) {
            return Err(E_FAIL);
        }
        native.keys = Some(manager.query().map_err(tsf::failure)?);
        if !self.preparing(native.epoch) {
            return Err(E_FAIL);
        }
        let source = native.source.as_ref().ok_or(E_FAIL)?;
        for (id, sink, target) in [
            (
                ITfThreadMgrEventSink::IID,
                sinks.thread,
                &native.thread_cookie,
            ),
            (
                ITfInputProcessorProfileActivationSink::IID,
                sinks.profile,
                &native.profile_cookie,
            ),
        ] {
            native.uncertain.set(true);
            let (call, cookie) = tsf::advise(source, &id, sink);
            if call.exception == 0 {
                native.uncertain.set(false);
            }
            if call.exact() {
                target.set(cookie);
                if cookie == TF_INVALID_COOKIE {
                    native.uncertain.set(true);
                }
            }
            self.acquisition(native.epoch, call)?;
            if cookie == TF_INVALID_COOKIE {
                native.uncertain.set(true);
                return Err(E_FAIL);
            }
        }
        let keys = native.keys.as_ref().ok_or(E_FAIL)?;
        native.uncertain.set(true);
        let call = tsf::advise_keys(keys, native.client, sinks.keys);
        if call.exception == 0 {
            native.uncertain.set(false);
        }
        if call.exact() {
            native.key_sink.set(true);
        }
        self.acquisition(native.epoch, call)?;
        for binding in &BINDINGS {
            native.uncertain.set(true);
            let call = tsf::preserve(keys, native.client, binding);
            if call.exception == 0 {
                native.uncertain.set(false);
            }
            if call.exact() {
                native.bindings.set(native.bindings.get() + 1);
            }
            self.acquisition(native.epoch, call)?;
        }
        let injected = self
            ._dependencies
            .as_ref()
            .map_or(std::ptr::null_mut(), |deps| deps.table.profiles);
        native.profiles = if injected.is_null() {
            tsf::create_profiles().ok()
        } else {
            // SAFETY: the dependency owner retains the injected SDK profile manager.
            unsafe { foreign::retain::<ITfInputProcessorProfileMgr>(injected) }.ok()
        };
        if !self.preparing(native.epoch) {
            return Err(E_FAIL);
        }
        if let Some(profiles) = &native.profiles {
            let generation = self.state.borrow().generation;
            let (call, profile) = tsf::active_profile(profiles);
            if call.exact()
                && self.preparing(native.epoch)
                && self.state.borrow().generation == generation
            {
                let _ =
                    self.profile_changed(profile.clsid, profile.guidProfile, TF_IPSINK_FLAG_ACTIVE);
            }
        }
        if self.preparing(native.epoch) {
            Ok(())
        } else {
            Err(E_FAIL)
        }
    }

    pub(crate) fn activate(
        self: &Rc<Self>,
        manager: *mut c_void,
        client: u32,
        flags: u32,
        sinks: Sinks,
    ) -> HRESULT {
        self.checked(|| {
            let epoch = {
                let mut state = self.state.borrow_mut();
                if state.activation != Activation::Inactive
                    || state.activation_open
                    || manager.is_null()
                    || client == 0
                    || state.epoch == u64::MAX
                {
                    return E_INVALIDARG;
                }
                if flags & TF_TMAE_SECUREMODE != 0 {
                    return E_ACCESSDENIED;
                }
                state.activation_open = true;
                state.activation = Activation::Preparing;
                state.epoch += 1;
                state.epoch
            };
            let _call = ActivationCall(self);
            let configuration = vocabulary::capture_config();
            {
                let mut state = self.state.borrow_mut();
                if let Some(config) = configuration {
                    state.accepted_config = Some(config);
                }
                if state.accepted_config.is_none() {
                    state.accepted_config = Some(Config::Off);
                }
                let policy = state.accepted_config.as_ref().map_or(0, Config::policy);
                if state.engine.is_none() {
                    state.engine = Engine::new().ok();
                }
                let Some(engine) = state.engine.as_mut() else {
                    state.activation = Activation::Inactive;
                    return E_FAIL;
                };
                if engine
                    .configure(if policy == 2 { 0 } else { policy })
                    .is_err()
                {
                    state.activation = Activation::Inactive;
                    return E_FAIL;
                }
                state.stopped = false;
                state.mode = 0;
            }
            let mut resources = Resources::new(self, epoch, client);
            let result = self.acquire(manager, &mut resources, &sinks);
            if result.is_ok() && self.preparing(epoch) {
                {
                    let mut state = self.state.borrow_mut();
                    state.resources = Some(Rc::new(resources));
                    state.activation = Activation::Active;
                    let diagnostics = &mut state.diagnostics;
                    diagnostics::increment(&mut diagnostics.activation, &mut diagnostics.saturated);
                    state.runtime.activation = state.diagnostics.activation;
                }
                self.register_diagnostics(epoch, sinks.provider);
                return if self.same_activation(epoch) {
                    S_OK
                } else {
                    E_FAIL
                };
            }
            {
                let mut state = self.state.borrow_mut();
                state.activation = Activation::Draining;
                state.mode = 0;
            }
            self.invalidate();
            drop(resources);
            result
                .err()
                .filter(|result| result.is_err())
                .unwrap_or(E_FAIL)
        })
    }

    pub(crate) fn deactivate(&self) -> HRESULT {
        self.checked(|| {
            let retired = {
                let mut state = self.state.borrow_mut();
                if state.activation == Activation::Preparing {
                    state.activation = Activation::Cancelling;
                    state.mode = 0;
                    drop(state);
                    self.invalidate();
                    return S_OK;
                }
                if state.activation != Activation::Active {
                    return S_OK;
                }
                state.activation = Activation::Draining;
                state.mode = 0;
                (state.resources.take(), state.diagnostic_source.take())
            };
            self.invalidate();
            let (resources, diagnostic_source) = retired;
            let result = resources
                .as_ref()
                .map_or(S_OK, |resources| resources.drain());
            if let (Some(source), Some(native)) = (&diagnostic_source, &resources) {
                tsf::unadvise_single(source, native.client);
            }
            drop(diagnostic_source);
            drop(resources);
            result
        })
    }

    fn same_activation(&self, epoch: u64) -> bool {
        let state = self.state.borrow();
        !self.poisoned.get() && state.epoch == epoch && state.activation == Activation::Active
    }

    fn register_diagnostics(&self, epoch: u64, provider: *mut c_void) {
        use windows::Win32::System::LibraryLoader::GetModuleFileNameW;
        let mut path = [0; 32768];
        // SAFETY: the bounded output receives only the current process executable path.
        let count = unsafe { GetModuleFileNameW(None, &mut path) } as usize;
        let proof = count != 0
            && count < path.len()
            && path[..count]
                .iter()
                .rposition(|unit| *unit == b'\\' as u16)
                .is_some_and(|index| {
                    String::from_utf16_lossy(&path[index + 1..count])
                        .eq_ignore_ascii_case("KeyrillProofEditor.exe")
                });
        if !proof || !self.same_activation(epoch) {
            return;
        }
        let resources = self.state.borrow().resources.clone();
        let Some(resources) = resources else {
            return;
        };
        let Some(manager) = resources.manager.as_ref() else {
            return;
        };
        let Ok(source) = manager.query::<ITfSourceSingle>() else {
            return;
        };
        if !self.same_activation(epoch) {
            return;
        }
        let call = tsf::advise_single(&source, resources.client, provider);
        if call.exception == 0 && call.result.is_ok() && self.same_activation(epoch) {
            let retired = self.state.borrow_mut().diagnostic_source.replace(source);
            drop(retired);
        } else {
            tsf::unadvise_single(&source, resources.client);
        }
    }

    pub(crate) fn profile_changed(
        &self,
        service: windows_core::GUID,
        profile: windows_core::GUID,
        flags: u32,
    ) -> HRESULT {
        self.checked(|| {
            self.invalidate();
            let mut state = self.state.borrow_mut();
            state.mode = if service == SERVICE_ID && flags & TF_IPSINK_FLAG_ACTIVE != 0 {
                PROFILES
                    .iter()
                    .find(|entry| entry.id == profile)
                    .map_or(0, |entry| entry.mode)
            } else {
                0
            };
            S_OK
        })
    }

    pub(crate) fn key(&self, release: bool) -> HRESULT {
        if release && !self.owner_thread() {
            return S_OK;
        }
        self.checked(|| {
            #[cfg(feature = "test-api")]
            self.inject_panic(1);
            S_OK
        })
    }

    pub(crate) fn copy_snapshot(&self) -> Result<Snapshot, HRESULT> {
        let state = self.state.borrow();
        if state.diagnostic_depth != 0 {
            return Err(HRESULT(0x8000000Au32 as i32));
        }
        let mut snapshot = state.diagnostics;
        snapshot.active = (state.activation == Activation::Active) as u32;
        snapshot.stopped = (state.stopped || self.poisoned.get()) as u32;
        snapshot.mode = state.mode;
        Ok(snapshot)
    }

    pub(crate) fn copy_snapshot_v2(&self) -> Result<SnapshotV2, HRESULT> {
        let base = self.copy_snapshot()?;
        let lifecycle = self.state.borrow().lifecycle.copy(false, 0);
        Ok(SnapshotV2 { base, lifecycle })
    }

    pub(crate) fn copy_runtime(&self) -> Result<RuntimeSnapshot, HRESULT> {
        let state = self.state.borrow();
        if state.diagnostic_depth != 0 {
            return Err(HRESULT(0x8000000Au32 as i32));
        }
        Ok(state.runtime)
    }

    pub(crate) fn interested(&self) -> bool {
        let state = self.state.borrow();
        state.activation == Activation::Active && !state.stopped && state.mode != 0
    }

    #[cfg(feature = "test-api")]
    pub(crate) fn arm_panic(&self, at: u32) {
        self.panic_at.set(at);
    }

    #[cfg(feature = "test-api")]
    pub(crate) fn inject_panic(&self, at: u32) {
        if self.panic_at.get() == at {
            self.panic_at.set(0);
            panic!("injected adapter boundary panic");
        }
    }

    #[cfg(feature = "test-api")]
    pub(crate) fn stopped(&self) -> bool {
        self.poisoned.get()
    }
}

impl Drop for Service {
    fn drop(&mut self) {
        // Generated IUnknown::Release invokes this destructor. No panic may leave it.
        let _ = catch_unwind(AssertUnwindSafe(|| {
            #[cfg(feature = "test-api")]
            self.inject_panic(3);
            self.invalidate();
        }));
    }
}
