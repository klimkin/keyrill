#![allow(non_snake_case)]
use super::*;
use crate::journal::{Backend, Change, Journal};
use std::cell::{Cell, RefCell};
use std::rc::Rc;
use windows::Win32::Foundation::E_FAIL;
use windows_core::{BOOL, BSTR, GUID, PCWSTR, implement};

#[test]
fn standard_layout_requires_the_complete_native_handle_identity() {
    let expected = HKL(0x04090409usize as *mut _);
    assert_eq!(standard_layout(&[expected], 0x0409), Some(expected));
    assert_eq!(standard_layout(&[expected], 0x0419), None);
    #[cfg(target_pointer_width = "64")]
    {
        let private = HKL(0xffff_ffff_0409_0409usize as *mut _);
        assert_eq!(standard_layout(&[private], 0x0409), None);
        assert_eq!(
            standard_layout(&[private, expected], 0x0409),
            Some(expected)
        );
    }
}

#[derive(Clone, Copy)]
enum Failure {
    None,
    Before,
    After,
    Partial,
}
struct State {
    value: RefCell<Value>,
    layout: RefCell<Value>,
    writes: Cell<usize>,
    layout_writes: Cell<usize>,
    failure: Cell<Failure>,
    layout_failure: Cell<Failure>,
}
#[implement(ITfInputProcessorProfiles)]
struct Provider(Rc<State>);
impl ITfInputProcessorProfiles_Impl for Provider_Impl {
    fn AddLanguageProfile(
        &self,
        class: *const GUID,
        language: u16,
        profile: *const GUID,
        description: &PCWSTR,
        description_len: u32,
        icon: &PCWSTR,
        icon_len: u32,
        index: u32,
    ) -> windows_core::Result<()> {
        // SAFETY: this provider is invoked only by the typed SDK caller with live GUID/buffer inputs.
        let (class, profile, description, icon) = unsafe {
            (
                *class,
                *profile,
                std::slice::from_raw_parts(description.0, description_len as usize),
                std::slice::from_raw_parts(icon.0, icon_len as usize),
            )
        };
        assert_eq!(class, SERVICE_ID);
        assert_eq!(language, PROFILES[0].language);
        assert_eq!(profile, PROFILES[0].id);
        assert_eq!(index, 0);
        if matches!(self.0.failure.get(), Failure::Before) {
            return Err(E_FAIL.into());
        }
        self.0.writes.set(self.0.writes.get() + 1);
        *self.0.value.borrow_mut() = if matches!(self.0.failure.get(), Failure::Partial) {
            json!({"description":"partial native effect"})
        } else {
            json!({"description":String::from_utf16(description).unwrap(),"icon":String::from_utf16(icon).unwrap(),"index":0})
        };
        if matches!(self.0.failure.get(), Failure::After | Failure::Partial) {
            Err(E_FAIL.into())
        } else {
            Ok(())
        }
    }
    fn RemoveLanguageProfile(
        &self,
        _: *const GUID,
        _: u16,
        _: *const GUID,
    ) -> windows_core::Result<()> {
        *self.0.value.borrow_mut() = Value::Null;
        self.0.writes.set(self.0.writes.get() + 1);
        Ok(())
    }
    fn Register(&self, _: *const GUID) -> windows_core::Result<()> {
        panic!("unexpected Register")
    }
    fn Unregister(&self, _: *const GUID) -> windows_core::Result<()> {
        panic!("unexpected Unregister")
    }
    fn EnumInputProcessorInfo(&self) -> windows_core::Result<IEnumGUID> {
        panic!("unexpected enumeration")
    }
    fn GetDefaultLanguageProfile(
        &self,
        _: u16,
        _: *const GUID,
        _: *mut GUID,
        _: *mut GUID,
    ) -> windows_core::Result<()> {
        panic!("unexpected default read")
    }
    fn SetDefaultLanguageProfile(
        &self,
        _: u16,
        _: *const GUID,
        _: *const GUID,
    ) -> windows_core::Result<()> {
        panic!("source selection forbidden")
    }
    fn ActivateLanguageProfile(
        &self,
        _: *const GUID,
        _: u16,
        _: *const GUID,
    ) -> windows_core::Result<()> {
        panic!("source activation forbidden")
    }
    fn GetActiveLanguageProfile(
        &self,
        _: *const GUID,
        _: *mut u16,
        _: *mut GUID,
    ) -> windows_core::Result<()> {
        panic!("unexpected active read")
    }
    fn GetLanguageProfileDescription(
        &self,
        _: *const GUID,
        _: u16,
        _: *const GUID,
    ) -> windows_core::Result<BSTR> {
        panic!("unexpected description query")
    }
    fn GetCurrentLanguage(&self) -> windows_core::Result<u16> {
        panic!("unexpected language read")
    }
    fn ChangeCurrentLanguage(&self, _: u16) -> windows_core::Result<()> {
        panic!("source change forbidden")
    }
    fn GetLanguageList(&self, _: *mut *mut u16, _: *mut u32) -> windows_core::Result<()> {
        panic!("unexpected language list")
    }
    fn EnumLanguageProfiles(&self, _: u16) -> windows_core::Result<IEnumTfLanguageProfiles> {
        panic!("unexpected profiles enumeration")
    }
    fn EnableLanguageProfile(
        &self,
        _: *const GUID,
        _: u16,
        _: *const GUID,
        _: BOOL,
    ) -> windows_core::Result<()> {
        panic!("source enablement forbidden")
    }
    fn IsEnabledLanguageProfile(
        &self,
        _: *const GUID,
        _: u16,
        _: *const GUID,
    ) -> windows_core::Result<BOOL> {
        panic!("unexpected enablement read")
    }
    fn EnableLanguageProfileByDefault(
        &self,
        _: *const GUID,
        _: u16,
        _: *const GUID,
        _: BOOL,
    ) -> windows_core::Result<()> {
        panic!("default enablement forbidden")
    }
    fn SubstituteKeyboardLayout(
        &self,
        class: *const GUID,
        language: u16,
        profile: *const GUID,
        layout: HKL,
    ) -> windows_core::Result<()> {
        // SAFETY: the typed caller retains its fixed service and profile GUIDs for this call.
        unsafe {
            assert_eq!(*class, SERVICE_ID);
            assert_eq!(*profile, PROFILES[0].id);
        }
        assert_eq!(language, PROFILES[0].language);
        assert_eq!(layout.0 as usize, 0x04090409);
        let failure = self.0.layout_failure.get();
        if matches!(failure, Failure::Before) {
            return Err(E_FAIL.into());
        }
        self.0.layout_writes.set(self.0.layout_writes.get() + 1);
        *self.0.layout.borrow_mut() = if matches!(failure, Failure::Partial) {
            json!("partial native layout")
        } else {
            json!("0x04090409")
        };
        if matches!(failure, Failure::After | Failure::Partial) {
            Err(E_FAIL.into())
        } else {
            Ok(())
        }
    }
}
struct Fixture {
    provider: ITfInputProcessorProfiles,
    state: Rc<State>,
    receipt: Option<Journal>,
    interrupt_after_profile: bool,
}
impl Backend for Fixture {
    fn read(&mut self, resource: &str) -> Result<Value> {
        match resource {
            "profile/en" => Ok(self.state.value.borrow().clone()),
            "layout/en" => Ok(self.state.layout.borrow().clone()),
            _ => panic!("unexpected resource"),
        }
    }
    fn write(&mut self, resource: &str, value: &Value) -> Result<()> {
        match resource {
            "profile/en" => profile_write_with(&self.provider, 0, value),
            "layout/en" => {
                layout_write_with(&self.provider, 0, value, HKL(0x04090409usize as *mut _))
            }
            _ => panic!("unexpected resource"),
        }
    }
    fn persist(&mut self, journal: &Journal) -> Result<()> {
        self.receipt = Some(journal.clone());
        if self.interrupt_after_profile && journal.completed == 1 {
            return Err("interrupted after profile checkpoint".into());
        }
        Ok(())
    }
}
#[test]
fn injected_native_profile_hresult_is_not_an_installation_acknowledgement() {
    for failure in [
        Failure::None,
        Failure::Before,
        Failure::After,
        Failure::Partial,
    ] {
        let state = Rc::new(State {
            value: RefCell::new(Value::Null),
            layout: RefCell::new(Value::Null),
            writes: Cell::new(0),
            layout_writes: Cell::new(0),
            failure: Cell::new(failure),
            layout_failure: Cell::new(Failure::None),
        });
        let provider: ITfInputProcessorProfiles = Provider(state.clone()).into();
        let mut backend = Fixture {
            provider,
            state: state.clone(),
            receipt: None,
            interrupt_after_profile: false,
        };
        let desired =
            json!({"description":PROFILES[0].description,"icon":"public-english.ico","index":0});
        let mut journal = Journal {
            desired: "a".repeat(64),
            previous: None,
            completed: 0,
            changes: vec![Change {
                resource: "profile/en".into(),
                before: Value::Null,
                after: desired.clone(),
            }],
        };
        let outcome = journal.reconcile(&mut backend);
        assert_eq!(
            outcome.is_ok(),
            matches!(failure, Failure::None | Failure::After)
        );
        state.failure.set(Failure::None);
        let mut recovered = backend.receipt.clone().unwrap();
        if matches!(failure, Failure::Partial) {
            assert!(recovered.reconcile(&mut backend).is_err());
            assert_eq!(state.writes.get(), 1);
            assert_eq!(recovered.completed, 0);
        } else {
            recovered.reconcile(&mut backend).unwrap();
            assert_eq!(*state.value.borrow(), desired);
            assert_eq!(state.writes.get(), 1);
            profile_write_with(&backend.provider, 0, &Value::Null).unwrap();
            assert!(state.value.borrow().is_null());
        }
    }
}

#[test]
fn profile_checkpoint_and_substitute_layout_failures_recover_without_replaying_profile() {
    for failure in [
        Failure::None,
        Failure::Before,
        Failure::After,
        Failure::Partial,
    ] {
        let state = Rc::new(State {
            value: RefCell::new(Value::Null),
            layout: RefCell::new(Value::Null),
            writes: Cell::new(0),
            layout_writes: Cell::new(0),
            failure: Cell::new(Failure::None),
            layout_failure: Cell::new(failure),
        });
        let provider: ITfInputProcessorProfiles = Provider(state.clone()).into();
        let mut backend = Fixture {
            provider,
            state: state.clone(),
            receipt: None,
            interrupt_after_profile: true,
        };
        let mut journal = Journal {
            desired: "a".repeat(64),
            previous: None,
            completed: 0,
            changes: vec![
                Change {
                    resource: "profile/en".into(),
                    before: Value::Null,
                    after: json!({"description":PROFILES[0].description,"icon":"public-english.ico","index":0}),
                },
                Change {
                    resource: "layout/en".into(),
                    before: Value::Null,
                    after: json!("0x04090409"),
                },
            ],
        };
        assert!(journal.reconcile(&mut backend).is_err());
        assert_eq!(state.writes.get(), 1);
        assert_eq!(state.layout_writes.get(), 0);
        backend.interrupt_after_profile = false;
        let mut recovered = backend.receipt.clone().unwrap();
        assert_eq!(recovered.completed, 1);
        assert_eq!(
            recovered.reconcile(&mut backend).is_ok(),
            matches!(failure, Failure::None | Failure::After)
        );
        state.layout_failure.set(Failure::None);
        let mut retried = backend.receipt.clone().unwrap();
        if matches!(failure, Failure::Partial) {
            assert!(retried.reconcile(&mut backend).is_err());
            assert_eq!(*state.layout.borrow(), json!("partial native layout"));
        } else {
            retried.reconcile(&mut backend).unwrap();
            assert_eq!(*state.layout.borrow(), json!("0x04090409"));
        }
        assert_eq!(state.writes.get(), 1);
        assert_eq!(state.layout_writes.get(), 1);
    }
}
