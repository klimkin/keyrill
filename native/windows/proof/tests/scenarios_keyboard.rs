use crate::support::{
    client::Client,
    dll::{Api, Event},
    oracle,
    providers::{PROFILE_EN, SERVICE_ID},
};
use windows::Win32::{
    Foundation::*,
    UI::{Input::KeyboardAndMouse::*, TextServices::*},
};
use windows_core::HRESULT;

type Translate =
    unsafe extern "system" fn(WPARAM, LPARAM, HKL, *const u8, *mut Event, *mut i32) -> HRESULT;
fn translated(
    call: Translate,
    key: u16,
    flags: isize,
    layout: HKL,
    state: &[u8; 256],
) -> (Event, i32) {
    let mut event = Event::default();
    let mut modifier = -1;
    unsafe {
        assert_eq!(
            call(
                WPARAM(key as usize),
                LPARAM(flags),
                layout,
                state.as_ptr(),
                &mut event,
                &mut modifier
            ),
            S_OK
        );
    }
    (event, modifier)
}
#[test]
fn keyboard_actual_layout_shift_caps_repeat_and_modifier_axes() {
    let api = Api::load();
    let call: Translate = unsafe { api.export(b"KeyrillTestTranslate\0") };
    let en = HKL(0x04090409usize as *mut _);
    let ru = HKL(0x04190419usize as *mut _);
    let mut state = [0; 256];
    for (shift, caps, expected) in [(0, 0, 'g'), (0x80, 0, 'G'), (0, 1, 'G')] {
        state[VK_SHIFT.0 as usize] = shift;
        state[VK_CAPITAL.0 as usize] = caps;
        let (event, modifier) = translated(call, b'G' as u16, 0x220001, en, &state);
        assert_eq!(
            (event.role, event.length, event.text[0], modifier),
            (1, 1, expected as u16, 0)
        );
    }
    state.fill(0);
    state[VK_CONTROL.0 as usize] = 0x80;
    assert_eq!(
        translated(call, b'G' as u16, 0x220001, en, &state).0.role,
        6
    );
    state.fill(0);
    let c = Client::new();
    c.type_text("gh");
    let event = translated(call, b'G' as u16, 0x220003, en, &state).0;
    assert_eq!(event.role, 6);
    assert!(!c.event(event));
    assert!(c.a.active.borrow().is_none());
    oracle::document(&c, "gh", 9);
    assert_eq!(
        translated(call, b'G' as u16, 0x220000, en, &state).0.role,
        6
    );
    for key in [VK_TAB, VK_RETURN, VK_F12, VK_DELETE] {
        assert_eq!(translated(call, key.0, 1, en, &state).0.role, 6);
    }
    for key in [
        VK_CONTROL,
        VK_LCONTROL,
        VK_RCONTROL,
        VK_MENU,
        VK_LMENU,
        VK_RMENU,
        VK_SHIFT,
        VK_LSHIFT,
        VK_RSHIFT,
        VK_LWIN,
        VK_RWIN,
    ] {
        assert_eq!(translated(call, key.0, 1, en, &state).1, 1);
    }
    let mut layouts = [HKL::default(); 64];
    let count = unsafe { GetKeyboardLayoutList(Some(&mut layouts)) };
    assert!((0..=64).contains(&count));
    let russian = layouts[..count as usize].contains(&ru);
    if russian {
        assert_eq!(
            translated(call, b'H' as u16, 0x230001, ru, &state).0.text[0],
            'р' as u16
        );
    } else {
        eprintln!("SKIP: RU layout translation; Russian layout is not already loaded");
    }
    let oem = [
        (VK_OEM_1, ";:", "жЖ"),
        (VK_OEM_PLUS, "=+", "=+"),
        (VK_OEM_COMMA, ",<", "бБ"),
        (VK_OEM_MINUS, "-_", "-_"),
        (VK_OEM_PERIOD, ".>", "юЮ"),
        (VK_OEM_2, "/?", ".,"),
        (VK_OEM_3, "`~", "ёЁ"),
        (VK_OEM_4, "[{", "хХ"),
        (VK_OEM_5, "\\|", "\\/"),
        (VK_OEM_6, "]}", "ъЪ"),
        (VK_OEM_7, "'\"", "эЭ"),
        (VK_OEM_102, "\\|", "\\/"),
    ];
    for layout in [en, ru] {
        if layout == ru && !russian {
            continue;
        }
        for (key, english, russian) in oem {
            for shift in [false, true] {
                for caps in [false, true] {
                    state[VK_SHIFT.0 as usize] = if shift { 0x80 } else { 0 };
                    state[VK_CAPITAL.0 as usize] = u8::from(caps);
                    let expected: Vec<_> = if layout == en { english } else { russian }
                        .encode_utf16()
                        .collect();
                    let cased = ('а' as u16..='ё' as u16).contains(&expected[0])
                        && expected[0] != expected[1];
                    let index = usize::from(if layout == ru && cased {
                        shift != caps
                    } else {
                        shift
                    });
                    let (event, modifier) = translated(call, key.0, 1, layout, &state);
                    assert_eq!(
                        (event.role, event.length, event.text[0], modifier),
                        (1, 1, expected[index], 0),
                        "key {:?} layout {:?} shift {shift} caps {caps}",
                        key,
                        layout
                    );
                }
            }
        }
    }
}

#[test]
fn keyboard_synthetic_tip_requires_exact_profile_and_substitute() {
    let api = Api::load();
    let call: unsafe extern "system" fn(
        *const TF_INPUTPROCESSORPROFILE,
        u32,
        HKL,
        *mut HKL,
    ) -> HRESULT = unsafe { api.export(b"KeyrillTestProfileLayout\0") };
    let en = HKL(0x04090409usize as *mut _);
    let ru = HKL(0x04190419usize as *mut _);
    let mut profile = TF_INPUTPROCESSORPROFILE {
        dwProfileType: TF_PROFILETYPE_INPUTPROCESSOR,
        clsid: SERVICE_ID,
        guidProfile: PROFILE_EN,
        langid: 0x409,
        dwFlags: TF_IPP_FLAG_ACTIVE,
        hkl: HKL(0xe0200409usize as *mut _),
        hklSubstitute: en,
        ..Default::default()
    };
    for (mode, substitute, expected) in [
        (1, en, en),
        (2, en, HKL::default()),
        (1, ru, HKL::default()),
    ] {
        profile.hklSubstitute = substitute;
        let mut output = HKL(std::ptr::dangling_mut());
        unsafe {
            assert_eq!(call(&profile, mode, substitute, &mut output), S_OK);
        }
        assert_eq!(output, expected);
    }
}
