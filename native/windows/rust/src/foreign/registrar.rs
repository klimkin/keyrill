use super::{Call, Lease, quarantine, returned_interface};
use crate::profiles::{Profile, SERVICE_ID};
use std::ffi::c_void;
use windows::Win32::Foundation::{E_UNEXPECTED, S_OK};
use windows::Win32::UI::Input::KeyboardAndMouse::HKL;
use windows::Win32::UI::TextServices::{
    IEnumTfLanguageProfiles, ITfCategoryMgr, ITfInputProcessorProfiles, TF_LANGUAGEPROFILE,
};
use windows_core::{BOOL, GUID, HRESULT, Interface};

unsafe extern "C" {
    fn kr_registrar_create(category: BOOL, output: *mut *mut c_void) -> Call;
    fn kr_registrar_entries(manager: *mut c_void, language: u16, output: *mut *mut c_void) -> Call;
    fn kr_registrar_next(
        entries: *mut c_void,
        entry: *mut TF_LANGUAGEPROFILE,
        fetched: *mut u32,
    ) -> Call;
    fn kr_registrar_description(
        manager: *mut c_void,
        service: *const GUID,
        language: u16,
        profile: *const GUID,
        output: *mut *const u16,
    ) -> Call;
    fn kr_registrar_enable(
        manager: *mut c_void,
        service: *const GUID,
        language: u16,
        profile: *const GUID,
        enabled: BOOL,
    ) -> Call;
    fn kr_registrar_enabled(
        manager: *mut c_void,
        service: *const GUID,
        language: u16,
        profile: *const GUID,
        output: *mut BOOL,
    ) -> Call;
    fn kr_registrar_service(manager: *mut c_void, service: *const GUID, install: BOOL) -> Call;
    fn kr_registrar_add(
        manager: *mut c_void,
        service: *const GUID,
        language: u16,
        profile: *const GUID,
        description: *const u16,
        description_length: u32,
        dll: *const u16,
        dll_length: u32,
    ) -> Call;
    fn kr_registrar_substitute(
        manager: *mut c_void,
        service: *const GUID,
        language: u16,
        profile: *const GUID,
        layout: HKL,
    ) -> Call;
    fn kr_registrar_category(manager: *mut c_void, service: *const GUID, install: BOOL) -> Call;
}

fn pointer<T: Interface>(value: &Lease<T>) -> *mut c_void {
    value.address() as *mut c_void
}

fn observed(call: Call) -> Call {
    if call.exception != 0 {
        quarantine();
    }
    call
}

fn acquired<T: Interface>(call: Call, output: *mut c_void) -> (Call, Result<Lease<T>, HRESULT>) {
    let returned = returned_interface::<T>(call, output);
    if call.exception != 0 || call.result != S_OK {
        let result = if call.exception == 0 && call.result.is_err() {
            call.result
        } else {
            E_UNEXPECTED
        };
        return (call, Err(result));
    }
    (call, returned.value.ok_or(E_UNEXPECTED))
}

pub(crate) fn profiles() -> (Call, Result<Lease<ITfInputProcessorProfiles>, HRESULT>) {
    let mut output = std::ptr::null_mut();
    // SAFETY: the selected class returns ITfInputProcessorProfiles into a writable pointer slot.
    let call = unsafe { kr_registrar_create(BOOL(0), &mut output) };
    acquired(call, output)
}

pub(crate) fn categories() -> (Call, Result<Lease<ITfCategoryMgr>, HRESULT>) {
    let mut output = std::ptr::null_mut();
    // SAFETY: the selected class returns ITfCategoryMgr into a writable pointer slot.
    let call = unsafe { kr_registrar_create(BOOL(1), &mut output) };
    acquired(call, output)
}

pub(crate) fn entries(
    manager: &Lease<ITfInputProcessorProfiles>,
    language: u16,
) -> (Call, Result<Lease<IEnumTfLanguageProfiles>, HRESULT>) {
    let mut output = std::ptr::null_mut();
    // SAFETY: the lease retains the manager and output is a writable enumerator result slot.
    let call = unsafe { kr_registrar_entries(pointer(manager), language, &mut output) };
    acquired(call, output)
}

pub(crate) fn next(
    entries: &Lease<IEnumTfLanguageProfiles>,
    entry: &mut TF_LANGUAGEPROFILE,
    fetched: &mut u32,
) -> Call {
    // SAFETY: the lease retains the enumerator and both outputs are distinct writable storage.
    observed(unsafe { kr_registrar_next(pointer(entries), entry, fetched) })
}

pub(crate) fn description(
    manager: &Lease<ITfInputProcessorProfiles>,
    profile: &Profile,
    output: &mut *const u16,
) -> Call {
    // SAFETY: manager and immutable GUIDs are live and output is a writable BSTR result slot.
    observed(unsafe {
        kr_registrar_description(
            pointer(manager),
            &SERVICE_ID,
            profile.language,
            &profile.id,
            output,
        )
    })
}

pub(crate) fn enable(
    manager: &Lease<ITfInputProcessorProfiles>,
    profile: &Profile,
    enabled: bool,
) -> Call {
    // SAFETY: the lease and immutable GUIDs remain live for the complete call.
    observed(unsafe {
        kr_registrar_enable(
            pointer(manager),
            &SERVICE_ID,
            profile.language,
            &profile.id,
            BOOL::from(enabled),
        )
    })
}

pub(crate) fn enabled(
    manager: &Lease<ITfInputProcessorProfiles>,
    profile: &Profile,
    output: &mut BOOL,
) -> Call {
    // SAFETY: manager and GUIDs are live and output is a writable BOOL result slot.
    observed(unsafe {
        kr_registrar_enabled(
            pointer(manager),
            &SERVICE_ID,
            profile.language,
            &profile.id,
            output,
        )
    })
}

pub(crate) fn service(manager: &Lease<ITfInputProcessorProfiles>, install: bool) -> Call {
    // SAFETY: manager and immutable service GUID remain live throughout the call.
    observed(unsafe { kr_registrar_service(pointer(manager), &SERVICE_ID, BOOL::from(install)) })
}

pub(crate) fn add(
    manager: &Lease<ITfInputProcessorProfiles>,
    profile: &Profile,
    dll: &[u16],
) -> Call {
    let description: Vec<u16> = profile.description.encode_utf16().collect();
    // SAFETY: buffers/GUIDs are live, and lengths fit u32 because CLI paths are capped at 32768 units.
    observed(unsafe {
        kr_registrar_add(
            pointer(manager),
            &SERVICE_ID,
            profile.language,
            &profile.id,
            description.as_ptr(),
            description.len() as u32,
            dll.as_ptr(),
            (dll.len() - 1) as u32,
        )
    })
}

pub(crate) fn substitute(
    manager: &Lease<ITfInputProcessorProfiles>,
    profile: &Profile,
    layout: HKL,
) -> Call {
    // SAFETY: manager and immutable GUIDs remain live throughout the call.
    observed(unsafe {
        kr_registrar_substitute(
            pointer(manager),
            &SERVICE_ID,
            profile.language,
            &profile.id,
            layout,
        )
    })
}

pub(crate) fn category(manager: &Lease<ITfCategoryMgr>, install: bool) -> Call {
    // SAFETY: manager and immutable service GUID remain live throughout the call.
    observed(unsafe { kr_registrar_category(pointer(manager), &SERVICE_ID, BOOL::from(install)) })
}
