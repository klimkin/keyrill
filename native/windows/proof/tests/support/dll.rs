use std::{ffi::c_void, path::PathBuf};
use windows::Win32::{Foundation::*, System::LibraryLoader::*};
use windows_core::{HRESULT, PCSTR, PCWSTR};
pub const VERSION: u32 = 2;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HostIdentity {
    pub process: u32,
    pub thread: u32,
    pub started: u64,
    pub kind: u32,
    pub version: [u32; 4],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct FieldWindow {
    pub handle: HWND,
    pub process: u32,
    pub thread: u32,
    pub control: i32,
    pub style: isize,
    pub class_name: [u16; 64],
    pub enabled: u32,
    pub complete: u32,
}
impl Default for FieldWindow {
    fn default() -> Self {
        Self {
            handle: HWND::default(),
            process: 0,
            thread: 0,
            control: 0,
            style: 0,
            class_name: [0; 64],
            enabled: 0,
            complete: 0,
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct FieldFacts {
    pub host: HostIdentity,
    pub process: u32,
    pub thread: u32,
    pub view: HWND,
    pub focus: HWND,
    pub foreground: HWND,
    pub root: HWND,
    pub field: FieldWindow,
    pub parent: FieldWindow,
    pub grandparent: FieldWindow,
    pub proof_root: FieldWindow,
    pub proof_process: u32,
}
#[repr(C)]
pub struct Dependencies {
    pub size: u32,
    pub version: u32,
    pub context: *mut c_void,
    pub retain: Option<unsafe extern "system" fn(*mut c_void)>,
    pub release: Option<unsafe extern "system" fn(*mut c_void)>,
    pub profiles: *mut c_void,
    pub layouts: Option<unsafe extern "system" fn(i32, *mut isize) -> i32>,
    pub capture: Option<
        unsafe extern "system" fn(
            *mut c_void,
            HWND,
            *const HostIdentity,
            *mut FieldFacts,
        ) -> HRESULT,
    >,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Event {
    pub role: u32,
    pub mode: u32,
    pub context: u32,
    pub length: u32,
    pub text: [u16; 8],
}
impl Event {
    pub fn text(unit: u16) -> Self {
        Self {
            role: 1,
            mode: 1,
            context: 1,
            length: 1,
            text: [unit, 0, 0, 0, 0, 0, 0, 0],
        }
    }
    pub fn command(role: u32) -> Self {
        Self {
            role,
            mode: 1,
            context: 1,
            ..Self::default()
        }
    }
}
#[repr(C)]
#[derive(Default, Debug)]
pub struct Probe {
    pub size: u32,
    pub version: u32,
    pub outer: HRESULT,
    pub callback: HRESULT,
    pub reserved: u32,
    pub entered: i32,
    pub completed: i32,
    pub effect: u32,
    pub identity: usize,
}
const _: () = {
    assert!(size_of::<HostIdentity>() == 40);
    assert!(size_of::<FieldWindow>() == 168);
    assert!(size_of::<FieldFacts>() == 760);
    assert!(size_of::<Dependencies>() == 56);
    assert!(size_of::<Event>() == 32);
    assert!(size_of::<Probe>() == 40);
    assert!(std::mem::offset_of!(Dependencies, capture) == 48);
    assert!(std::mem::offset_of!(FieldFacts, proof_process) == 752);
    assert!(std::mem::offset_of!(Probe, identity) == 32);
};
pub struct Api {
    pub create: unsafe extern "system" fn(
        u32,
        *const Dependencies,
        *mut *mut c_void,
        *mut *mut c_void,
    ) -> HRESULT,
    pub release: unsafe extern "system" fn(*mut c_void) -> HRESULT,
    pub event:
        unsafe extern "system" fn(*mut c_void, *mut c_void, *const Event, *mut i32) -> HRESULT,
    pub probe: unsafe extern "system" fn(*mut c_void, *mut c_void, u32, u32, *mut Probe) -> HRESULT,
    pub stopped: unsafe extern "system" fn(*mut c_void, *mut i32) -> HRESULT,
    pub panic: unsafe extern "system" fn(*mut c_void, u32) -> HRESULT,
}
impl Api {
    pub fn load() -> Self {
        let path = PathBuf::from(
            std::env::var_os("KEYRILL_TEST_DLL").expect("runner must supply KEYRILL_TEST_DLL"),
        );
        assert!(path.is_absolute());
        let path: Vec<u16> = path
            .as_os_str()
            .to_string_lossy()
            .encode_utf16()
            .chain(Some(0))
            .collect();
        // The controlled runner deliberately retains the module reference until process exit.
        let module =
            unsafe { LoadLibraryW(PCWSTR(path.as_ptr())).expect("load explicit test DLL") };
        unsafe fn symbol<T: Copy>(module: HMODULE, name: &[u8]) -> T {
            let value = unsafe { GetProcAddress(module, PCSTR(name.as_ptr())) }
                .expect("required test export");
            assert_eq!(size_of::<T>(), size_of_val(&value));
            unsafe { std::mem::transmute_copy(&value) }
        }
        unsafe {
            let digest: safe_digest::Call = symbol(module, b"KeyrillTestBuildDigest\0");
            let mut actual = [0; 8];
            assert_eq!(digest(8, actual.as_mut_ptr()), S_OK);
            let expected = std::env::var("KEYRILL_TEST_BUILD_DIGEST")
                .expect("runner must supply matching build digest");
            let words: Vec<_> = expected
                .split(',')
                .map(|s| {
                    u32::from_str_radix(s.trim_start_matches("0x"), 16).expect("hex digest word")
                })
                .collect();
            assert_eq!(
                actual.as_slice(),
                words.as_slice(),
                "loaded DLL build identity"
            );
            Self {
                create: symbol(module, b"KeyrillTestCreate\0"),
                release: symbol(module, b"KeyrillTestRelease\0"),
                event: symbol(module, b"KeyrillTestEvent\0"),
                probe: symbol(module, b"KeyrillTestProbe\0"),
                stopped: symbol(module, b"KeyrillTestStopped\0"),
                panic: symbol(module, b"KeyrillTestPanic\0"),
            }
        }
    }
}
mod safe_digest {
    pub type Call = unsafe extern "system" fn(u32, *mut u32) -> windows_core::HRESULT;
}
