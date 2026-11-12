//! Public fixture expectations and independent metadata-first field evidence.

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PublicCase {
    Empty,
    ProbeG,
    FixtureEnBoundary,
    FixtureEnCorrected,
    FixtureEnWord,
    FixtureRuWord,
    FixtureContinuation,
    FixtureTwoWords,
    ManualProvisional,
    ManualProvisionalLiteral,
    ManualPc,
    ManualPcLiteral,
    ModelEn,
    ModelEnLiteral,
    ModelRu,
    ModelRuLiteral,
    PrefixSuffix,
}
impl PublicCase {
    pub const ALL: [Self; 17] = [
        Self::Empty,
        Self::ProbeG,
        Self::FixtureEnBoundary,
        Self::FixtureEnCorrected,
        Self::FixtureEnWord,
        Self::FixtureRuWord,
        Self::FixtureContinuation,
        Self::FixtureTwoWords,
        Self::ManualProvisional,
        Self::ManualProvisionalLiteral,
        Self::ManualPc,
        Self::ManualPcLiteral,
        Self::ModelEn,
        Self::ModelEnLiteral,
        Self::ModelRu,
        Self::ModelRuLiteral,
        Self::PrefixSuffix,
    ];
    pub fn parse(name: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|case| case.name() == name)
    }
    pub fn name(self) -> &'static str {
        [
            "empty",
            "probe-g",
            "fixture-en-boundary",
            "fixture-en-corrected",
            "fixture-en-word",
            "fixture-ru-word",
            "fixture-continuation",
            "fixture-two-words",
            "manual-provisional",
            "manual-provisional-literal",
            "manual-pc",
            "manual-pc-literal",
            "model-en",
            "model-en-literal",
            "model-ru",
            "model-ru-literal",
            "prefix-suffix",
        ][self as usize]
    }
    pub fn text(self) -> &'static str {
        [
            "",
            "g",
            "ghbdtn ",
            "привет ",
            "ghbdtn",
            "привет",
            "привет x",
            "привет привет ",
            "хорошо",
            "[jhjij",
            "ёлка",
            "`krf ",
            "список ",
            "cgbcjr ",
            "world ",
            "цщкдв ",
            "left привет right",
        ][self as usize]
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum ReadOnly {
    #[default]
    Unknown,
    Unavailable,
    Unsupported,
    Writable,
    ReadOnly,
    Mixed,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum Refusal {
    None,
    #[default]
    Target,
    NativeFocus,
    NativeFacts,
    NativeExcluded,
    Stale,
    Provider,
    Binding,
    ControlType,
    Password,
    Disabled,
    ReadOnly,
    Mixed,
    Writability,
    TextUnavailable,
    TextMismatch,
    Selection,
}
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct WindowFacts {
    pub handle: usize,
    pub pid: u32,
    pub tid: u32,
    pub control: i32,
    pub style: isize,
    pub exstyle: isize,
    pub enabled: bool,
    pub complete: bool,
    pub class_truncated: bool,
    pub class_name: Vec<u16>,
}
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct NativeFocus {
    pub pid: u32,
    pub foreground_tid: u32,
    pub process_started: u64,
    pub foreground_thread_started: u64,
    pub focused_thread_started: u64,
    pub foreground: usize,
    pub focused: WindowFacts,
    pub root: WindowFacts,
    pub parents: Vec<WindowFacts>,
    pub parents_truncated: bool,
}
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Metadata {
    pub refusal: Refusal,
    pub native: NativeFocus,
    pub provider_pid: i32,
    pub provider_window: usize,
    pub control_type: i32,
    pub exact_binding: bool,
    pub nonpassword: bool,
    pub enabled: bool,
    pub value_readonly: ReadOnly,
    pub text_readonly: ReadOnly,
    pub text_pattern: bool,
}
pub fn native_safety(value: &NativeFocus) -> Refusal {
    if !value.focused.complete || !value.root.complete || value.parents_truncated {
        return Refusal::NativeFacts;
    }
    if !value.focused.enabled || value.focused.style & (0x20 | 0x800) != 0 {
        return Refusal::NativeExcluded;
    }
    for parent in &value.parents {
        if !parent.complete {
            return Refusal::NativeFacts;
        }
        if !parent.enabled {
            return Refusal::NativeExcluded;
        }
    }
    if value.root.enabled {
        Refusal::None
    } else {
        Refusal::NativeExcluded
    }
}
pub fn writable(value: ReadOnly, text: ReadOnly) -> Refusal {
    use self::ReadOnly::*;
    if [value, text].contains(&ReadOnly) {
        return Refusal::ReadOnly;
    }
    if [value, text].contains(&Mixed) {
        return Refusal::Mixed;
    }
    if [value, text].contains(&Unknown) {
        return Refusal::Writability;
    }
    if [value, text].contains(&Writable) {
        Refusal::None
    } else {
        Refusal::Writability
    }
}
pub fn eligible(value: &Metadata) -> Refusal {
    if value.refusal != Refusal::None {
        return value.refusal;
    }
    let native = native_safety(&value.native);
    if native != Refusal::None {
        return native;
    }
    if !value.exact_binding {
        return Refusal::Binding;
    }
    if value.control_type != 50030 {
        return Refusal::ControlType;
    }
    if !value.nonpassword {
        return Refusal::Password;
    }
    if !value.enabled {
        return Refusal::Disabled;
    }
    writable(value.value_readonly, value.text_readonly)
}
pub trait Provider {
    fn metadata(&mut self) -> Metadata;
    fn document(&mut self, bound: usize) -> Result<Vec<u16>, Refusal>;
    fn end_caret(&mut self) -> Result<(), Refusal>;
}
pub fn check_public_case(provider: &mut impl Provider, case: PublicCase) -> Result<(), Refusal> {
    let evidence = provider.metadata();
    let refusal = eligible(&evidence);
    if refusal != Refusal::None {
        return Err(refusal);
    }
    if !evidence.text_pattern {
        return Err(Refusal::TextUnavailable);
    }
    let revalidate = |provider: &mut dyn Provider| {
        let now = provider.metadata();
        let failure = eligible(&now);
        if failure != Refusal::None {
            Err(failure)
        } else if now != evidence {
            Err(Refusal::Stale)
        } else {
            Ok(())
        }
    };
    revalidate(provider)?;
    let expected: Vec<u16> = case.text().encode_utf16().collect();
    let actual = provider.document(expected.len() + 1)?;
    if actual != expected {
        return Err(Refusal::TextMismatch);
    }
    revalidate(provider)?;
    provider.end_caret()?;
    revalidate(provider)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[derive(Default)]
    struct Fake {
        meta: Metadata,
        text: Vec<u16>,
        reads: u32,
        caret: bool,
    }
    impl Provider for Fake {
        fn metadata(&mut self) -> Metadata {
            self.meta.clone()
        }
        fn document(&mut self, _: usize) -> Result<Vec<u16>, Refusal> {
            self.reads += 1;
            Ok(self.text.clone())
        }
        fn end_caret(&mut self) -> Result<(), Refusal> {
            if self.caret {
                Ok(())
            } else {
                Err(Refusal::Selection)
            }
        }
    }
    fn safe() -> Fake {
        let window = WindowFacts {
            enabled: true,
            complete: true,
            ..Default::default()
        };
        Fake {
            meta: Metadata {
                refusal: Refusal::None,
                native: NativeFocus {
                    focused: window.clone(),
                    root: window,
                    ..Default::default()
                },
                exact_binding: true,
                control_type: 50030,
                nonpassword: true,
                enabled: true,
                value_readonly: ReadOnly::Unsupported,
                text_readonly: ReadOnly::Writable,
                text_pattern: true,
                ..Default::default()
            },
            caret: true,
            ..Default::default()
        }
    }
    #[test]
    fn secure_excluded_and_uncertain_fields_have_zero_character_reads() {
        for refusal in [
            Refusal::Target,
            Refusal::NativeFocus,
            Refusal::NativeFacts,
            Refusal::NativeExcluded,
            Refusal::Stale,
            Refusal::Provider,
            Refusal::Binding,
            Refusal::ControlType,
            Refusal::Password,
            Refusal::Disabled,
            Refusal::ReadOnly,
            Refusal::Mixed,
            Refusal::Writability,
        ] {
            let mut provider = safe();
            provider.meta.refusal = refusal;
            assert_eq!(
                check_public_case(&mut provider, PublicCase::Empty),
                Err(refusal)
            );
            assert_eq!(provider.reads, 0);
        }
        for state in [
            ReadOnly::ReadOnly,
            ReadOnly::Mixed,
            ReadOnly::Unknown,
            ReadOnly::Unavailable,
            ReadOnly::Unsupported,
        ] {
            let mut provider = safe();
            provider.meta.value_readonly = ReadOnly::Unsupported;
            provider.meta.text_readonly = state;
            assert!(check_public_case(&mut provider, PublicCase::Empty).is_err());
            assert_eq!(provider.reads, 0);
        }
    }
    #[test]
    fn named_case_requires_complete_document_and_exact_end_caret() {
        for case in PublicCase::ALL {
            let mut provider = safe();
            provider.text = case.text().encode_utf16().collect();
            assert_eq!(check_public_case(&mut provider, case), Ok(()));
            provider.text.push(0);
            assert_eq!(
                check_public_case(&mut provider, case),
                Err(Refusal::TextMismatch)
            );
            provider.text.pop();
            provider.caret = false;
            assert_eq!(
                check_public_case(&mut provider, case),
                Err(Refusal::Selection)
            );
        }
    }
}
