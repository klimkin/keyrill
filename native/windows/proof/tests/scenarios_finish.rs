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
