use super::*;

pub(super) const ITFTHREADMGR_TABLE: ITfThreadMgr_Vtbl = ITfThreadMgr_Vtbl {
    base__: UNKNOWN,
    Activate: ITfThreadMgr_Activate,
    Deactivate: ITfThreadMgr_Deactivate,
    CreateDocumentMgr: ITfThreadMgr_CreateDocumentMgr,
    EnumDocumentMgrs: ITfThreadMgr_EnumDocumentMgrs,
    GetFocus: ITfThreadMgr_GetFocus,
    SetFocus: ITfThreadMgr_SetFocus,
    AssociateFocus: ITfThreadMgr_AssociateFocus,
    IsThreadFocus: ITfThreadMgr_IsThreadFocus,
    GetFunctionProvider: ITfThreadMgr_GetFunctionProvider,
    EnumFunctionProviders: ITfThreadMgr_EnumFunctionProviders,
    GetGlobalCompartment: ITfThreadMgr_GetGlobalCompartment,
};
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_Activate(
    this: *mut core::ffi::c_void,
    a1: *mut u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("Activate");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_Deactivate(
    this: *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("Deactivate");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_CreateDocumentMgr(
    this: *mut core::ffi::c_void,
    a1: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("CreateDocumentMgr");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_EnumDocumentMgrs(
    this: *mut core::ffi::c_void,
    a1: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("EnumDocumentMgrs");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_GetFocus(
    this: *mut core::ffi::c_void,
    a1: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetFocus");
            a1.write(p.own(ITfDocumentMgr::IID));
            p.result("GetFocus")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_SetFocus(
    this: *mut core::ffi::c_void,
    a1: *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("SetFocus");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_AssociateFocus(
    this: *mut core::ffi::c_void,
    a1: windows::Win32::Foundation::HWND,
    a2: *mut core::ffi::c_void,
    a3: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("AssociateFocus");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_IsThreadFocus(
    this: *mut core::ffi::c_void,
    a1: *mut windows_core::BOOL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("IsThreadFocus");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_GetFunctionProvider(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetFunctionProvider");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_EnumFunctionProviders(
    this: *mut core::ffi::c_void,
    a1: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("EnumFunctionProviders");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfThreadMgr_GetGlobalCompartment(
    this: *mut core::ffi::c_void,
    a1: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetGlobalCompartment");
            E_NOTIMPL
        })
    }
}
pub(super) const ITFDOCUMENTMGR_TABLE: ITfDocumentMgr_Vtbl = ITfDocumentMgr_Vtbl {
    base__: UNKNOWN,
    CreateContext: ITfDocumentMgr_CreateContext,
    Push: ITfDocumentMgr_Push,
    Pop: ITfDocumentMgr_Pop,
    GetTop: ITfDocumentMgr_GetTop,
    GetBase: ITfDocumentMgr_GetBase,
    EnumContexts: ITfDocumentMgr_EnumContexts,
};
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfDocumentMgr_CreateContext(
    this: *mut core::ffi::c_void,
    a1: u32,
    a2: u32,
    a3: *mut core::ffi::c_void,
    a4: *mut *mut core::ffi::c_void,
    a5: *mut u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("CreateContext");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfDocumentMgr_Push(
    this: *mut core::ffi::c_void,
    a1: *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("Push");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfDocumentMgr_Pop(
    this: *mut core::ffi::c_void,
    a1: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("Pop");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfDocumentMgr_GetTop(
    this: *mut core::ffi::c_void,
    a1: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetTop");
            let pointer = p.manager().top.borrow().as_raw();
            a1.write(retain(pointer));
            p.result("GetTop")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfDocumentMgr_GetBase(
    this: *mut core::ffi::c_void,
    a1: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetBase");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfDocumentMgr_EnumContexts(
    this: *mut core::ffi::c_void,
    a1: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("EnumContexts");
            E_NOTIMPL
        })
    }
}
pub(super) const ITFSOURCE_TABLE: ITfSource_Vtbl = ITfSource_Vtbl {
    base__: UNKNOWN,
    AdviseSink: ITfSource_AdviseSink,
    UnadviseSink: ITfSource_UnadviseSink,
};
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfSource_AdviseSink(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: *mut core::ffi::c_void,
    a3: *mut u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("AdviseSink");
            a3.write(if *a1 == ITfThreadMgrEventSink::IID {
                1
            } else {
                2
            });
            p.result("AdviseSink")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfSource_UnadviseSink(
    this: *mut core::ffi::c_void,
    a1: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("UnadviseSink");
            p.result("UnadviseSink")
        })
    }
}
pub(super) const ITFSOURCESINGLE_TABLE: ITfSourceSingle_Vtbl = ITfSourceSingle_Vtbl {
    base__: UNKNOWN,
    AdviseSingleSink: ITfSourceSingle_AdviseSingleSink,
    UnadviseSingleSink: ITfSourceSingle_UnadviseSingleSink,
};
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfSourceSingle_AdviseSingleSink(
    this: *mut core::ffi::c_void,
    a1: u32,
    a2: *const windows_core::GUID,
    a3: *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("AdviseSingleSink");
            replace_reference(
                &p.manager().diagnostic,
                Some(IUnknown::from_raw(retain(a3))),
            );
            p.result("AdviseSingleSink")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfSourceSingle_UnadviseSingleSink(
    this: *mut core::ffi::c_void,
    a1: u32,
    a2: *const windows_core::GUID,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("UnadviseSingleSink");
            replace_reference(&p.manager().diagnostic, None);
            p.result("UnadviseSingleSink")
        })
    }
}
pub(super) const ITFKEYSTROKEMGR_TABLE: ITfKeystrokeMgr_Vtbl = ITfKeystrokeMgr_Vtbl {
    base__: UNKNOWN,
    AdviseKeyEventSink: ITfKeystrokeMgr_AdviseKeyEventSink,
    UnadviseKeyEventSink: ITfKeystrokeMgr_UnadviseKeyEventSink,
    GetForeground: ITfKeystrokeMgr_GetForeground,
    TestKeyDown: ITfKeystrokeMgr_TestKeyDown,
    TestKeyUp: ITfKeystrokeMgr_TestKeyUp,
    KeyDown: ITfKeystrokeMgr_KeyDown,
    KeyUp: ITfKeystrokeMgr_KeyUp,
    GetPreservedKey: ITfKeystrokeMgr_GetPreservedKey,
    IsPreservedKey: ITfKeystrokeMgr_IsPreservedKey,
    PreserveKey: ITfKeystrokeMgr_PreserveKey,
    UnpreserveKey: ITfKeystrokeMgr_UnpreserveKey,
    SetPreservedKeyDescription: ITfKeystrokeMgr_SetPreservedKeyDescription,
    GetPreservedKeyDescription: ITfKeystrokeMgr_GetPreservedKeyDescription,
    SimulatePreservedKey: ITfKeystrokeMgr_SimulatePreservedKey,
};
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_AdviseKeyEventSink(
    this: *mut core::ffi::c_void,
    a1: u32,
    a2: *mut core::ffi::c_void,
    a3: windows_core::BOOL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("AdviseKeyEventSink");
            p.result("AdviseKeyEventSink")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_UnadviseKeyEventSink(
    this: *mut core::ffi::c_void,
    a1: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("UnadviseKeyEventSink");
            p.result("UnadviseKeyEventSink")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_GetForeground(
    this: *mut core::ffi::c_void,
    a1: *mut windows_core::GUID,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetForeground");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_TestKeyDown(
    this: *mut core::ffi::c_void,
    a1: windows::Win32::Foundation::WPARAM,
    a2: windows::Win32::Foundation::LPARAM,
    a3: *mut windows_core::BOOL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("TestKeyDown");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_TestKeyUp(
    this: *mut core::ffi::c_void,
    a1: windows::Win32::Foundation::WPARAM,
    a2: windows::Win32::Foundation::LPARAM,
    a3: *mut windows_core::BOOL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("TestKeyUp");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_KeyDown(
    this: *mut core::ffi::c_void,
    a1: windows::Win32::Foundation::WPARAM,
    a2: windows::Win32::Foundation::LPARAM,
    a3: *mut windows_core::BOOL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("KeyDown");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_KeyUp(
    this: *mut core::ffi::c_void,
    a1: windows::Win32::Foundation::WPARAM,
    a2: windows::Win32::Foundation::LPARAM,
    a3: *mut windows_core::BOOL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("KeyUp");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_GetPreservedKey(
    this: *mut core::ffi::c_void,
    a1: *mut core::ffi::c_void,
    a2: *const TF_PRESERVEDKEY,
    a3: *mut windows_core::GUID,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetPreservedKey");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_IsPreservedKey(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: *const TF_PRESERVEDKEY,
    a3: *mut windows_core::BOOL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("IsPreservedKey");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_PreserveKey(
    this: *mut core::ffi::c_void,
    a1: u32,
    a2: *const windows_core::GUID,
    a3: *const TF_PRESERVEDKEY,
    a4: windows_core::PCWSTR,
    a5: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("PreserveKey");
            p.result("PreserveKey")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_UnpreserveKey(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: *const TF_PRESERVEDKEY,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("UnpreserveKey");
            p.result("UnpreserveKey")
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_SetPreservedKeyDescription(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: windows_core::PCWSTR,
    a3: u32,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("SetPreservedKeyDescription");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_GetPreservedKeyDescription(
    this: *mut core::ffi::c_void,
    a1: *const windows_core::GUID,
    a2: *mut *mut core::ffi::c_void,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("GetPreservedKeyDescription");
            E_NOTIMPL
        })
    }
}
// SAFETY: TSF supplies the declared SDK arguments; all host state is owned by this slot.
unsafe extern "system" fn ITfKeystrokeMgr_SimulatePreservedKey(
    this: *mut core::ffi::c_void,
    a1: *mut core::ffi::c_void,
    a2: *const windows_core::GUID,
    a3: *mut windows_core::BOOL,
) -> windows_core::HRESULT {
    unsafe {
        boundary(this, |p| {
            p.hit("SimulatePreservedKey");
            E_NOTIMPL
        })
    }
}
