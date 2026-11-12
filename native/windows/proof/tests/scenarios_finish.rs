use crate::support::{
    client::Client,
    dll::Event,
    document::{Effect, Span},
    oracle,
};
use std::rc::Rc;
use windows::Win32::{Foundation::*, UI::TextServices::*};
fn corrected() -> Client {
    let c = Client::new();
    c.type_text("ghbdtn ");
    oracle::document(&c, "привет ", 14);
    c
}
fn stopped(c: &Client) -> bool {
    let mut value = 0;
    unsafe {
        assert_eq!((c.api.stopped)(c.handle, &mut value), S_OK);
    }
    value != 0
}
fn selection(start: i32, length: i32, ase: TfActiveSelEnd) -> (Span, TF_SELECTIONSTYLE) {
    (
        Span { start, length },
        TF_SELECTIONSTYLE {
            ase,
            fInterimChar: false.into(),
        },
    )
}
#[test]
fn finish_unchanged_range_preserves_moved_or_selected_caret() {
    for span in [
        Span {
            start: 12,
            length: 0,
        },
        Span {
            start: 11,
            length: 0,
        },
        Span {
            start: 0,
            length: 18,
        },
    ] {
        let c = Client::new();
        c.type_text("hello");
        c.a.selection.set(span);
        let writes = c.a.writes.get();
        let ends = c.a.ends.get();
        assert!(!c.event(Event::command(6)));
        oracle::selection(&c, "hello", span, c.a.style.get());
        assert_eq!(c.a.writes.get(), writes + 1);
        assert_eq!(c.a.ends.get(), ends + 1);
        assert!(c.a.active.borrow().is_none());
        assert!(!stopped(&c));
    }
}
#[test]
fn finish_native_effects_restore_exact_selection_and_style() {
    for boundary in [None, Some("SetText.after"), Some("EndComposition.after")] {
        for (span, style) in [
            selection(14, 0, TF_AE_NONE),
            selection(11, 0, TF_AE_NONE),
            selection(3, 0, TF_AE_NONE),
            selection(9, 3, TF_AE_START),
            selection(6, 10, TF_AE_END),
            selection(0, 20, TF_AE_START),
        ] {
            let c = corrected();
            c.a.selection.set(span);
            c.a.style.set(style);
            let writes = c.a.writes.get();
            let ends = c.a.ends.get();
            let starts = c.a.starts.get();
            if let Some(boundary) = boundary {
                let d = Rc::clone(&c.a);
                c.host.on(boundary, 0, move || {
                    d.selection.set(Span {
                        start: 7,
                        length: 0,
                    });
                    d.style.set(TF_SELECTIONSTYLE {
                        ase: TF_AE_NONE,
                        fInterimChar: false.into(),
                    });
                });
            }
            assert!(!c.event(Event::command(6)));
            oracle::selection(&c, "привет ", span, style);
            assert_eq!(c.a.writes.get(), writes + 1);
            assert_eq!(c.a.ends.get(), ends + 1);
            assert_eq!(c.a.starts.get(), starts);
            assert!(c.a.active.borrow().is_none());
            assert!(!stopped(&c));
        }
    }
}
#[test]
fn finish_continuation_replans_with_one_write_per_composition() {
    for boundary in [None, Some("SetText.after"), Some("EndComposition.after")] {
        let c = corrected();
        let writes = c.a.writes.get();
        let ends = c.a.ends.get();
        let starts = c.a.starts.get();
        if let Some(boundary) = boundary {
            let d = Rc::clone(&c.a);
            c.host.on(boundary, 0, move || {
                d.selection.set(Span {
                    start: 7,
                    length: 0,
                })
            });
        }
        assert!(c.key(b'x' as u16, false));
        oracle::document(&c, "привет x", 15);
        assert_eq!(c.a.writes.get(), writes + 2);
        assert_eq!(c.a.ends.get(), ends + 1);
        assert_eq!(c.a.starts.get(), starts + 1);
        let ops: Vec<_> =
            c.a.operations
                .borrow()
                .iter()
                .filter(|o| o.request == c.a.request.get() && o.effect != Effect::Selection)
                .cloned()
                .collect();
        assert_eq!(
            ops.iter().map(|o| o.effect).collect::<Vec<_>>(),
            [Effect::Text, Effect::End, Effect::Start, Effect::Text]
        );
        assert_eq!(ops[0].composition, starts);
        assert_eq!(
            ops[0].span,
            Span {
                start: 7,
                length: 7
            }
        );
        assert_eq!(
            ops[0].replacement,
            "привет ".encode_utf16().collect::<Vec<_>>()
        );
        assert_eq!(ops[2].composition, starts + 1);
        assert_eq!(
            ops[2].span,
            Span {
                start: 14,
                length: 0
            }
        );
        assert_eq!(ops[3].replacement, vec![b'x' as u16]);
        assert!(!stopped(&c));
    }
}
#[test]
fn finish_clone_gravity_never_reads_unowned_selection() {
    for gravity in [false, true] {
        for span in [
            Span {
                start: 14,
                length: 0,
            },
            Span {
                start: 3,
                length: 0,
            },
            Span {
                start: 9,
                length: 3,
            },
            Span {
                start: 6,
                length: 10,
            },
            Span {
                start: 0,
                length: 20,
            },
        ] {
            for ase in [TF_AE_NONE, TF_AE_START, TF_AE_END] {
                let c = corrected();
                let style = TF_SELECTIONSTYLE {
                    ase,
                    fInterimChar: false.into(),
                };
                c.a.selection.set(span);
                c.a.style.set(style);
                c.a.move_clones.set(gravity);
                let reads = c.a.text_reads.borrow().len();
                let selections =
                    c.a.operations
                        .borrow()
                        .iter()
                        .filter(|o| o.effect == Effect::Selection)
                        .count();
                for boundary in ["SetText.after", "EndComposition.after"] {
                    let d = Rc::clone(&c.a);
                    c.host.on(boundary, 0, move || {
                        d.selection.set(Span {
                            start: 7,
                            length: 0,
                        });
                        d.style.set(TF_SELECTIONSTYLE {
                            ase: TF_AE_NONE,
                            fInterimChar: false.into(),
                        });
                    });
                }
                assert!(!c.event(Event::command(6)));
                oracle::selection(&c, "привет ", span, style);
                assert!(!stopped(&c));
                assert_eq!(
                    c.a.operations
                        .borrow()
                        .iter()
                        .filter(|o| o.effect == Effect::Selection)
                        .count(),
                    selections + 1
                );
                assert!(c.a.text_reads.borrow()[reads..].iter().all(|r| *r
                    == Span {
                        start: 7,
                        length: 7
                    }));
            }
        }
    }
}
#[test]
fn finish_matching_selection_requires_no_selection_effect() {
    let c = Client::new();
    c.type_text("hello");
    let before =
        c.a.operations
            .borrow()
            .iter()
            .filter(|o| o.effect == Effect::Selection)
            .count();
    assert!(!c.key(b'!' as u16, false));
    oracle::document(&c, "hello", 12);
    assert_eq!(
        c.a.operations
            .borrow()
            .iter()
            .filter(|o| o.effect == Effect::Selection)
            .count(),
        before
    );
}
#[test]
fn finish_post_end_exclusion_stops_reads_and_continuation() {
    for readonly in [false, true] {
        let c = corrected();
        let d = Rc::clone(&c.a);
        let reads = Rc::new(std::cell::Cell::new(0));
        let record = Rc::clone(&reads);
        c.host.on("EndComposition.after", 0, move || {
            record.set(d.reads.get());
            if readonly {
                d.status.set(TF_SD_READONLY);
            } else {
                d.results
                    .borrow_mut()
                    .insert("scope.GetValue", E_ACCESSDENIED);
            }
        });
        let ends = c.a.ends.get();
        assert!(!c.key(b'x' as u16, false));
        oracle::document(&c, "привет ", 14);
        assert_eq!(c.a.reads.get(), reads.get());
        assert_eq!(c.a.ends.get(), ends + 1);
        assert_eq!(c.a.starts.get(), 1);
        assert!(stopped(&c));
        let writes = c.a.writes.get();
        assert!(!c.key(b'y' as u16, false));
        assert_eq!(c.a.reads.get(), reads.get());
        assert_eq!(c.a.writes.get(), writes);
        oracle::document(&c, "привет ", 14);
    }
}
#[test]
fn finish_selection_failures_stop_without_replan_or_retry() {
    use windows_core::Interface;
    for case in 0..31 {
        let c = corrected();
        c.a.selection.set(Span {
            start: 6,
            length: 10,
        });
        c.a.style.set(TF_SELECTIONSTYLE {
            ase: TF_AE_START,
            fInterimChar: false.into(),
        });
        c.a.move_clones.set(true);
        let before = crate::support::dll::snapshot(&c.service);
        let writes = c.a.writes.get();
        let ends = c.a.ends.get();
        let starts = c.a.starts.get();
        let reads = c.a.text_reads.borrow().len();
        let selections =
            c.a.operations
                .borrow()
                .iter()
                .filter(|o| o.effect == Effect::Selection)
                .count();
        let attempted = !(case < 6 || (20..=25).contains(&case));
        if attempted {
            let d = Rc::clone(&c.a);
            c.host.on("EndComposition.after", 0, move || {
                d.selection.set(Span {
                    start: 7,
                    length: 0,
                });
                d.style.set(TF_SELECTIONSTYLE {
                    ase: TF_AE_NONE,
                    fInterimChar: false.into(),
                });
            });
        }
        let boundary = match case {
            0..=5 => "Clone.after",
            6 | 8 | 10 => "ShiftEnd",
            7 | 9 | 11 => "ShiftStart",
            20..=25 => "GetSelection",
            26 => "ShiftEnd.after",
            27 => "ShiftStart.after",
            _ => "SetSelection.after",
        };
        let d = Rc::clone(&c.a);
        let other = c.context_b.as_raw() as usize;
        let sink = c.service.cast::<ITfThreadMgrEventSink>().unwrap();
        let composition_span = Rc::clone(&d.active.borrow().as_ref().unwrap().span);
        let excluded = Rc::new(std::cell::Cell::new(None));
        let excluded_in = Rc::clone(&excluded);
        c.host.on(
            boundary,
            usize::from((20..=25).contains(&case)),
            move || match case {
                0 => {
                    let mut value = d.selection.get();
                    value.start += 1;
                    d.selection.set(value);
                }
                1 => {
                    let mut value = d.selection.get();
                    value.length -= 1;
                    d.selection.set(value);
                }
                2 | 13 => {
                    let mut value = d.style.get();
                    value.ase = TF_AE_END;
                    d.style.set(value);
                }
                3 => {
                    let last = d.clones.borrow().last().unwrap().upgrade().unwrap();
                    let mut value = last.get();
                    value.start = 5;
                    last.set(value);
                }
                4 | 19 => {
                    unsafe {
                        assert_eq!(
                            (sink.vtable().OnPushContext)(sink.as_raw(), other as *mut _),
                            S_OK
                        );
                    }
                    excluded_in.set(Some(d.reads.get()));
                }
                5 => d.range_context.set(other as *mut _),
                6 => d.shift_end_limit.set(1),
                7 => d.shift_start_limit.set(1),
                8 | 10 => {
                    d.results
                        .borrow_mut()
                        .insert("ShiftEnd", if case == 8 { E_FAIL } else { S_FALSE });
                }
                9 | 11 => {
                    d.results
                        .borrow_mut()
                        .insert("ShiftStart", if case == 9 { E_FAIL } else { S_FALSE });
                }
                12 | 28 => {
                    d.results
                        .borrow_mut()
                        .insert("SetSelection", if case == 12 { E_FAIL } else { S_FALSE });
                }
                14 | 24 => {
                    let mut value = d.style.get();
                    value.fInterimChar = true.into();
                    d.style.set(value);
                }
                15 => {
                    let mut value = d.selection.get();
                    value.start += 1;
                    d.selection.set(value);
                }
                16 | 26 | 27 => d.foreign.set(true),
                17 => {
                    d.status.set(TF_SD_READONLY);
                    excluded_in.set(Some(d.reads.get()));
                }
                18 => {
                    d.scope.set(IS_PRIVATE);
                    excluded_in.set(Some(d.reads.get()));
                }
                20 | 21 => {
                    d.results
                        .borrow_mut()
                        .insert("GetSelection", if case == 20 { S_FALSE } else { E_FAIL });
                }
                22 => d.selection_fetched.set(2),
                23 => d.nulls.borrow_mut().push("GetSelection"),
                25 => {
                    let mut value = d.style.get();
                    value.ase = TfActiveSelEnd(99);
                    d.style.set(value);
                }
                29 => {
                    let mut value = composition_span.get();
                    value.length += 1;
                    composition_span.set(value);
                }
                30 => d.text.borrow_mut()[7] = b'!' as u16,
                _ => unreachable!(),
            },
        );
        assert!(!c.key(b'x' as u16, false), "case{case}");
        assert!(stopped(&c), "case{case}");
        assert_eq!(
            c.a.writes.get(),
            writes + u32::from(attempted),
            "case{case}"
        );
        assert_eq!(c.a.ends.get(), ends + u32::from(attempted), "case{case}");
        assert_eq!(c.a.starts.get(), starts);
        assert_eq!(
            c.a.operations
                .borrow()
                .iter()
                .filter(|o| o.effect == Effect::Selection)
                .count(),
            selections + usize::from((12..20).contains(&case) || case >= 28),
            "case{case}"
        );
        let after = crate::support::dll::snapshot(&c.service);
        assert_eq!(after.counts[11], before.counts[11] + u32::from(attempted));
        assert_eq!(after.counts[10], before.counts[10] + u32::from(!attempted));
        if let Some(reads) = excluded.get() {
            assert_eq!(c.a.reads.get(), reads);
        }
        assert_eq!(
            *c.a.text.borrow(),
            format!(
                "prefix {}suffix",
                if case == 30 {
                    "!ривет "
                } else {
                    "привет "
                }
            )
            .encode_utf16()
            .collect::<Vec<_>>()
        );
        assert_eq!(
            *c.b.text.borrow(),
            "prefix suffix".encode_utf16().collect::<Vec<_>>()
        );
        assert!(c.a.text_reads.borrow()[reads..].iter().all(|r| *r
            == Span {
                start: 7,
                length: 7
            }));
        let final_reads = c.a.reads.get();
        assert!(!c.key(b'y' as u16, false));
        assert_eq!(c.a.reads.get(), final_reads);
        assert_eq!(c.a.writes.get(), writes + u32::from(attempted));
        c.host.drained();
    }
}
