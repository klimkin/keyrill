use super::{profile_path, registry, tip};
use crate::Result;
use keyrill_windows::profiles::{PROFILES, SERVICE_ID};
use serde_json::{Value, json};
use std::path::Path;
use windows::Win32::System::Com::*;
use windows::Win32::System::Registry::*;
use windows::Win32::UI::Input::KeyboardAndMouse::{GetKeyboardLayoutList, HKL};
use windows::Win32::UI::TextServices::*;

pub(super) struct Com;
impl Com {
    pub(super) fn new() -> Result<Self> {
        // SAFETY: setup initializes COM on its sole owner thread and balances success in Drop.
        unsafe { CoInitializeEx(None, COINIT_APARTMENTTHREADED) }.ok()?;
        Ok(Self)
    }
}
impl Drop for Com {
    fn drop(&mut self) {
        // SAFETY: the single successful initialization belongs to this thread and owner.
        unsafe {
            CoUninitialize();
        }
    }
}
fn profiles() -> Result<ITfInputProcessorProfiles> {
    // SAFETY: COM is initialized and the requested SDK interface owns its returned reference.
    Ok(unsafe { CoCreateInstance(&CLSID_TF_InputProcessorProfiles, None, CLSCTX_INPROC_SERVER) }?)
}
fn categories() -> Result<ITfCategoryMgr> {
    // SAFETY: COM is initialized and the returned interface is retained by its Rust owner.
    Ok(unsafe { CoCreateInstance(&CLSID_TF_CategoryMgr, None, CLSCTX_INPROC_SERVER) }?)
}
fn both(mut inspect: impl FnMut(REG_SAM_FLAGS) -> Result<Value>) -> Result<Value> {
    let first = inspect(KEY_WOW64_64KEY)?;
    if inspect(KEY_WOW64_32KEY)? != first {
        return Err("machine TSF views differ; scope or partial effect requires inspection".into());
    }
    Ok(first)
}
pub(super) fn service_state() -> Result<Value> {
    both(|view| Ok(json!(registry::exists(HKEY_LOCAL_MACHINE, view, &tip())?)))
}
fn service_metadata_at(hive: HKEY, view: REG_SAM_FLAGS, path: &str) -> Result<Value> {
    only(registry::names(hive, view, path)?, &[])?;
    Ok(Value::Null)
}
pub(super) fn service_write(value: &Value) -> Result<()> {
    let enabled = value.as_bool().ok_or("invalid service desired state")?;
    scope_preflight()?;
    let manager = profiles()?;
    // SAFETY: only Keyrill's stable service is changed; preflight excludes foreign descendants.
    unsafe {
        if enabled {
            manager.Register(&SERVICE_ID)?;
        } else {
            manager.Unregister(&SERVICE_ID)?;
        }
    }
    Ok(())
}
pub(super) fn profile_state(index: usize) -> Result<Value> {
    both(|view| {
        let path = profile_path(index);
        let description = registry::read(HKEY_LOCAL_MACHINE, view, &path, "Description")?;
        let icon = registry::read(HKEY_LOCAL_MACHINE, view, &path, "IconFile")?;
        let icon_index = registry::read(HKEY_LOCAL_MACHINE, view, &path, "IconIndex")?;
        if description.is_null() && icon.is_null() && icon_index.is_null() {
            return Ok(Value::Null);
        }
        if icon_index != registry::dword(0) {
            return Err("profile icon index differs from owned resource".into());
        }
        Ok(
            json!({"description":registry::decode_string(&description)?,"icon":registry::decode_string(&icon)?,"index":0}),
        )
    })
}
pub(super) fn profile_desired(index: usize, icon: &Path) -> Result<Value> {
    Ok(
        json!({"description":PROFILES[index].description,"icon":icon.to_str().ok_or("non-Unicode icon path")?,"index":0}),
    )
}
pub(super) fn profile_write(index: usize, value: &Value) -> Result<()> {
    let manager = profiles()?;
    profile_write_with(&manager, index, value)
}
fn profile_write_with(
    manager: &ITfInputProcessorProfiles,
    index: usize,
    value: &Value,
) -> Result<()> {
    let profile = &PROFILES[index];
    if value.is_null() {
        // SAFETY: only this immutable service/language/profile tuple is removed.
        unsafe {
            manager.RemoveLanguageProfile(&SERVICE_ID, profile.language, &profile.id)?;
        }
    } else {
        let description: Vec<_> = value["description"]
            .as_str()
            .ok_or("profile description missing")?
            .encode_utf16()
            .collect();
        let icon: Vec<_> = value["icon"]
            .as_str()
            .ok_or("profile icon missing")?
            .encode_utf16()
            .collect();
        if value["index"] != 0 {
            return Err("profile icon index invalid".into());
        }
        // SAFETY: GUIDs and bounded slices remain live; this call neither enables nor activates.
        unsafe {
            manager.AddLanguageProfile(
                &SERVICE_ID,
                profile.language,
                &profile.id,
                &description,
                &icon,
                0,
            )?;
        }
    }
    Ok(())
}

#[cfg(test)]
#[path = "tsf_tests.rs"]
mod tests;
pub(super) fn layout_state(index: usize) -> Result<Value> {
    both(|view| {
        let value = registry::read(
            HKEY_LOCAL_MACHINE,
            view,
            &profile_path(index),
            "SubstituteLayout",
        )?;
        if value.is_null() {
            Ok(Value::Null)
        } else {
            Ok(json!(registry::decode_string(&value)?))
        }
    })
}
pub(super) fn layout_write(index: usize, value: &Value) -> Result<()> {
    layout_write_with(&profiles()?, index, value, loaded_layout(index)?)
}
fn layout_write_with(
    manager: &ITfInputProcessorProfiles,
    index: usize,
    value: &Value,
    hkl: HKL,
) -> Result<()> {
    let profile = &PROFILES[index];
    let expected = format!("0x{:04x}{:04x}", profile.language, profile.language);
    if value.as_str() != Some(&expected) {
        return Err("unsupported substitute layout desired state".into());
    }
    // SAFETY: the exact standard layout handle was returned by the native loaded-layout query.
    unsafe {
        manager.SubstituteKeyboardLayout(&SERVICE_ID, profile.language, &profile.id, hkl)?;
    }
    Ok(())
}
pub(super) fn loaded_layout(index: usize) -> Result<HKL> {
    let mut layouts = [HKL::default(); 256];
    // SAFETY: this bounded slice is writable native HKL storage. Querying selects no source.
    let count = unsafe { GetKeyboardLayoutList(Some(&mut layouts)) };
    if count <= 0 || count as usize >= layouts.len() {
        return Err("loaded keyboard layouts unavailable or excessive".into());
    }
    standard_layout(&layouts[..count as usize], PROFILES[index].language)
        .ok_or_else(||"required standard US or Russian keyboard layout is not loaded; add it in Windows Settings before installing Keyrill".into())
}

fn standard_layout(layouts: &[HKL], language: u16) -> Option<HKL> {
    let language = usize::from(language);
    let identity = (language << 16) | language;
    layouts
        .iter()
        .copied()
        .find(|layout| layout.0 as usize == identity)
}
pub(super) fn category_state() -> Result<Value> {
    both(|view| {
        let category = format!(
            "{}\\Category\\Category\\{{{GUID_TFCAT_TIP_KEYBOARD:?}}}\\{{{SERVICE_ID:?}}}",
            tip()
        );
        let item = format!(
            "{}\\Category\\Item\\{{{SERVICE_ID:?}}}\\{{{GUID_TFCAT_TIP_KEYBOARD:?}}}",
            tip()
        );
        let first = registry::exists(HKEY_LOCAL_MACHINE, view, &category)?;
        if registry::exists(HKEY_LOCAL_MACHINE, view, &item)? != first {
            return Err("partial category registration requires inspection".into());
        }
        Ok(json!(first))
    })
}
pub(super) fn category_write(value: &Value) -> Result<()> {
    let enabled = value.as_bool().ok_or("invalid category desired state")?;
    let manager = categories()?;
    // SAFETY: the exact immutable service/category tuple is retained throughout the call.
    unsafe {
        if enabled {
            manager.RegisterCategory(&SERVICE_ID, &GUID_TFCAT_TIP_KEYBOARD, &SERVICE_ID)?;
        } else {
            manager.UnregisterCategory(&SERVICE_ID, &GUID_TFCAT_TIP_KEYBOARD, &SERVICE_ID)?;
        }
    }
    Ok(())
}
fn only(actual: Vec<String>, expected: &[String]) -> Result<()> {
    if actual.iter().any(|name| {
        !expected
            .iter()
            .any(|value| value.eq_ignore_ascii_case(name))
    }) {
        return Err("unrecognized owned registration descendant; no mutation permitted".into());
    }
    Ok(())
}
pub(super) fn scope_preflight() -> Result<()> {
    for view in [KEY_WOW64_64KEY, KEY_WOW64_32KEY] {
        only(
            registry::children(HKEY_LOCAL_MACHINE, view, &tip())?,
            &["Category".into(), "LanguageProfile".into()],
        )?;
        service_metadata_at(HKEY_LOCAL_MACHINE, view, &tip())?;
        let languages = format!("{}\\LanguageProfile", tip());
        only(registry::names(HKEY_LOCAL_MACHINE, view, &languages)?, &[])?;
        only(registry::names(HKEY_CURRENT_USER, view, &tip())?, &[])?;
        only(
            registry::children(HKEY_CURRENT_USER, view, &tip())?,
            &["LanguageProfile".into()],
        )?;
        only(registry::names(HKEY_CURRENT_USER, view, &languages)?, &[])?;
        only(
            registry::children(HKEY_CURRENT_USER, view, &languages)?,
            &PROFILES.map(|p| format!("0x{:08x}", p.language)),
        )?;
        only(
            registry::children(HKEY_LOCAL_MACHINE, view, &languages)?,
            &PROFILES.map(|p| format!("0x{:08x}", p.language)),
        )?;
        for (index, p) in PROFILES.iter().enumerate() {
            let language_path = format!("{languages}\\0x{:08x}", p.language);
            for hive in [HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER] {
                only(registry::names(hive, view, &language_path)?, &[])?;
                only(
                    registry::children(hive, view, &language_path)?,
                    &[format!("{{{:?}}}", p.id)],
                )?;
            }
            only(
                registry::children(
                    HKEY_LOCAL_MACHINE,
                    view,
                    &format!("{languages}\\0x{:08x}", p.language),
                )?,
                &[format!("{{{:?}}}", p.id)],
            )?;
            let path = profile_path(index);
            only(
                registry::names(HKEY_LOCAL_MACHINE, view, &path)?,
                &["Description", "IconFile", "IconIndex", "SubstituteLayout"].map(str::to_owned),
            )?;
            only(registry::children(HKEY_LOCAL_MACHINE, view, &path)?, &[])?;
            only(registry::children(HKEY_CURRENT_USER, view, &path)?, &[])?;
            only(
                registry::names(HKEY_CURRENT_USER, view, &path)?,
                &["Enable".into()],
            )?;
        }
        let category = format!("{}\\Category", tip());
        only(registry::names(HKEY_LOCAL_MACHINE, view, &category)?, &[])?;
        only(
            registry::children(HKEY_LOCAL_MACHINE, view, &category)?,
            &["Category".into(), "Item".into()],
        )?;
        for (branch, first, second) in [
            ("Category", GUID_TFCAT_TIP_KEYBOARD, SERVICE_ID),
            ("Item", SERVICE_ID, GUID_TFCAT_TIP_KEYBOARD),
        ] {
            let path = format!("{category}\\{branch}");
            for exact in [
                &path,
                &format!("{path}\\{{{first:?}}}"),
                &format!("{path}\\{{{first:?}}}\\{{{second:?}}}"),
            ] {
                only(registry::names(HKEY_LOCAL_MACHINE, view, exact)?, &[])?;
            }
            only(
                registry::children(HKEY_LOCAL_MACHINE, view, &path)?,
                &[format!("{{{first:?}}}")],
            )?;
            only(
                registry::children(HKEY_LOCAL_MACHINE, view, &format!("{path}\\{{{first:?}}}"))?,
                &[format!("{{{second:?}}}")],
            )?;
            only(
                registry::children(
                    HKEY_LOCAL_MACHINE,
                    view,
                    &format!("{path}\\{{{first:?}}}\\{{{second:?}}}"),
                )?,
                &[],
            )?;
        }
    }
    service_state()?;
    category_state()?;
    for i in 0..2 {
        profile_state(i)?;
        layout_state(i)?;
    }
    Ok(())
}
pub(super) fn is_proof_resource(
    resource: &str,
    before: &Value,
    proof: Option<&Path>,
) -> Result<bool> {
    let Some(proof) = proof else {
        return Ok(false);
    };
    match resource {
        "service" | "category" => Ok(before == true),
        "layout/en" => Ok(before == "0x04090409"),
        "layout/ru" => Ok(before == "0x04190419"),
        "profile/en" | "profile/ru" => {
            let description = if resource == "profile/en" {
                "Keyrill proof EN"
            } else {
                "Keyrill proof RU"
            };
            let icon = before["icon"].as_str().ok_or("proof icon path absent")?;
            Ok(before["description"] == description
                && before["index"] == 0
                && Path::new(icon).canonicalize()? == proof)
        }
        _ => Ok(false),
    }
}
