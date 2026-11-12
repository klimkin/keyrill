//! Typed TSF calls. The C++ side invokes each host method inside its exception fence.

use super::{Call, InterfaceError, Lease, quarantine, returned_interface};
use crate::keys::Binding;
use std::ffi::c_void;
use windows::Win32::Foundation::{E_UNEXPECTED, S_OK};
use windows::Win32::UI::Input::KeyboardAndMouse::HKL;
use windows::Win32::UI::TextServices::*;
use windows_core::{GUID, HRESULT, Interface};

unsafe extern "C" {
    fn kr_foreign_advise_single(source: *mut c_void, client: u32, provider: *mut c_void) -> Call;
    fn kr_foreign_unadvise_single(source: *mut c_void, client: u32) -> Call;
    fn kr_foreign_advise(
        source: *mut c_void,
        id: *const GUID,
        sink: *mut c_void,
        cookie: *mut u32,
    ) -> Call;
    fn kr_foreign_unadvise(source: *mut c_void, cookie: u32) -> Call;
    fn kr_foreign_advise_keys(keys: *mut c_void, client: u32, sink: *mut c_void) -> Call;
    fn kr_foreign_unadvise_keys(keys: *mut c_void, client: u32) -> Call;
    fn kr_foreign_preserve(
        keys: *mut c_void,
        client: u32,
        id: *const GUID,
        key: *const TF_PRESERVEDKEY,
        description: *const u16,
        length: u32,
    ) -> Call;
    fn kr_foreign_unpreserve(
        keys: *mut c_void,
        id: *const GUID,
        key: *const TF_PRESERVEDKEY,
    ) -> Call;
    fn kr_foreign_create_profiles(output: *mut *mut c_void) -> Call;
    fn kr_foreign_active_profile(
        profiles: *mut c_void,
        output: *mut TF_INPUTPROCESSORPROFILE,
    ) -> Call;
    fn kr_foreign_substitute(
        profiles: *mut c_void,
        service: *const GUID,
        language: u16,
        profile: *const GUID,
        output: *mut HKL,
    ) -> Call;
    fn kr_foreign_layouts(
        call: Option<unsafe extern "system" fn(i32, *mut isize) -> i32>,
        count: i32,
        layouts: *mut HKL,
        output: *mut i32,
    ) -> Call;
}

pub(crate) fn advise_single(
    source: &Lease<ITfSourceSingle>,
    client: u32,
    provider: *mut c_void,
) -> Call {
    // SAFETY: source is retained and the caller retains its own generated provider interface.
    unsafe { kr_foreign_advise_single(pointer(source), client, provider) }
}

pub(crate) fn unadvise_single(source: &Lease<ITfSourceSingle>, client: u32) {
    // SAFETY: source is retained through the one diagnostic-registration surrender.
    let call = unsafe { kr_foreign_unadvise_single(pointer(source), client) };
    if call.exception != 0 {
        quarantine();
    }
}

fn pointer<T: Interface>(lease: &Lease<T>) -> *mut c_void {
    lease.owner.pointer.as_ptr()
}

pub(crate) fn acquired<T: Interface>(
    call: Call,
    output: *mut c_void,
) -> Result<Lease<T>, InterfaceError> {
    let output = returned_interface::<T>(call, output).value;
    if !call.exact() {
        return Err(InterfaceError::Native(call));
    }
    output.ok_or(InterfaceError::Missing)
}

pub(crate) fn advise(source: &Lease<ITfSource>, id: &GUID, sink: *mut c_void) -> (Call, u32) {
    let mut cookie = TF_INVALID_COOKIE;
    // SAFETY: the lease retains source, and the caller retains its own generated COM sink.
    let call = unsafe { kr_foreign_advise(pointer(source), id, sink, &mut cookie) };
    (call, cookie)
}

pub(crate) fn unadvise(source: &Lease<ITfSource>, cookie: u32) -> Call {
    // SAFETY: source is retained while the captured registration is surrendered once.
    unsafe { kr_foreign_unadvise(pointer(source), cookie) }
}

pub(crate) fn advise_keys(keys: &Lease<ITfKeystrokeMgr>, client: u32, sink: *mut c_void) -> Call {
    // SAFETY: the lease retains keys and the caller retains its own generated key sink.
    unsafe { kr_foreign_advise_keys(pointer(keys), client, sink) }
}

pub(crate) fn unadvise_keys(keys: &Lease<ITfKeystrokeMgr>, client: u32) -> Call {
    // SAFETY: keys is retained while the captured registration is surrendered once.
    unsafe { kr_foreign_unadvise_keys(pointer(keys), client) }
}

pub(crate) fn preserve(keys: &Lease<ITfKeystrokeMgr>, client: u32, binding: &Binding) -> Call {
    let description: Vec<u16> = binding.description.encode_utf16().collect();
    // SAFETY: the lease and immutable binding/output buffers remain live through the fenced call.
    unsafe {
        kr_foreign_preserve(
            pointer(keys),
            client,
            &binding.id,
            &binding.key,
            description.as_ptr(),
            description.len() as u32,
        )
    }
}

pub(crate) fn unpreserve(keys: &Lease<ITfKeystrokeMgr>, binding: &Binding) -> Call {
    // SAFETY: keys and this captured immutable binding remain live through the fenced call.
    unsafe { kr_foreign_unpreserve(pointer(keys), &binding.id, &binding.key) }
}

pub(crate) fn create_profiles() -> Result<Lease<ITfInputProcessorProfileMgr>, InterfaceError> {
    let mut output = std::ptr::null_mut();
    // SAFETY: initialized output is distinct writable pointer storage.
    let call = unsafe { kr_foreign_create_profiles(&mut output) };
    acquired(call, output)
}

pub(crate) fn active_profile(
    profiles: &Lease<ITfInputProcessorProfileMgr>,
) -> (Call, TF_INPUTPROCESSORPROFILE) {
    let mut output = TF_INPUTPROCESSORPROFILE::default();
    // SAFETY: the retained manager and initialized SDK output remain live through the call.
    let call = unsafe { kr_foreign_active_profile(pointer(profiles), &mut output) };
    (call, output)
}

pub(crate) fn substitute(
    profiles: &Lease<ITfInputProcessorProfileSubstituteLayout>,
    profile: &TF_INPUTPROCESSORPROFILE,
) -> (Call, HKL) {
    let mut output = HKL::default();
    // SAFETY: manager/profile are retained immutable input, and output is distinct writable storage.
    let call = unsafe {
        kr_foreign_substitute(
            pointer(profiles),
            &profile.clsid,
            profile.langid,
            &profile.guidProfile,
            &mut output,
        )
    };
    (call, output)
}

pub(crate) fn layouts(
    callback: Option<unsafe extern "system" fn(i32, *mut isize) -> i32>,
    output: &mut [HKL],
) -> (Call, i32) {
    let mut count = 0;
    // SAFETY: output is a live bounded array and an injected callback is retained by the dependency owner.
    let call = unsafe {
        kr_foreign_layouts(
            callback,
            output.len() as i32,
            output.as_mut_ptr(),
            &mut count,
        )
    };
    (call, count)
}

pub(crate) fn failure(call: Call) -> HRESULT {
    if call.exception != 0 || call.result == S_OK {
        E_UNEXPECTED
    } else {
        call.result
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use windows::Win32::Foundation::{E_FAIL, E_NOINTERFACE, S_FALSE};
    use windows::Win32::System::Com::{IClassFactory, IClassFactory_Impl};
    use windows_core::{BOOL, IUnknown, Ref, Result, implement};

    #[implement(IClassFactory, Agile = false)]
    struct Controlled {
        retired: Arc<AtomicUsize>,
    }

    impl Drop for Controlled {
        fn drop(&mut self) {
            self.retired.fetch_add(1, Ordering::AcqRel);
        }
    }

    impl IClassFactory_Impl for Controlled_Impl {
        fn CreateInstance(
            &self,
            _: Ref<'_, IUnknown>,
            _: *const GUID,
            output: *mut *mut c_void,
        ) -> Result<()> {
            if !output.is_null() {
                // SAFETY: the SDK contract supplies a writable interface output.
                unsafe { output.write(std::ptr::null_mut()) };
            }
            E_NOINTERFACE.ok()
        }

        fn LockServer(&self, _: BOOL) -> Result<()> {
            Ok(())
        }
    }

    #[test]
    fn refused_owned_outputs_retire_and_exact_null_stays_missing() {
        for result in [S_FALSE, E_FAIL] {
            let retired = Arc::new(AtomicUsize::new(0));
            let object = windows_core::ComObject::new(Controlled {
                retired: Arc::clone(&retired),
            })
            .to_interface::<IUnknown>();
            let output = acquired::<IUnknown>(
                Call {
                    result,
                    exception: 0,
                },
                object.into_raw(),
            );
            let Err(InterfaceError::Native(call)) = output else {
                panic!("nonexact status granted an interface");
            };
            assert_eq!(call.result, result);
            assert_eq!(call.exception, 0);
            assert_eq!(retired.load(Ordering::Acquire), 1);
        }
        assert!(matches!(
            acquired::<IUnknown>(
                Call {
                    result: S_OK,
                    exception: 0
                },
                std::ptr::null_mut()
            ),
            Err(InterfaceError::Missing)
        ));
    }
}
