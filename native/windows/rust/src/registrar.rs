//! Maintainer CLI for the existing TSF profiles and per-user COM registration.

#[cfg(feature = "test-api")]
use crate::foreign;
use crate::foreign::{Call, Lease, registrar as native};
use crate::profiles::{PROFILES, Profile, SERVICE_ID};
#[cfg(feature = "test-api")]
use std::ffi::c_void;
use std::ffi::{OsStr, OsString};
use std::io::Write;
use std::os::windows::ffi::OsStrExt;
use windows::Win32::Foundation::{
    E_FAIL, E_UNEXPECTED, ERROR_FILE_NOT_FOUND, ERROR_INSUFFICIENT_BUFFER, ERROR_NOT_FOUND,
    ERROR_PATH_NOT_FOUND, ERROR_SUCCESS, GetLastError, S_FALSE, S_OK,
};
use windows::Win32::Storage::FileSystem::{
    GetFileAttributesW, GetFullPathNameW, INVALID_FILE_ATTRIBUTES,
};
use windows::Win32::System::Com::{COINIT_APARTMENTTHREADED, CoInitializeEx, CoUninitialize};
use windows::Win32::System::Registry::{
    HKEY, HKEY_CURRENT_USER, KEY_READ, KEY_WRITE, REG_OPTION_NON_VOLATILE, REG_SZ, RegCloseKey,
    RegCreateKeyExW, RegDeleteTreeW, RegOpenKeyExW, RegSetValueExW,
};
use windows::Win32::UI::Input::KeyboardAndMouse::{KLF_NOTELLSHELL, LoadKeyboardLayoutW};
use windows::Win32::UI::TextServices::{
    ITfCategoryMgr, ITfInputProcessorProfiles, TF_LANGUAGEPROFILE,
};
use windows_core::{BOOL, BSTR, HRESULT, Interface, PCWSTR};

enum Command {
    Status,
    Check,
    Register(OsString),
    Unregister,
    Enable,
    Disable,
}

fn command(args: &[OsString]) -> Option<Command> {
    match args {
        [name] if name == "status" => Some(Command::Status),
        [name] if name == "check" => Some(Command::Check),
        [name] if name == "unregister" => Some(Command::Unregister),
        [name] if name == "enable" => Some(Command::Enable),
        [name] if name == "disable" => Some(Command::Disable),
        [name, path] if name == "register" => Some(Command::Register(path.clone())),
        _ => None,
    }
}

fn wide(value: &OsStr) -> Vec<u16> {
    value.encode_wide().chain(Some(0)).collect()
}

fn label(profile: &Profile) -> &'static str {
    if profile.mode == 1 { "EN" } else { "RU" }
}

fn report(stage: &str, result: HRESULT, profile: &str) -> HRESULT {
    println!("{stage} [{profile}]: 0x{:08x}.", result.0 as u32);
    let _ = std::io::stdout().flush();
    result
}

fn returned(call: Call) -> HRESULT {
    if call.exception != 0 {
        E_UNEXPECTED
    } else {
        call.result
    }
}

fn exact(result: HRESULT) -> Result<(), HRESULT> {
    if result == S_OK {
        Ok(())
    } else if result.is_err() {
        Err(result)
    } else {
        Err(E_UNEXPECTED)
    }
}

fn win32_failure(result: HRESULT) -> HRESULT {
    if result.is_err() { result } else { E_FAIL }
}

fn acquisition<T: Interface>(
    stage: &str,
    (call, value): (Call, Result<Lease<T>, HRESULT>),
) -> Result<Lease<T>, HRESULT> {
    let _ = report(stage, call.result, "service");
    value
}
fn presence(manager: &Lease<ITfInputProcessorProfiles>, profile: &Profile) -> HRESULT {
    let (call, entries) = native::entries(manager, profile.language);
    let _ = report("EnumLanguageProfiles", call.result, label(profile));
    let entries = match entries {
        Ok(entries) => entries,
        Err(result) => return result,
    };
    loop {
        let mut entry = TF_LANGUAGEPROFILE::default();
        let mut fetched = 0;
        let result = returned(native::next(&entries, &mut entry, &mut fetched));
        if result == S_FALSE && fetched == 0 {
            return report("Profile presence", S_FALSE, label(profile));
        }
        if result != S_OK || fetched != 1 {
            return report(
                "IEnumTfLanguageProfiles::Next",
                if result.is_err() {
                    result
                } else {
                    E_UNEXPECTED
                },
                label(profile),
            );
        }
        if entry.clsid != SERVICE_ID
            || entry.langid != profile.language
            || entry.guidProfile != profile.id
        {
            continue;
        }
        let mut description = std::ptr::null();
        let call = native::description(manager, profile, &mut description);
        let result = report(
            "GetLanguageProfileDescription",
            returned(call),
            label(profile),
        );
        if call.exception != 0 {
            return result;
        }
        // SAFETY: a normally returned BSTR is caller-owned, including on a failed HRESULT.
        let description = unsafe { BSTR::from_raw(description) };
        if result.is_err() {
            return result;
        }
        if result != S_OK
            || !description
                .iter()
                .copied()
                .eq(profile.description.encode_utf16())
        {
            return report(
                "Profile description mismatch or unavailable",
                E_UNEXPECTED,
                label(profile),
            );
        }
        return report("Profile presence", S_OK, label(profile));
    }
}

fn preflight(manager: &Lease<ITfInputProcessorProfiles>) -> HRESULT {
    let mut result = S_OK;
    for profile in &PROFILES {
        let present = presence(manager, profile);
        if present != S_OK && result.is_ok() {
            result = if present == S_FALSE {
                ERROR_NOT_FOUND.to_hresult()
            } else {
                present
            };
        }
    }
    report("Both-profile presence preflight", result, "service")
}

fn enable_profiles(manager: &Lease<ITfInputProcessorProfiles>, enabled: bool) -> HRESULT {
    let result = preflight(manager);
    if result.is_err() {
        return result;
    }
    for profile in &PROFILES {
        let call = native::enable(manager, profile, enabled);
        let result = report("EnableLanguageProfile", returned(call), label(profile));
        if let Err(result) = exact(result) {
            return report(
                "EnableLanguageProfile unexpected result",
                result,
                label(profile),
            );
        }
    }
    let result = preflight(manager);
    if result.is_err() {
        return result;
    }
    for profile in &PROFILES {
        let mut actual = BOOL(0);
        let call = native::enabled(manager, profile, &mut actual);
        let result = report(
            "IsEnabledLanguageProfile readback",
            returned(call),
            label(profile),
        );
        if let Err(result) = exact(result) {
            return report(
                "Enablement readback unexpected result",
                result,
                label(profile),
            );
        }
        if actual.as_bool() != enabled {
            return report("Enablement readback mismatch", E_FAIL, label(profile));
        }
    }
    S_OK
}

fn registry_path() -> Vec<u16> {
    wide(OsStr::new(&format!(
        "Software\\Classes\\CLSID\\{{{SERVICE_ID:?}}}"
    )))
}

fn registry(dll: Option<&[u16]>) -> HRESULT {
    let mut path = registry_path();
    let Some(dll) = dll else {
        // SAFETY: path is a live NUL-terminated UTF-16 registry path under HKCU.
        let result = unsafe { RegDeleteTreeW(HKEY_CURRENT_USER, PCWSTR(path.as_ptr())) };
        return report(
            "RegDeleteTreeW HKCU COM",
            if result == ERROR_FILE_NOT_FOUND {
                S_OK
            } else {
                result.to_hresult()
            },
            "service",
        );
    };
    path.pop();
    path.extend("\\InprocServer32".encode_utf16().chain(Some(0)));
    let mut key = HKEY::default();
    // SAFETY: path is NUL-terminated and key is a distinct writable handle slot.
    let result = unsafe {
        RegCreateKeyExW(
            HKEY_CURRENT_USER,
            PCWSTR(path.as_ptr()),
            None,
            PCWSTR::null(),
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            None,
            &mut key,
            None,
        )
    };
    let _ = report("RegCreateKeyExW HKCU COM", result.to_hresult(), "service");
    if result != ERROR_SUCCESS {
        return result.to_hresult();
    }
    let bytes: Vec<u8> = dll.iter().flat_map(|unit| unit.to_le_bytes()).collect();
    // SAFETY: key is open, and bytes includes the DLL path's terminating UTF-16 NUL.
    let mut result = unsafe { RegSetValueExW(key, PCWSTR::null(), None, REG_SZ, Some(&bytes)) };
    let _ = report(
        "RegSetValueExW HKCU COM server",
        result.to_hresult(),
        "service",
    );
    if result == ERROR_SUCCESS {
        let name = wide(OsStr::new("ThreadingModel"));
        let apartment: Vec<u8> = "Apartment"
            .encode_utf16()
            .chain(Some(0))
            .flat_map(u16::to_le_bytes)
            .collect();
        // SAFETY: key is open and both the value name and REG_SZ data are terminated.
        result =
            unsafe { RegSetValueExW(key, PCWSTR(name.as_ptr()), None, REG_SZ, Some(&apartment)) };
        let _ = report(
            "RegSetValueExW HKCU COM ThreadingModel",
            result.to_hresult(),
            "service",
        );
    }
    // SAFETY: key is the one successfully acquired handle, closed exactly once.
    let closed = unsafe { RegCloseKey(key) };
    let _ = report("RegCloseKey HKCU COM", closed.to_hresult(), "service");
    if result == ERROR_SUCCESS {
        closed.to_hresult()
    } else {
        result.to_hresult()
    }
}

fn register(dll: Option<&[u16]>) -> HRESULT {
    let manager: Lease<ITfInputProcessorProfiles> = match acquisition(
        "CoCreateInstance InputProcessorProfiles",
        native::profiles(),
    ) {
        Ok(manager) => manager,
        Err(result) => return result,
    };
    let categories: Lease<ITfCategoryMgr> =
        match acquisition("CoCreateInstance CategoryMgr", native::categories()) {
            Ok(categories) => categories,
            Err(result) => return result,
        };
    let install = dll.is_some();
    if install {
        let result = registry(dll);
        if result.is_err() {
            return result;
        }
    }
    let service = report(
        if install { "Register" } else { "Unregister" },
        returned(native::service(&manager, install)),
        "service",
    );
    if install && service.is_err() {
        return service;
    }
    if let Some(dll) = dll {
        for profile in &PROFILES {
            let call = native::add(&manager, profile, dll);
            let result = report("AddLanguageProfile", returned(call), label(profile));
            if result.is_err() {
                return result;
            }
            let layout = wide(OsStr::new(profile.layout));
            // SAFETY: layout is a live NUL-terminated KLID and KLF_NOTELLSHELL preserves source selection.
            let layout = unsafe { LoadKeyboardLayoutW(PCWSTR(layout.as_ptr()), KLF_NOTELLSHELL) };
            let layout = match layout {
                Ok(layout) => {
                    let _ = report("LoadKeyboardLayoutW", S_OK, label(profile));
                    layout
                }
                Err(error) => {
                    return report(
                        "LoadKeyboardLayoutW",
                        win32_failure(error.code()),
                        label(profile),
                    );
                }
            };
            let result = report(
                "SubstituteKeyboardLayout",
                returned(native::substitute(&manager, profile, layout)),
                label(profile),
            );
            if result.is_err() {
                return result;
            }
        }
    }
    let category = report(
        if install {
            "RegisterCategory"
        } else {
            "UnregisterCategory"
        },
        returned(native::category(&categories, install)),
        "service",
    );
    if install {
        return match exact(category) {
            Ok(()) => preflight(&manager),
            Err(result) => result,
        };
    }
    let registry = registry(None);
    if service.is_err() {
        service
    } else if category.is_err() {
        category
    } else {
        registry
    }
}

fn status() -> HRESULT {
    let manager: Lease<ITfInputProcessorProfiles> = match acquisition(
        "CoCreateInstance InputProcessorProfiles status",
        native::profiles(),
    ) {
        Ok(manager) => manager,
        Err(result) => return result,
    };
    let mut path = registry_path();
    path.pop();
    path.extend("\\InprocServer32".encode_utf16().chain(Some(0)));
    let mut key = HKEY::default();
    // SAFETY: path is NUL-terminated and key is a distinct writable handle slot.
    let registry = unsafe {
        RegOpenKeyExW(
            HKEY_CURRENT_USER,
            PCWSTR(path.as_ptr()),
            None,
            KEY_READ,
            &mut key,
        )
    };
    if !key.is_invalid() {
        // SAFETY: RegOpenKeyExW returned this open key and it is closed once.
        let _ = unsafe { RegCloseKey(key) };
    }
    let absent = registry == ERROR_FILE_NOT_FOUND || registry == ERROR_PATH_NOT_FOUND;
    println!(
        "COM HKCU registration: {}.",
        if registry == ERROR_SUCCESS {
            "present"
        } else if absent {
            "absent"
        } else {
            "unavailable"
        }
    );
    let _ = report(
        "RegOpenKeyExW HKCU COM status",
        registry.to_hresult(),
        "service",
    );
    let mut result = if registry == ERROR_SUCCESS || absent {
        S_OK
    } else {
        registry.to_hresult()
    };
    for profile in &PROFILES {
        let registered = presence(&manager, profile);
        if registered.is_err() {
            result = registered;
        }
        let mut enabled = BOOL(0);
        let available = if registered == S_OK {
            report(
                "IsEnabledLanguageProfile status",
                returned(native::enabled(&manager, profile, &mut enabled)),
                label(profile),
            )
        } else {
            E_FAIL
        };
        if registered == S_OK
            && let Err(error) = exact(available)
        {
            result = error;
        }
        println!(
            "TSF {} profile: {}; enabled: {}.",
            label(profile),
            if registered == S_OK {
                "present"
            } else if registered == S_FALSE {
                "absent"
            } else {
                "unavailable"
            },
            if registered != S_OK {
                "not applicable"
            } else if available == S_OK {
                if enabled.as_bool() { "yes" } else { "no" }
            } else {
                "unavailable"
            }
        );
    }
    println!(
        "TSF profile registration scope has not been established. No elevation fallback or source selection."
    );
    result
}

fn enable(enabled: bool, check: bool) -> HRESULT {
    let manager = match acquisition(
        "CoCreateInstance InputProcessorProfiles preflight",
        native::profiles(),
    ) {
        Ok(manager) => manager,
        Err(result) => return result,
    };
    if check {
        preflight(&manager)
    } else {
        enable_profiles(&manager, enabled)
    }
}

fn resolve_dll(path: &OsStr) -> Result<Vec<u16>, HRESULT> {
    let input = wide(path);
    let mut output = vec![0u16; 32768];
    // SAFETY: input is NUL-terminated and output is a writable 32768-unit buffer.
    let count = unsafe { GetFullPathNameW(PCWSTR(input.as_ptr()), Some(&mut output), None) };
    let result = if count == 0 {
        // SAFETY: GetLastError immediately follows the failed Win32 call.
        win32_failure(unsafe { GetLastError() }.to_hresult())
    } else if count as usize >= output.len() {
        ERROR_INSUFFICIENT_BUFFER.to_hresult()
    } else {
        S_OK
    };
    let _ = report("GetFullPathNameW DLL", result, "service");
    if result.is_err() {
        return Err(result);
    }
    output.truncate(count as usize + 1);
    // SAFETY: the resolved output is a live NUL-terminated path.
    let attributes = unsafe { GetFileAttributesW(PCWSTR(output.as_ptr())) };
    let result = if attributes == INVALID_FILE_ATTRIBUTES {
        // SAFETY: GetLastError immediately follows the failed Win32 call.
        win32_failure(unsafe { GetLastError() }.to_hresult())
    } else {
        S_OK
    };
    let _ = report("GetFileAttributesW DLL", result, "service");
    if result.is_err() {
        Err(result)
    } else {
        Ok(output)
    }
}

/// Runs the maintainer registration CLI and returns its process exit code.
///
/// `status` and `check` are read-only. Installation and enablement require an explicit command.
pub fn run() -> i32 {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    let Some(command) = command(&args) else {
        eprintln!(
            "Maintainer only. Usage: KeyrillRegister status | check | register ABSOLUTE_DLL_PATH | enable | disable | unregister"
        );
        return 2;
    };
    let dll = if let Command::Register(path) = &command {
        match resolve_dll(path) {
            Ok(path) => Some(path),
            Err(_) => return 2,
        }
    } else {
        None
    };
    // SAFETY: the CLI initializes its current thread once and balances each successful result.
    let initialized = unsafe { CoInitializeEx(None, COINIT_APARTMENTTHREADED) };
    let _ = report("CoInitializeEx", initialized, "service");
    if initialized.is_err() {
        return 1;
    }
    let result = match command {
        Command::Status => status(),
        Command::Check => enable(false, true),
        Command::Enable => enable(true, false),
        Command::Disable => enable(false, false),
        Command::Register(_) | Command::Unregister => {
            let _ = status();
            register(dll.as_deref())
        }
    };
    // SAFETY: all operation-owned COM leases drained before balancing successful initialization.
    unsafe { CoUninitialize() };
    if result.is_err() {
        eprintln!(
            "Operation failed (0x{:08x}). Inspect the stage above and run status before any further change.",
            result.0 as u32
        );
        return 1;
    }
    match command {
        Command::Status => {}
        Command::Check => println!(
            "Both proof profiles are present. Read-only check; no enablement or source selection."
        ),
        Command::Register(_) => println!(
            "Registered proof profiles through TSF and COM through HKCU. Enablement and source selection are separate."
        ),
        Command::Unregister => println!("Removed proof profiles and HKCU COM registration."),
        Command::Enable => println!(
            "Both proof profiles are present and enablement readback matches enabled. Source selection is manual."
        ),
        Command::Disable => {
            println!("Both proof profiles are present and enablement readback matches disabled.")
        }
    }
    0
}

#[cfg(feature = "test-api")]
#[unsafe(no_mangle)]
unsafe extern "system" fn KeyrillTestRegistration(manager: *mut c_void, operation: u32) -> HRESULT {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if operation > 2 {
            return windows::Win32::Foundation::E_INVALIDARG;
        }
        // SAFETY: the SDK test caller retains a live ITfInputProcessorProfiles for this call.
        let manager = match unsafe { foreign::retain::<ITfInputProcessorProfiles>(manager) } {
            Ok(manager) => manager,
            Err(error) => return error.result,
        };
        match operation {
            0 => preflight(&manager),
            1 => enable_profiles(&manager, true),
            _ => enable_profiles(&manager, false),
        }
    }))
    .unwrap_or(E_UNEXPECTED)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn commands_require_the_existing_exact_arguments() {
        for name in ["status", "check", "enable", "disable", "unregister"] {
            assert!(command(&[OsString::from(name)]).is_some());
            assert!(command(&[OsString::from(name), OsString::from("extra")]).is_none());
        }
        assert!(command(&[]).is_none());
        assert!(command(&[OsString::from("register")]).is_none());
        assert!(command(&[OsString::from("STATUS")]).is_none());
        let path = OsString::from("KeyrillTsf.dll");
        let Some(Command::Register(actual)) = command(&[OsString::from("register"), path.clone()])
        else {
            panic!("register must preserve its DLL path");
        };
        assert_eq!(actual, path);
    }

    #[test]
    fn registration_uses_the_existing_per_user_service_key() {
        let expected = wide(OsStr::new(
            "Software\\Classes\\CLSID\\{1A746212-84DB-4ACF-88B3-CAAA26352774}",
        ));
        assert_eq!(registry_path(), expected);
    }

    #[test]
    fn failed_win32_call_cannot_report_success_without_last_error() {
        assert_eq!(win32_failure(S_OK), E_FAIL);
        assert_eq!(win32_failure(S_FALSE), E_FAIL);
        assert_eq!(win32_failure(E_UNEXPECTED), E_UNEXPECTED);
    }
}
