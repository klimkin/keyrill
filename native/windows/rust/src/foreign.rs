//! The only owner of host COM references. Generated ownership helpers stay out of this module too.

pub(crate) mod document;
pub(crate) mod registrar;
pub(crate) mod tsf;
pub(crate) mod win32;

use crate::field_contract::{Facts, HostIdentity};
use std::cell::Cell;
use std::ffi::c_void;
use std::marker::PhantomData;
use std::ptr::NonNull;
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, Ordering};
use windows::Win32::Foundation::{E_POINTER, E_UNEXPECTED, HMODULE, HWND, S_OK};
use windows::Win32::System::LibraryLoader::{
    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, GET_MODULE_HANDLE_EX_FLAG_PIN, GetModuleHandleExW,
};
use windows::Win32::UI::TextServices::{ITfContext, ITfEditSession};
use windows_core::{GUID, HRESULT, Interface, PCWSTR};

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub(crate) struct Call {
    pub(crate) result: HRESULT,
    pub(crate) exception: u32,
}

impl Call {
    pub(crate) fn exact(self) -> bool {
        self.result == S_OK && self.exception == 0
    }
}

pub(crate) fn keyboard_state(output: &mut [u8; 256]) -> bool {
    windows_core::link!("user32.dll" "system" fn GetKeyboardState(output: *mut u8) -> windows_core::BOOL);
    // The pinned projection derives its writable pointer from as_ptr(), which loses mutable
    // provenance. Keep this output mutable through the actual Win32 call.
    // SAFETY: output is a complete writable keyboard snapshot retained through the call.
    unsafe { GetKeyboardState(output.as_mut_ptr()).as_bool() }
}

#[cfg(test)]
mod keyboard_tests {
    use super::keyboard_state;
    use windows::Win32::UI::Input::KeyboardAndMouse::SetKeyboardState;

    struct Restore([u8; 256]);
    impl Drop for Restore {
        fn drop(&mut self) {
            // SAFETY: this restores only the test thread's complete original keyboard snapshot.
            let _ = unsafe { SetKeyboardState(&self.0) };
        }
    }

    #[test]
    fn keyboard_output_retains_mutable_provenance() {
        let mut original = [0; 256];
        assert!(keyboard_state(&mut original));
        let restore = Restore(original);
        let mut pressed = restore.0;
        pressed[0xa4] = 0x80;
        pressed[0xa5] = 0;
        // SAFETY: the complete input snapshot changes only this test thread's key-state table.
        unsafe { SetKeyboardState(&pressed).unwrap() };
        let mut observed = [0; 256];
        assert!(keyboard_state(&mut observed));
        assert_eq!(observed[0xa4], 0x80);
        assert!(observed[0xa4] & 0x80 != 0);
        assert_eq!(observed[0xa5] & 0x80, 0);
    }
}

#[derive(Clone, Copy, Debug)]
pub(crate) enum InterfaceError {
    Native(Call),
    Missing,
}

impl InterfaceError {
    pub(crate) fn call(self) -> Call {
        match self {
            Self::Native(call) => call,
            Self::Missing => Call {
                result: S_OK,
                exception: 0,
            },
        }
    }
}

unsafe extern "C" {
    fn kr_foreign_query(object: *mut c_void, id: *const GUID, output: *mut *mut c_void) -> Call;
    fn kr_foreign_add_ref(object: *mut c_void, count: *mut u32) -> Call;
    fn kr_foreign_release(object: *mut c_void, count: *mut u32) -> Call;
    fn kr_foreign_request(
        context: *mut c_void,
        client: u32,
        session: *mut c_void,
        flags: u32,
        session_result: *mut HRESULT,
    ) -> Call;
    fn kr_foreign_lifetime(
        call: unsafe extern "system" fn(*mut c_void),
        context: *mut c_void,
    ) -> Call;
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct Dependencies {
    pub(crate) size: u32,
    pub(crate) version: u32,
    pub(crate) context: *mut c_void,
    pub(crate) retain: Option<unsafe extern "system" fn(*mut c_void)>,
    pub(crate) release: Option<unsafe extern "system" fn(*mut c_void)>,
    pub(crate) profiles: *mut c_void,
    pub(crate) layouts: Option<unsafe extern "system" fn(i32, *mut isize) -> i32>,
    pub(crate) capture: Option<
        unsafe extern "system" fn(*mut c_void, HWND, *const HostIdentity, *mut Facts) -> HRESULT,
    >,
}

const _: () = assert!(size_of::<Dependencies>() == 56);

pub(crate) struct DependencyLease {
    pub(crate) table: Dependencies,
}

impl DependencyLease {
    pub(crate) fn new(table: Dependencies) -> Result<Self, Call> {
        if let Some(retain) = table.retain {
            // SAFETY: the validated dependency owner retains callbacks and context through release.
            let result = unsafe { kr_foreign_lifetime(retain, table.context) };
            if !result.exact() {
                quarantine();
                return Err(result);
            }
        }
        Ok(Self { table })
    }
}

impl Drop for DependencyLease {
    fn drop(&mut self) {
        if let Some(release) = self.table.release {
            // SAFETY: this is the one release paired with the successful dependency acquisition.
            let result = unsafe { kr_foreign_lifetime(release, self.table.context) };
            if !result.exact() {
                quarantine();
            }
        }
    }
}

static UNCERTAIN: AtomicBool = AtomicBool::new(false);

pub(crate) fn uncertain() -> bool {
    UNCERTAIN.load(Ordering::Acquire)
}

pub(crate) fn quarantine() {
    UNCERTAIN.store(true, Ordering::Release);
    let mut module = HMODULE::default();
    // SAFETY: FROM_ADDRESS interprets this address as module code, never as UTF-16 text.
    // PIN retains that module through process exit when a foreign reference has unknown ownership.
    unsafe {
        let _ = GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            PCWSTR(quarantine as *const () as *const u16),
            &mut module,
        );
    }
}

struct Owner {
    pointer: NonNull<c_void>,
    release_failed: Rc<Cell<bool>>,
}

#[derive(Clone)]
pub(crate) struct ReleaseWitness(Rc<Cell<bool>>);

impl ReleaseWitness {
    pub(crate) fn failed(&self) -> bool {
        self.0.get()
    }
}

impl Drop for Owner {
    fn drop(&mut self) {
        let mut count = 0;
        // SAFETY: this owner holds exactly one reference and surrenders it once through the fence.
        let result = unsafe { kr_foreign_release(self.pointer.as_ptr(), &mut count) };
        if !result.exact() {
            self.release_failed.set(true);
            // A throwing Release may have already deleted the object. Never retry it.
            quarantine();
        }
    }
}

pub(crate) struct Lease<T: Interface> {
    owner: Rc<Owner>,
    interface: PhantomData<T>,
}

impl<T: Interface> Clone for Lease<T> {
    fn clone(&self) -> Self {
        Self {
            owner: Rc::clone(&self.owner),
            interface: PhantomData,
        }
    }
}

impl<T: Interface> Lease<T> {
    pub(crate) fn address(&self) -> usize {
        self.owner.pointer.as_ptr() as usize
    }

    pub(crate) fn release_witness(&self) -> ReleaseWitness {
        ReleaseWitness(Rc::clone(&self.owner.release_failed))
    }

    pub(crate) fn query<U: Interface>(&self) -> Result<Lease<U>, InterfaceError> {
        // SAFETY: this Rust lease retains the host across the complete fenced acquisition.
        unsafe { query(self.owner.pointer.as_ptr()) }
    }
}

/// Take exactly one live reference transferred by a normally completed typed SDK call.
/// The caller separately validates its HRESULT before using the object as native evidence.
pub(crate) unsafe fn adopt<T: Interface>(pointer: *mut c_void) -> Result<Lease<T>, Call> {
    let Some(pointer) = NonNull::new(pointer) else {
        return Err(Call {
            result: E_UNEXPECTED,
            exception: 0,
        });
    };
    Ok(Lease {
        owner: Rc::new(Owner {
            pointer,
            release_failed: Rc::new(Cell::new(false)),
        }),
        interface: PhantomData,
    })
}

pub(crate) struct Returned<T> {
    pub(crate) call: Call,
    pub(crate) value: T,
}

fn returned_interface<T: Interface>(
    call: Call,
    pointer: *mut c_void,
) -> Returned<Option<Lease<T>>> {
    let value = if call.exception != 0 {
        quarantine();
        None
    } else if pointer.is_null() {
        None
    } else {
        // SAFETY: the typed SDK output transfers one memory reference on normal completion.
        // Its HRESULT remains separate and grants no document authority.
        unsafe { adopt(pointer) }.ok()
    };
    Returned { call, value }
}

/// Acquire a reference from an inbound pointer while its caller retains it.
pub(crate) unsafe fn retain<T: Interface>(pointer: *mut c_void) -> Result<Lease<T>, Call> {
    let Some(pointer) = NonNull::new(pointer) else {
        return Err(Call {
            result: E_POINTER,
            exception: 0,
        });
    };
    let mut count = 0;
    // SAFETY: the inbound caller retains this live interface until the callback returns.
    let result = unsafe { kr_foreign_add_ref(pointer.as_ptr(), &mut count) };
    if !result.exact() {
        quarantine();
        return Err(result);
    }
    Ok(Lease {
        owner: Rc::new(Owner {
            pointer,
            release_failed: Rc::new(Cell::new(false)),
        }),
        interface: PhantomData,
    })
}

pub(crate) unsafe fn query<T: Interface>(pointer: *mut c_void) -> Result<Lease<T>, InterfaceError> {
    if pointer.is_null() {
        return Err(InterfaceError::Native(Call {
            result: E_POINTER,
            exception: 0,
        }));
    }
    let mut output = std::ptr::null_mut();
    // SAFETY: the caller retains the live host and output is a distinct writable pointer slot.
    let result = unsafe { kr_foreign_query(pointer, &T::IID, &mut output) };
    if !result.exact() {
        if result.exception != 0 {
            quarantine();
        } else {
            let retired = returned_interface::<T>(result, output).value;
            let witness = retired.as_ref().map(Lease::release_witness);
            drop(retired);
            if witness.is_some_and(|witness| witness.failed()) {
                return Err(InterfaceError::Native(Call {
                    result: E_UNEXPECTED,
                    exception: 1,
                }));
            }
        }
        return Err(InterfaceError::Native(result));
    }
    if output.is_null() {
        return Err(InterfaceError::Missing);
    }
    // SAFETY: exact S_OK transfers one reference. Null output is rejected by adopt.
    unsafe { adopt(output) }.map_err(InterfaceError::Native)
}

pub(crate) fn request(
    context: &Lease<ITfContext>,
    client: u32,
    session: &ITfEditSession,
    flags: u32,
) -> (Call, HRESULT) {
    let mut session_result = E_UNEXPECTED;
    // SAFETY: local Rust COM and the lease retain both objects throughout this synchronous call.
    let result = unsafe {
        kr_foreign_request(
            context.owner.pointer.as_ptr(),
            client,
            session.as_raw(),
            flags,
            &mut session_result,
        )
    };
    (result, session_result)
}
