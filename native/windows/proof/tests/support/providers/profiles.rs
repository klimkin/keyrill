use super::*;

pub(super) const ITFINPUTPROCESSORPROFILEMGR_TABLE: ITfInputProcessorProfileMgr_Vtbl =
    ITfInputProcessorProfileMgr_Vtbl {
        base__: UNKNOWN,
        ActivateProfile: ITfInputProcessorProfileMgr_ActivateProfile,
        DeactivateProfile: ITfInputProcessorProfileMgr_DeactivateProfile,
        GetProfile: ITfInputProcessorProfileMgr_GetProfile,
        EnumProfiles: ITfInputProcessorProfileMgr_EnumProfiles,
        ReleaseInputProcessor: ITfInputProcessorProfileMgr_ReleaseInputProcessor,
        RegisterProfile: ITfInputProcessorProfileMgr_RegisterProfile,
        UnregisterProfile: ITfInputProcessorProfileMgr_UnregisterProfile,
        GetActiveProfile: ITfInputProcessorProfileMgr_GetActiveProfile,
    };
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileMgr_ActivateProfile(
    this: *mut core::ffi::c_void,
    a1: u32,
    a2: u16,
    a3: *const windows_core::GUID,
    a4: *const windows_core::GUID,
    a5: windows::Win32::UI::Input::KeyboardAndMouse::HKL,
    a6: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            source::fixed(a1, a2, a3, a4, a5, a6);
            let state = p.profile();
            assert_eq!(state.language.get(), 0x419);
            assert_eq!(state.activation_calls.get(), 0);
            state.activation_calls.set(1);
            state.profile.set(source_profile(1));
            p.hit("profile.activate");
            p.result("ActivateProfile")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileMgr_DeactivateProfile(
    this: *mut core::ffi::c_void,
    a1: u32,
    a2: u16,
    a3: *const windows_core::GUID,
    a4: *const windows_core::GUID,
    a5: windows::Win32::UI::Input::KeyboardAndMouse::HKL,
    a6: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("DeactivateProfile");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileMgr_GetProfile(
    this: *mut core::ffi::c_void,
    a1: u32,
    a2: u16,
    a3: *const windows_core::GUID,
    a4: *const windows_core::GUID,
    a5: windows::Win32::UI::Input::KeyboardAndMouse::HKL,
    a6: *mut TF_INPUTPROCESSORPROFILE,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            source::fixed(a1, a2, a3, a4, a5, 0);
            p.hit("target.read");
            a6.write(source_profile(1));
            p.result("GetProfile")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileMgr_EnumProfiles(
    this: *mut core::ffi::c_void,
    a1: u16,
    a2: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("EnumProfiles");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileMgr_ReleaseInputProcessor(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("ReleaseInputProcessor");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileMgr_RegisterProfile(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: u16,
    a3: *const windows_core::GUID,
    a4: windows_core::PCWSTR,
    a5: u32,
    a6: windows_core::PCWSTR,
    a7: u32,
    a8: u32,
    a9: windows::Win32::UI::Input::KeyboardAndMouse::HKL,
    a10: u32,
    a11: windows_core::BOOL,
    a12: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("RegisterProfile");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileMgr_UnregisterProfile(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: u16,
    a3: *const windows_core::GUID,
    a4: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("UnregisterProfile");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileMgr_GetActiveProfile(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: *mut TF_INPUTPROCESSORPROFILE,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetActiveProfile");
            p.hit("profile.read");
            a2.write(p.profile().profile.get());
            p.result("GetActiveProfile")
        })
    }
}
pub(super) const ITFINPUTPROCESSORPROFILESUBSTITUTELAYOUT_TABLE:
    ITfInputProcessorProfileSubstituteLayout_Vtbl = ITfInputProcessorProfileSubstituteLayout_Vtbl {
    base__: UNKNOWN,
    GetSubstituteKeyboardLayout:
        ITfInputProcessorProfileSubstituteLayout_GetSubstituteKeyboardLayout,
};
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfInputProcessorProfileSubstituteLayout_GetSubstituteKeyboardLayout(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: u16,
    a3: *const windows_core::GUID,
    a4: *mut windows::Win32::UI::Input::KeyboardAndMouse::HKL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetSubstituteKeyboardLayout");
            a4.write(p.profile().substitute.get());
            p.result("GetSubstituteKeyboardLayout")
        })
    }
}
