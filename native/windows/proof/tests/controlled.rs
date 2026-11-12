#![cfg(windows)]
mod scenarios_admission;
mod scenarios_boundary;
mod scenarios_editing;
mod scenarios_finish;
mod support;
use support::{client::Client, oracle};
use windows::Win32::{Foundation::*, UI::TextServices::*};
use windows_core::Interface;
#[test]
fn correction_preserves_complete_documents() {
    let c = Client::new();
    c.type_text("ghbdtn ");
    oracle::document(&c, "привет ", 14);
    c.type_text("x");
    oracle::document(&c, "привет x", 15);
    c.host.drained();
}
#[test]
fn denied_and_delayed_sessions_do_not_edit() {
    for delayed in [false, true] {
        let c = Client::new();
        c.a.invoke.set(false);
        c.a.results.borrow_mut().insert("session", TF_E_SYNCHRONOUS);
        assert!(!c.key(b'g' as u16, true));
        oracle::document(&c, "g", 8);
        oracle::no_effects(&c);
        if delayed {
            assert_eq!(c.delayed(), E_FAIL);
            oracle::document(&c, "g", 8);
            oracle::no_effects(&c);
        }
        c.host.drained();
    }
}
#[test]
fn strict_status_refuses_nonexact_success() {
    for status in [S_FALSE, E_ACCESSDENIED] {
        let c = Client::new();
        c.a.results.borrow_mut().insert("GetStatus", status);
        assert!(!c.key(b'g' as u16, true));
        oracle::document(&c, "g", 8);
        oracle::no_effects(&c);
        c.host.drained();
    }
}
#[test]
fn release_reentry_invalidates_pending_authority() {
    let c = Client::new();
    let service = c.service.as_raw() as usize;
    c.host.on("Release.after", 0, move || unsafe {
        let ptr = service as *mut std::ffi::c_void;
        let vt = *(ptr as *const *const ITfTextInputProcessorEx_Vtbl);
        assert_eq!(((*vt).base__.Deactivate)(ptr), S_OK);
    });
    assert!(!c.key(b'g' as u16, true));
    oracle::document(&c, "g", 8);
    oracle::no_effects(&c);
    c.host.drained();
}
