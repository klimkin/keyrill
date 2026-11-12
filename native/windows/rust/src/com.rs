use crate::foreign;
use crate::service::{LOCKS, OBJECTS, ObjectLease, Service, WORKERS};
use std::ffi::c_void;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::rc::Rc;
use std::sync::atomic::Ordering;
use windows::Win32::Foundation::{
    CLASS_E_CLASSNOTAVAILABLE, CLASS_E_NOAGGREGATION, E_NOINTERFACE, E_POINTER, E_UNEXPECTED,
    LPARAM, S_FALSE, S_OK, WPARAM,
};
use windows::Win32::System::Com::{IClassFactory, IClassFactory_Impl};
use windows::Win32::UI::TextServices::{
    ITfFunctionProvider, ITfFunctionProvider_Vtbl, ITfKeyEventSink, ITfKeyEventSink_Vtbl,
    ITfTextInputProcessor_Impl, ITfTextInputProcessorEx, ITfTextInputProcessorEx_Impl,
    ITfThreadMgr,
};
use windows_core::{
    BOOL, BSTR, GUID, HRESULT, IUnknown, IUnknown_Vtbl, Interface, Ref, Result, implement,
    interface,
};

pub(crate) use crate::profiles::SERVICE_ID;

// Raw mirrors retain the SDK's output-pointer checks and exact HRESULT semantics.
#[interface("aa80e7f5-2021-11d2-93e0-0060b067b86e")]
unsafe trait RawKeyEventSink: IUnknown {
    fn OnSetFocus(&self, foreground: BOOL) -> HRESULT;
    fn OnTestKeyDown(
        &self,
        context: *mut c_void,
        key: WPARAM,
        flags: LPARAM,
        eaten: *mut BOOL,
    ) -> HRESULT;
    fn OnTestKeyUp(
        &self,
        context: *mut c_void,
        key: WPARAM,
        flags: LPARAM,
        eaten: *mut BOOL,
    ) -> HRESULT;
    fn OnKeyDown(
        &self,
        context: *mut c_void,
        key: WPARAM,
        flags: LPARAM,
        eaten: *mut BOOL,
    ) -> HRESULT;
    fn OnKeyUp(
        &self,
        context: *mut c_void,
        key: WPARAM,
        flags: LPARAM,
        eaten: *mut BOOL,
    ) -> HRESULT;
    fn OnPreservedKey(&self, context: *mut c_void, id: *const GUID, eaten: *mut BOOL) -> HRESULT;
}

#[interface("101d6610-0990-11d3-8df0-00105a2799b5")]
unsafe trait RawFunctionProvider: IUnknown {
    fn GetType(&self, output: *mut GUID) -> HRESULT;
    fn GetDescription(&self, output: *mut *mut c_void) -> HRESULT;
    fn GetFunction(
        &self,
        function: *const GUID,
        id: *const GUID,
        output: *mut *mut c_void,
    ) -> HRESULT;
}

const _: () = {
    assert!(RawKeyEventSink::IID.to_u128() == ITfKeyEventSink::IID.to_u128());
    assert!(RawFunctionProvider::IID.to_u128() == ITfFunctionProvider::IID.to_u128());
    assert!(size_of::<RawKeyEventSink_Vtbl>() == size_of::<ITfKeyEventSink_Vtbl>());
    assert!(align_of::<RawKeyEventSink_Vtbl>() == align_of::<ITfKeyEventSink_Vtbl>());
    assert!(size_of::<RawFunctionProvider_Vtbl>() == size_of::<ITfFunctionProvider_Vtbl>());
    assert!(align_of::<RawFunctionProvider_Vtbl>() == align_of::<ITfFunctionProvider_Vtbl>());
};

macro_rules! assert_method {
    ($raw:ty, $sdk:ty, $field:ident) => {
        const _: () =
            assert!(std::mem::offset_of!($raw, $field) == std::mem::offset_of!($sdk, $field));
        const _: fn(&$raw, &$sdk) = |raw, sdk| {
            let mut signature = raw.$field;
            let _ = signature;
            signature = sdk.$field;
            let _ = signature;
        };
    };
}
assert_method!(RawKeyEventSink_Vtbl, ITfKeyEventSink_Vtbl, OnSetFocus);
assert_method!(RawKeyEventSink_Vtbl, ITfKeyEventSink_Vtbl, OnTestKeyDown);
assert_method!(RawKeyEventSink_Vtbl, ITfKeyEventSink_Vtbl, OnTestKeyUp);
assert_method!(RawKeyEventSink_Vtbl, ITfKeyEventSink_Vtbl, OnKeyDown);
assert_method!(RawKeyEventSink_Vtbl, ITfKeyEventSink_Vtbl, OnKeyUp);
assert_method!(RawKeyEventSink_Vtbl, ITfKeyEventSink_Vtbl, OnPreservedKey);
assert_method!(RawFunctionProvider_Vtbl, ITfFunctionProvider_Vtbl, GetType);
assert_method!(
    RawFunctionProvider_Vtbl,
    ITfFunctionProvider_Vtbl,
    GetDescription
);
assert_method!(
    RawFunctionProvider_Vtbl,
    ITfFunctionProvider_Vtbl,
    GetFunction
);

#[implement(
    ITfTextInputProcessorEx,
    RawKeyEventSink,
    RawFunctionProvider,
    Agile = false
)]
struct ServiceCom {
    service: Rc<Service>,
}

pub(crate) fn create(service: Rc<Service>) -> ITfTextInputProcessorEx {
    ServiceCom { service }.into()
}

impl ITfTextInputProcessor_Impl for ServiceCom_Impl {
    fn Activate(&self, manager: Ref<'_, ITfThreadMgr>, _: u32) -> Result<()> {
        self.service.activate(manager.is_null()).ok()
    }

    fn Deactivate(&self) -> Result<()> {
        self.service.deactivate().ok()
    }
}

impl ITfTextInputProcessorEx_Impl for ServiceCom_Impl {
    fn ActivateEx(&self, manager: Ref<'_, ITfThreadMgr>, client: u32, _: u32) -> Result<()> {
        self.Activate(manager, client)
    }
}

fn key(service: &Service, eaten: *mut BOOL, release: bool) -> HRESULT {
    if eaten.is_null() {
        return E_POINTER;
    }
    // SAFETY: each raw COM method requires one writable BOOL when this pointer is nonnull.
    unsafe { eaten.write(BOOL(0)) };
    service.key(release)
}

impl RawKeyEventSink_Impl for ServiceCom_Impl {
    unsafe fn OnSetFocus(&self, _: BOOL) -> HRESULT {
        self.service.checked(|| {
            self.service.invalidate();
            S_OK
        })
    }

    unsafe fn OnTestKeyDown(
        &self,
        _: *mut c_void,
        _: WPARAM,
        _: LPARAM,
        eaten: *mut BOOL,
    ) -> HRESULT {
        key(&self.service, eaten, false)
    }

    unsafe fn OnTestKeyUp(
        &self,
        _: *mut c_void,
        _: WPARAM,
        _: LPARAM,
        eaten: *mut BOOL,
    ) -> HRESULT {
        key(&self.service, eaten, true)
    }

    unsafe fn OnKeyDown(&self, _: *mut c_void, _: WPARAM, _: LPARAM, eaten: *mut BOOL) -> HRESULT {
        key(&self.service, eaten, false)
    }

    unsafe fn OnKeyUp(&self, _: *mut c_void, _: WPARAM, _: LPARAM, eaten: *mut BOOL) -> HRESULT {
        key(&self.service, eaten, true)
    }

    unsafe fn OnPreservedKey(&self, _: *mut c_void, _: *const GUID, eaten: *mut BOOL) -> HRESULT {
        key(&self.service, eaten, false)
    }
}

impl RawFunctionProvider_Impl for ServiceCom_Impl {
    unsafe fn GetType(&self, output: *mut GUID) -> HRESULT {
        if output.is_null() {
            return E_POINTER;
        }
        self.service.entry(|| {
            // SAFETY: the COM caller supplies one writable GUID.
            unsafe { output.write(SERVICE_ID) };
            S_OK
        })
    }

    unsafe fn GetDescription(&self, output: *mut *mut c_void) -> HRESULT {
        if output.is_null() {
            return E_POINTER;
        }
        // SAFETY: the COM caller supplies a writable pointer slot.
        unsafe { output.write(std::ptr::null_mut()) };
        self.service.entry(|| {
            let description = BSTR::from("Keyrill proof service diagnostics");
            // SAFETY: ownership of the allocated BSTR is transferred to the caller.
            unsafe { output.write(description.into_raw().cast_mut().cast()) };
            S_OK
        })
    }

    unsafe fn GetFunction(
        &self,
        _: *const GUID,
        _: *const GUID,
        output: *mut *mut c_void,
    ) -> HRESULT {
        if output.is_null() {
            return E_POINTER;
        }
        // SAFETY: the COM caller supplies a writable pointer slot.
        unsafe { output.write(std::ptr::null_mut()) };
        self.service.entry(|| E_NOINTERFACE)
    }
}

#[implement(IClassFactory, Agile = false)]
struct Factory {
    _object: ObjectLease,
}

fn fence(call: impl FnOnce() -> HRESULT) -> HRESULT {
    catch_unwind(AssertUnwindSafe(call)).unwrap_or(E_UNEXPECTED)
}

impl IClassFactory_Impl for Factory_Impl {
    fn CreateInstance(
        &self,
        outer: Ref<'_, IUnknown>,
        id: *const GUID,
        output: *mut *mut c_void,
    ) -> Result<()> {
        fence(|| {
            if output.is_null() {
                return E_POINTER;
            }
            // SAFETY: COM supplies a writable output slot before any object creation.
            unsafe { output.write(std::ptr::null_mut()) };
            if id.is_null() {
                return E_POINTER;
            }
            if !outer.is_null() {
                return CLASS_E_NOAGGREGATION;
            }
            let service = create(Service::new());
            // SAFETY: this is our generated object, with a valid IID and writable output slot.
            unsafe { service.query(id, output) }
        })
        .ok()
    }

    fn LockServer(&self, lock: BOOL) -> Result<()> {
        fence(|| {
            if lock.as_bool() {
                LOCKS.fetch_add(1, Ordering::AcqRel);
            } else {
                let _ = LOCKS.try_update(Ordering::AcqRel, Ordering::Acquire, |locks| {
                    locks.checked_sub(1)
                });
            }
            S_OK
        })
        .ok()
    }
}

/// Return a class factory for the Windows TSF service.
///
/// # Safety
/// GUID pointers must identify readable GUIDs and `output` must identify a writable pointer slot.
#[unsafe(no_mangle)]
pub unsafe extern "system" fn DllGetClassObject(
    class: *const GUID,
    id: *const GUID,
    output: *mut *mut c_void,
) -> HRESULT {
    fence(|| {
        if output.is_null() {
            return E_POINTER;
        }
        // SAFETY: caller supplies a writable pointer slot.
        unsafe { output.write(std::ptr::null_mut()) };
        if class.is_null() || id.is_null() {
            return E_POINTER;
        }
        // SAFETY: both GUID pointers are live readable values under the export contract.
        if unsafe { *class } != SERVICE_ID {
            return CLASS_E_CLASSNOTAVAILABLE;
        }
        let factory: IClassFactory = Factory {
            _object: ObjectLease::new(),
        }
        .into();
        // SAFETY: the factory is our generated object; id/output were checked above.
        unsafe { factory.query(id, output) }
    })
}

/// Report whether COM objects, workers, server locks and uncertain foreign ownership have drained.
#[unsafe(no_mangle)]
pub extern "system" fn DllCanUnloadNow() -> HRESULT {
    if OBJECTS.load(Ordering::Acquire) == 0
        && LOCKS.load(Ordering::Acquire) == 0
        && WORKERS.load(Ordering::Acquire) == 0
        && !foreign::uncertain()
    {
        S_OK
    } else {
        S_FALSE
    }
}
