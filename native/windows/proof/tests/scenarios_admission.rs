use crate::support::{client::Client, oracle};
use std::rc::Rc;
use windows::Win32::{Foundation::*, System::Variant::*, UI::TextServices::*};
use windows_core::Interface;
fn no_admission(c: &Client) {
    oracle::document(c, "", 7);
    oracle::no_effects(c);
    assert_eq!(c.a.reads.get(), 0);
    assert!(!c.host.trace.borrow().contains(&"range.ACP"));
}
#[test]
fn explicit_ordinary_application_scopes_admit_one_owned_mark() {
    for scope in [IS_DEFAULT, IS_TEXT] {
        let c = Client::new();
        c.a.scope.set(scope);
        c.type_text("ghbdtn ");
        oracle::document(&c, "привет ", 14);
        assert_eq!(c.a.starts.get(), 1);
        assert_eq!(c.a.writes.get(), 7);
    }
}
#[test]
fn uncertain_application_scope_refuses_before_acp_or_text() {
    for version in [None, Some([11, 2607, 14, 0]), Some([11, 2609, 11, 0])] {
        for case in 0..22 {
            let c = version.map_or_else(Client::new, Client::notepad_version);
            let d = &c.a;
            match case {
                0 => {
                    d.results.borrow_mut().insert("GetAppProperty", S_FALSE);
                    d.nulls.borrow_mut().push("GetAppProperty");
                }
                1 | 2 => {
                    d.results
                        .borrow_mut()
                        .insert("GetAppProperty", if case == 1 { S_FALSE } else { E_FAIL });
                }
                3 => d.nulls.borrow_mut().push("GetAppProperty"),
                4 => {
                    d.results.borrow_mut().insert("scope.GetValue", S_FALSE);
                    d.scope_type.set(VT_EMPTY);
                }
                5 => {
                    d.results.borrow_mut().insert("scope.GetValue", E_FAIL);
                }
                6 => d.scope_type.set(VT_EMPTY),
                7 => d.scope_value_present.set(false),
                8 => d.scope_type.set(VT_I4),
                9 | 21 => {
                    d.results
                        .borrow_mut()
                        .insert("GetInputScopes", if case == 9 { E_FAIL } else { S_FALSE });
                }
                10 => d.scope_count.set(0),
                11 => d.nulls.borrow_mut().push("GetInputScopes"),
                12 => d.scope.set(InputScope(-1)),
                13 => d.scope.set(IS_EMAIL_SMTPEMAILADDRESS),
                14 => {
                    d.results.borrow_mut().insert("scope.GetValue", S_FALSE);
                }
                15 => d.scope.set(IS_PRIVATE),
                16 => d.scope.set(IS_PASSWORD),
                17 => d.scope.set(InputScope(i32::MAX)),
                18 => {
                    d.scope.set(IS_TEXT);
                    d.scope_count.set(2);
                    d.scope_second.set(IS_PRIVATE);
                }
                19 => d.scope_count.set(2),
                20 => {
                    d.scope.set(IS_TEXT);
                    d.scope_count.set(2);
                }
                _ => unreachable!(),
            };
            assert!(!c.key(b'g' as u16, false), "version{version:?}/case{case}");
            no_admission(&c);
        }
    }
}
#[test]
fn notepad_exact_unavailable_scope_has_bounded_authority() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        for unavailable in [false, true] {
            let c = Client::notepad_version(version);
            if unavailable {
                c.a.scope_type.set(VT_EMPTY);
                c.a.results.borrow_mut().insert("scope.GetValue", E_FAIL);
            }
            c.type_text("ghbdtn ");
            oracle::document(&c, "привет ", 14);
            assert_eq!(c.a.starts.get(), 1);
            assert_eq!(c.a.writes.get(), 7);
            assert_eq!(
                c.host.trace.borrow().contains(&"GetInputScopes"),
                !unavailable
            );
            let trace = c.host.trace.borrow();
            assert!(
                trace.iter().position(|s| *s == "scope.GetValue").unwrap()
                    < trace.iter().position(|s| *s == "range.ACP").unwrap()
            );
            drop(trace);
            assert!(c.key(b'x' as u16, false));
            oracle::document(&c, "привет x", 15);
        }
    }
    let c = Client::new();
    c.a.scope_type.set(VT_EMPTY);
    c.a.results.borrow_mut().insert("scope.GetValue", E_FAIL);
    assert!(!c.key(b'g' as u16, false));
    no_admission(&c);
}
#[test]
fn notepad_field_fact_mutations_refuse_before_acp() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        for case in 0..44 {
            let c = Client::notepad_version(version);
            {
                let mut facts = c.host.field_facts.borrow_mut();
                let f = facts.as_mut().unwrap();
                match case {
                    0 => f.host.kind = 0,
                    1 => f.host.process += 1,
                    2 => f.host.thread += 1,
                    3 => f.process = 0,
                    4 => f.thread = 0,
                    5 => f.focus = HWND::default(),
                    6 => f.view = HWND::default(),
                    7 => f.foreground = HWND::default(),
                    8 => f.root = HWND::default(),
                    9 => f.field.style &= !0x40000000,
                    10 => f.field.style &= !4,
                    11 => f.field.style |= 0x20,
                    12 => f.field.style |= 0x800,
                    13 => f.field.thread += 1,
                    14 => f.parent.thread += 1,
                    15 => f.grandparent.handle = f.parent.handle,
                    16..=19 => f.host.version[case - 16] += 1,
                    _ => {
                        let node = (case - 20) / 8;
                        let field = match node {
                            0 => &mut f.field,
                            1 => &mut f.parent,
                            _ => &mut f.grandparent,
                        };
                        match (case - 20) % 8 {
                            0 => field.complete = 0,
                            1 => field.handle = HWND::default(),
                            2 => field.process += 1,
                            3 => field.thread = 0,
                            4 => field.control += 1,
                            5 => field.enabled = 0,
                            6 => field.style |= 0x08000000,
                            7 => field.class_name[0] = b'!' as u16,
                            _ => unreachable!(),
                        }
                    }
                }
            }
            assert!(!c.key(b'g' as u16, false), "case{case}");
            no_admission(&c);
        }
    }
}
#[test]
fn writable_policy_requires_exact_status_and_interfaces() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        for status in [S_FALSE, E_FAIL] {
            let c = Client::notepad_version(version);
            c.a.results.borrow_mut().insert("GetStatus", status);
            assert!(!c.key(b'g' as u16, false));
            assert!(!c.host.trace.borrow().contains(&"GetSelection"));
            no_admission(&c);
        }
        for thread in [false, true] {
            for case in 0..6 {
                let c = Client::notepad_version(version);
                let (results, nulls) = if thread {
                    (&c.manager.results, &c.manager.nulls)
                } else {
                    (&c.a.results, &c.a.nulls)
                };
                match case {
                    0 | 1 => {
                        results.borrow_mut().insert(
                            "compartment.QI",
                            if case == 0 { S_FALSE } else { E_NOINTERFACE },
                        );
                    }
                    2 => nulls.borrow_mut().push("compartment.QI"),
                    3 | 4 => {
                        results.borrow_mut().insert(
                            "GetCompartment",
                            if case == 3 { S_FALSE } else { E_ACCESSDENIED },
                        );
                    }
                    5 => nulls.borrow_mut().push("GetCompartment"),
                    _ => unreachable!(),
                };
                assert!(!c.key(b'g' as u16, false));
                assert!(!c.host.trace.borrow().contains(&"GetSelection"));
                no_admission(&c);
            }
        }
    }
}
#[test]
fn compartment_variant_signatures_distinguish_unset_from_malformed() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        let cases = [
            (S_OK, VT_EMPTY, true),
            (S_OK, VT_I4, true),
            (S_FALSE, VT_EMPTY, true),
            (S_FALSE, VT_I4, false),
            (S_OK, VT_BOOL, false),
            (S_OK, VT_UNKNOWN, false),
            (E_FAIL, VT_EMPTY, false),
            (E_ACCESSDENIED, VT_I4, false),
        ];
        for compartment in 0..3 {
            for (result, kind, allowed) in cases {
                let c = Client::notepad_version(version);
                if compartment == 2 {
                    c.manager.compartment_type.set(kind);
                    c.manager.compartment_result.set(result);
                } else {
                    let mut kinds = c.a.compartment_types.get();
                    kinds[compartment] = kind;
                    c.a.compartment_types.set(kinds);
                    let mut results = c.a.compartment_results.get();
                    results[compartment] = result;
                    c.a.compartment_results.set(results);
                }
                assert_eq!(
                    c.key(b'g' as u16, false),
                    allowed,
                    "compartment{compartment},kind{kind:?},result{result:?}"
                );
                if allowed {
                    oracle::document(&c, "g", 8);
                    assert_eq!(c.a.starts.get(), 1);
                    assert_eq!(c.a.writes.get(), 1);
                } else {
                    assert!(!c.host.trace.borrow().contains(&"GetSelection"));
                    no_admission(&c);
                }
            }
        }
    }
}
#[test]
fn notepad_field_drift_blocks_boundary_and_owned_text_reads() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        for owned in [false, true] {
            let boundaries: &[&str] = if owned {
                &["GetRange", "GetExtent", "enumerator.Next"]
            } else {
                &[
                    "range.ACP",
                    "GetExtent",
                    "enumerator.Next",
                    "Clone",
                    "ShiftStart",
                ]
            };
            for &boundary in boundaries {
                for readonly in [false, true] {
                    let c = Client::notepad_version(version);
                    if owned {
                        c.type_text("g");
                    } else {
                        c.a.scope_type.set(VT_EMPTY);
                        c.a.results.borrow_mut().insert("scope.GetValue", E_FAIL);
                    }
                    let reads = c.a.reads.get();
                    let writes = c.a.writes.get();
                    let host = Rc::clone(&c.host);
                    c.host.on(boundary, 0, move || {
                        host.field_facts.borrow_mut().as_mut().unwrap().field.style |=
                            if readonly { 0x800 } else { 0x20 };
                    });
                    assert!(!c.key(if owned { b'h' } else { b'g' } as u16, false));
                    assert_eq!(c.a.reads.get(), reads);
                    assert_eq!(c.a.writes.get(), writes);
                    oracle::document(&c, if owned { "g" } else { "" }, if owned { 8 } else { 7 });
                }
            }
        }
    }
}
#[test]
fn notepad_selection_and_scope_failures_are_metadata_only() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        for case in 0..11 {
            let c = Client::notepad_version(version);
            c.a.scope_type.set(VT_EMPTY);
            c.a.results.borrow_mut().insert("scope.GetValue", E_FAIL);
            match case {
                0 => {
                    c.a.results.borrow_mut().insert("GetStatus", E_FAIL);
                }
                1 | 2 => {
                    c.a.results
                        .borrow_mut()
                        .insert("GetSelection", if case == 1 { E_FAIL } else { S_FALSE });
                }
                3 => c.a.nulls.borrow_mut().push("GetSelection"),
                4 | 5 => c.a.selection_fetched.set(if case == 4 { 0 } else { 2 }),
                6 => {
                    let mut s = c.a.style.get();
                    s.fInterimChar = true.into();
                    c.a.style.set(s);
                }
                7 => {
                    let mut s = c.a.style.get();
                    s.ase = TfActiveSelEnd(99);
                    c.a.style.set(s);
                }
                8 => c.a.range_context.set(c.context_b.as_raw()),
                9 => {
                    c.a.scope_type.set(VT_UNKNOWN);
                    c.a.results.borrow_mut().remove("scope.GetValue");
                    c.a.results.borrow_mut().insert("scope.QI", S_FALSE);
                }
                10 => {
                    let sink = c.service.cast::<ITfThreadMgrEventSink>().unwrap();
                    let other = c.context_b.as_raw() as usize;
                    c.host.on("GetSelection", 0, move || unsafe {
                        assert_eq!(
                            (sink.vtable().OnPushContext)(sink.as_raw(), other as *mut _),
                            S_OK
                        );
                    });
                }
                _ => unreachable!(),
            };
            assert!(!c.key(b'g' as u16, false), "case{case}");
            no_admission(&c);
        }
    }
}
#[test]
fn notepad_native_policy_uncertainty_has_no_admission_effects() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        for case in 0..18 {
            let c = Client::notepad_version(version);
            match case {
                0 => c.a.status.set(TF_SD_READONLY),
                1 => c.a.disabled.set(true),
                2 => c.a.empty_disabled.set(true),
                3 => c.manager.disabled.set(true),
                4..=9 => {
                    let index = (case - 4) % 3;
                    let result = if case < 7 { E_ACCESSDENIED } else { E_FAIL };
                    if index == 2 {
                        c.manager.compartment_result.set(result);
                    } else {
                        let mut r = c.a.compartment_results.get();
                        r[index] = result;
                        c.a.compartment_results.set(r);
                    }
                }
                10..=15 => {
                    let index = (case - 10) % 3;
                    let kind = if case < 13 { VT_UNKNOWN } else { VT_BOOL };
                    if index == 2 {
                        c.manager.compartment_type.set(kind);
                    } else {
                        let mut kinds = c.a.compartment_types.get();
                        kinds[index] = kind;
                        c.a.compartment_types.set(kinds);
                    }
                }
                16 | 17 => {
                    c.a.scope_type.set(VT_EMPTY);
                    c.a.results.borrow_mut().insert(
                        "scope.GetValue",
                        if case == 16 { E_FAIL } else { E_ACCESSDENIED },
                    );
                    if case == 16 {
                        let host = Rc::clone(&c.host);
                        c.host.on("scope.GetValue", 0, move || {
                            host.field_facts.borrow_mut().as_mut().unwrap().focus = HWND::default()
                        });
                    }
                }
                _ => unreachable!(),
            };
            assert!(!c.key(b'g' as u16, false), "case{case}");
            no_admission(&c);
        }
    }
}
#[test]
fn exceptional_notepad_scope_requires_fresh_exact_failure() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        for case in 0..5 {
            let c = Client::notepad_version(version);
            c.a.scope_type.set(VT_EMPTY);
            c.a.results.borrow_mut().insert(
                "scope.GetValue",
                match case {
                    0 => S_OK,
                    1 => S_FALSE,
                    2 => E_ACCESSDENIED,
                    _ => E_FAIL,
                },
            );
            if case == 3 {
                c.a.results.borrow_mut().insert("GetAppProperty", S_FALSE);
            }
            if case == 4 {
                let sink = c.service.cast::<ITfThreadMgrEventSink>().unwrap();
                let other = c.context_b.as_raw() as usize;
                c.host.on("scope.GetValue", 0, move || unsafe {
                    assert_eq!(
                        (sink.vtable().OnPushContext)(sink.as_raw(), other as *mut _),
                        S_OK
                    );
                });
            }
            assert!(!c.key(b'g' as u16, false));
            no_admission(&c);
        }
    }
}
#[test]
fn notepad_executor_refusals_expire_retained_callbacks() {
    for version in [[11, 2607, 14, 0], [11, 2609, 11, 0]] {
        for case in 0..9 {
            let c = Client::notepad_version(version);
            c.a.retain_callback.set(true);
            c.a.scope_type.set(VT_EMPTY);
            c.a.results.borrow_mut().insert("scope.GetValue", E_FAIL);
            match case {
                0 => c.a.acp.set(false),
                1 => {
                    c.a.invoke.set(false);
                    c.a.results
                        .borrow_mut()
                        .insert("RequestEditSession", E_ACCESSDENIED);
                }
                2 | 8 => {
                    c.a.invoke.set(false);
                    if case == 8 {
                        c.a.results.borrow_mut().insert("session", TF_E_SYNCHRONOUS);
                    }
                }
                3 => c.a.foreign.set(true),
                4 => c.a.reject_composition.set(true),
                5 => {
                    let host = Rc::clone(&c.host);
                    c.host.on("capture", 2, move || {
                        host.field_facts
                            .borrow_mut()
                            .as_mut()
                            .unwrap()
                            .parent
                            .handle = HWND(3usize as *mut _)
                    });
                }
                6 | 7 => {
                    if case == 7 {
                        c.a.scope_type.set(VT_UNKNOWN);
                        c.a.results.borrow_mut().remove("scope.GetValue");
                    }
                    let sink = c.service.cast::<ITfThreadMgrEventSink>().unwrap();
                    let other = c.context_b.as_raw() as usize;
                    c.host.on(
                        if case == 6 {
                            "range.GetContext"
                        } else {
                            "GetInputScopes"
                        },
                        0,
                        move || unsafe {
                            assert_eq!(
                                (sink.vtable().OnPushContext)(sink.as_raw(), other as *mut _),
                                S_OK
                            );
                        },
                    );
                }
                _ => unreachable!(),
            };
            assert!(!c.key(b'g' as u16, false), "case{case}");
            oracle::document(&c, "", 7);
            assert_eq!(c.a.writes.get(), 0);
            assert!(c.a.active.borrow().is_none());
            if c.a.retained.borrow().is_some() {
                let trace = c.host.trace.borrow().clone();
                let starts = c.a.starts.get();
                assert_eq!(c.delayed(), E_FAIL);
                assert_eq!(*c.host.trace.borrow(), trace);
                assert_eq!(c.a.starts.get(), starts);
                assert_eq!(c.a.writes.get(), 0);
                oracle::document(&c, "", 7);
            }
        }
    }
}

#[test]
fn unknown_notepad_versions_refuse_executor_before_acp_and_pass_input() {
    for version in [
        [0; 4],
        [11, 2607, 13, 0],
        [11, 2607, 15, 0],
        [11, 2609, 10, 0],
        [11, 2609, 12, 0],
        [11, 2609, 11, 1],
    ] {
        for unavailable in [false, true] {
            let c = Client::notepad_version(version);
            if unavailable {
                c.a.scope_type.set(VT_EMPTY);
                c.a.results.borrow_mut().insert("scope.GetValue", E_FAIL);
            }
            assert!(
                !c.key(b'g' as u16, true),
                "version {version:?}, unavailable {unavailable}"
            );
            oracle::document(&c, "g", 8);
            oracle::no_effects(&c);
            assert_eq!(c.a.reads.get(), 0);
            assert!(c.a.text_reads.borrow().is_empty());
            assert!(!c.host.trace.borrow().contains(&"range.ACP"));
            assert!(c.a.active.borrow().is_none());
        }
    }
}
