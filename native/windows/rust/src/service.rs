use std::cell::{Cell, RefCell};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::rc::Rc;
use std::sync::atomic::{AtomicU32, Ordering};
use windows::Win32::Foundation::{E_FAIL, E_POINTER, RPC_E_WRONG_THREAD, S_OK};
use windows::Win32::System::Threading::GetCurrentThreadId;
use windows_core::HRESULT;

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

#[derive(Default)]
struct State {
    generation: u64,
    active: bool,
}

pub(crate) struct Service {
    owner_thread: u32,
    poisoned: Cell<bool>,
    state: RefCell<State>,
    #[cfg(feature = "test-api")]
    panic_at: Cell<u32>,
}

impl Service {
    pub(crate) fn new() -> Rc<Self> {
        Rc::new(Self {
            // SAFETY: GetCurrentThreadId has no pointer or lifetime preconditions.
            owner_thread: unsafe { GetCurrentThreadId() },
            poisoned: Cell::new(false),
            state: RefCell::new(State::default()),
            #[cfg(feature = "test-api")]
            panic_at: Cell::new(0),
        })
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
        self.entry(call)
    }

    pub(crate) fn invalidate(&self) {
        let mut state = self.state.borrow_mut();
        state.generation = state.generation.wrapping_add(1);
        state.active = false;
    }

    pub(crate) fn activate(&self, manager_is_null: bool) -> HRESULT {
        self.checked(|| {
            if manager_is_null {
                return E_POINTER;
            }
            // Admission remains closed until the native acquisition unit is transferred.
            E_FAIL
        })
    }

    pub(crate) fn deactivate(&self) -> HRESULT {
        self.checked(|| {
            self.invalidate();
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
