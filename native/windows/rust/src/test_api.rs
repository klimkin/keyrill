//! Versioned test transport. All decisions stay in the production service and edit frame.

use crate::com;
use crate::edit::{Effect, Frame};
use crate::foreign;
use crate::service::{ObjectLease, Service};
use keyrill_ffi::NativeEvent;
use std::ffi::c_void;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::rc::Rc;
use windows::Win32::Foundation::{E_INVALIDARG, E_POINTER, E_UNEXPECTED, S_OK};
use windows::Win32::UI::TextServices::ITfContext;
use windows_core::{BOOL, HRESULT, IUnknown, Interface};

const VERSION: u32 = 2;

struct Handle {
    service: Rc<Service>,
    _object: ObjectLease,
}

#[repr(C)]
pub(crate) struct Probe {
    size: u32,
    version: u32,
    outer: HRESULT,
    callback: HRESULT,
    foreign_exception: u32,
    entered: BOOL,
    completed: BOOL,
    effect: u32,
    identity: usize,
}

fn fence(call: impl FnOnce() -> HRESULT) -> HRESULT {
    catch_unwind(AssertUnwindSafe(call)).unwrap_or(E_UNEXPECTED)
}

#[unsafe(no_mangle)]
unsafe extern "system" fn KeyrillTestCreate(
    version: u32,
    dependencies: *const foreign::Dependencies,
    handle: *mut *mut c_void,
    output: *mut *mut c_void,
) -> HRESULT {
    fence(|| {
        if handle.is_null() || output.is_null() {
            return E_POINTER;
        }
        // SAFETY: test API callers provide distinct writable pointer slots.
        unsafe {
            handle.write(std::ptr::null_mut());
            output.write(std::ptr::null_mut());
        }
        if version != VERSION {
            return E_INVALIDARG;
        }
        let dependencies = if dependencies.is_null() {
            None
        } else {
            // SAFETY: the test API requires a readable size/version prefix before table validation.
            let size = unsafe { std::ptr::addr_of!((*dependencies).size).read() };
            // SAFETY: version is the second u32 in the required readable prefix.
            let version = unsafe { std::ptr::addr_of!((*dependencies).version).read() };
            if version != VERSION || size as usize != size_of::<foreign::Dependencies>() {
                return E_INVALIDARG;
            }
            // SAFETY: the validated size now requires a complete readable dependency table.
            let table = unsafe { *dependencies };
            if table.retain.is_some() != table.release.is_some() {
                return E_INVALIDARG;
            }
            match foreign::DependencyLease::new(table) {
                Ok(owner) => Some(owner),
                Err(call) => return call.result,
            }
        };
        let service = Service::with_dependencies(dependencies);
        let object = com::create(Rc::clone(&service));
        let owner = Box::new(Handle {
            service,
            _object: ObjectLease::new(),
        });
        // SAFETY: outputs transfer one handle and one COM reference to the test caller.
        unsafe {
            handle.write(Box::into_raw(owner).cast());
            output.write(object.into_raw());
        }
        S_OK
    })
}

#[unsafe(no_mangle)]
unsafe extern "system" fn KeyrillTestEvent(
    handle: *mut c_void,
    context: *mut c_void,
    event: *const NativeEvent,
    eaten: *mut BOOL,
) -> HRESULT {
    fence(|| {
        if eaten.is_null() {
            return E_POINTER;
        }
        // SAFETY: the test transport supplies a writable BOOL output slot.
        unsafe { eaten.write(BOOL(0)) };
        if handle.is_null() || context.is_null() || event.is_null() {
            return E_POINTER;
        }
        // SAFETY: the test caller retains its live handle for this synchronous entry.
        let handle = unsafe { &*handle.cast::<Handle>() };
        handle.service.checked(|| S_OK)
    })
}

#[unsafe(no_mangle)]
unsafe extern "system" fn KeyrillTestMaintain(handle: *mut c_void) -> HRESULT {
    fence(|| {
        if handle.is_null() {
            return E_POINTER;
        }
        // SAFETY: the test caller retains its live handle for this synchronous entry.
        unsafe { &*handle.cast::<Handle>() }.service.maintain()
    })
}

#[unsafe(no_mangle)]
unsafe extern "system" fn KeyrillTestRelease(handle: *mut c_void) -> HRESULT {
    fence(|| {
        if handle.is_null() {
            return E_POINTER;
        }
        // SAFETY: the test caller surrenders its unique handle exactly once after calls drain.
        drop(unsafe { Box::from_raw(handle.cast::<Handle>()) });
        S_OK
    })
}

#[unsafe(no_mangle)]
unsafe extern "system" fn KeyrillTestPanic(handle: *mut c_void, at: u32) -> HRESULT {
    fence(|| {
        if handle.is_null() {
            return E_POINTER;
        }
        // SAFETY: test transport keeps this handle live until Release.
        let handle = unsafe { &*handle.cast::<Handle>() };
        handle.service.checked(|| {
            handle.service.arm_panic(at);
            S_OK
        })
    })
}

#[unsafe(no_mangle)]
unsafe extern "system" fn KeyrillTestStopped(handle: *mut c_void, output: *mut BOOL) -> HRESULT {
    fence(|| {
        if output.is_null() {
            return E_POINTER;
        }
        // SAFETY: output is a writable BOOL under the test API contract.
        unsafe { output.write(BOOL(0)) };
        if handle.is_null() {
            return E_POINTER;
        }
        // SAFETY: test transport keeps this handle live until Release.
        let handle = unsafe { &*handle.cast::<Handle>() };
        // SAFETY: output is initialized even when the service has been poisoned.
        unsafe { output.write(BOOL(handle.service.stopped() as i32)) };
        S_OK
    })
}

#[unsafe(no_mangle)]
unsafe extern "system" fn KeyrillTestProbe(
    handle: *mut c_void,
    host: *mut c_void,
    operation: u32,
    size: u32,
    output: *mut Probe,
) -> HRESULT {
    fence(|| {
        if output.is_null() {
            return E_POINTER;
        }
        if size as usize != size_of::<Probe>() {
            return E_INVALIDARG;
        }
        // SAFETY: size-checked output is a distinct writable Probe object.
        unsafe {
            output.write(Probe {
                size,
                version: VERSION,
                outer: E_UNEXPECTED,
                callback: E_UNEXPECTED,
                foreign_exception: 0,
                entered: BOOL(0),
                completed: BOOL(0),
                effect: 0,
                identity: 0,
            });
        }
        if handle.is_null() || host.is_null() {
            return E_POINTER;
        }
        // SAFETY: test transport keeps handle and host live throughout the call.
        let handle = unsafe { &*handle.cast::<Handle>() };
        handle.service.checked(|| {
            // SAFETY: output is valid and exclusively used by this synchronous call.
            let output = unsafe { &mut *output };
            if operation == 0 {
                // SAFETY: host is an inbound IUnknown retained by the test caller.
                match unsafe { foreign::query::<IUnknown>(host) } {
                    Ok(identity) => {
                        output.identity = identity.address();
                        output.outer = S_OK;
                    }
                    Err(call) => {
                        output.outer = call.result;
                        output.foreign_exception = call.exception;
                    }
                }
            } else if operation == 1 {
                // SAFETY: operation 1 requires an ITfContext pointer retained by the test caller.
                match unsafe { foreign::retain::<ITfContext>(host) } {
                    Ok(context) => {
                        let frame = Frame::new(Rc::clone(&handle.service));
                        let result = frame.request(&context, 42);
                        output.outer = result.outer.result;
                        output.callback = result.callback;
                        output.foreign_exception = result.outer.exception;
                        output.entered = BOOL(result.entered as i32);
                        output.completed = BOOL(result.completed as i32);
                        output.effect = match result.effect {
                            Effect::Unattempted => 0,
                            Effect::Attempted => 1,
                            Effect::Verified => 2,
                        };
                    }
                    Err(call) => {
                        output.outer = call.result;
                        output.foreign_exception = call.exception;
                    }
                }
            } else {
                return E_INVALIDARG;
            }
            S_OK
        })
    })
}
