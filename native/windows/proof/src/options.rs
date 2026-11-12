//! Strict argument parsing for the independent host inspector and editor.
use crate::field::PublicCase;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FixtureMode {
    Normal,
    DenyTsfLocks,
    ProveOwnedShutdown,
    ProveOwnedEnToRu,
}
impl FixtureMode {
    pub fn parse(arguments: &[String]) -> Option<Self> {
        match arguments
            .iter()
            .map(String::as_str)
            .collect::<Vec<_>>()
            .as_slice()
        {
            [] => Some(Self::Normal),
            ["--deny-tsf-locks"] => Some(Self::DenyTsfLocks),
            ["--prove-owned-shutdown"] => Some(Self::ProveOwnedShutdown),
            ["--prove-owned-en-to-ru"] => Some(Self::ProveOwnedEnToRu),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Command {
    Build,
    Host,
    Service,
    Field,
    PublicCase(PublicCase),
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Options {
    pub command: Command,
    pub pid: u32,
    pub seconds: u32,
    pub expected_build: Option<[u32; 8]>,
}

pub fn number(value: &str) -> Option<u32> {
    if value.is_empty() || !value.bytes().all(|byte| byte.is_ascii_digit()) {
        return None;
    }
    value
        .parse::<u32>()
        .ok()
        .filter(|&word| word != 0 && word != u32::MAX)
}

pub fn digest(value: &str) -> Option<[u32; 8]> {
    if value.len() != 64 || !value.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        return None;
    }
    let mut result = [0; 8];
    for (index, word) in result.iter_mut().enumerate() {
        *word = u32::from_str_radix(&value[index * 8..index * 8 + 8], 16).ok()?;
    }
    Some(result)
}

pub fn parse(arguments: &[String]) -> Option<Options> {
    let mut result = Options {
        command: Command::Build,
        pid: 0,
        seconds: 0,
        expected_build: None,
    };
    if arguments == ["build"] {
        return Some(result);
    }
    if !(2..=6).contains(&arguments.len()) {
        return None;
    }
    result.pid = number(&arguments[1])?;
    result.command = match arguments[0].as_str() {
        "host" => Command::Host,
        "service" => Command::Service,
        "field" => Command::Field,
        "public-case" if arguments.len() == 3 => {
            Command::PublicCase(PublicCase::parse(&arguments[2])?)
        }
        _ => return None,
    };
    if matches!(result.command, Command::PublicCase(_)) {
        return Some(result);
    }
    if result.command != Command::Service {
        return (arguments.len() == 2).then_some(result);
    }
    if arguments.len() % 2 != 0 {
        return None;
    }
    for pair in arguments[2..].chunks_exact(2) {
        match pair[0].as_str() {
            "--arm-seconds" if result.seconds == 0 => {
                result.seconds = number(&pair[1])?;
                if result.seconds > 3600 {
                    return None;
                }
            }
            "--expect-server-build" if result.expected_build.is_none() => {
                result.expected_build = Some(digest(&pair[1])?)
            }
            _ => return None,
        }
    }
    Some(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn args(values: &[&str]) -> Vec<String> {
        values.iter().map(|value| (*value).into()).collect()
    }
    #[test]
    fn host_inspector_arguments() {
        assert_eq!(parse(&args(&["build"])).unwrap().command, Command::Build);
        for command in ["host", "service", "field"] {
            assert_eq!(parse(&args(&[command, "123"])).unwrap().pid, 123);
        }
        for case in PublicCase::ALL {
            assert_eq!(
                parse(&args(&["public-case", "123", case.name()]))
                    .unwrap()
                    .command,
                Command::PublicCase(case)
            );
        }
        for value in [
            "",
            "0",
            "-1",
            "+1",
            " 1",
            "4294967295",
            "4294967296",
            "999999999999999999999",
        ] {
            assert!(parse(&args(&["service", value])).is_none());
        }
        for value in ["0", "3601", "-1", "1x"] {
            assert!(parse(&args(&["service", "123", "--arm-seconds", value])).is_none());
        }
        let hash = "0123456789abcdefABCDEF019876543200000000ffffffff8000000000000001";
        let options = parse(&args(&[
            "service",
            "123",
            "--arm-seconds",
            "3600",
            "--expect-server-build",
            hash,
        ]))
        .unwrap();
        assert_eq!(
            options.expected_build,
            Some([
                0x01234567,
                0x89abcdef,
                0xabcdef01,
                0x98765432,
                0,
                u32::MAX,
                0x80000000,
                1
            ])
        );
        assert!(
            parse(&args(&[
                "service",
                "123",
                "--expect-server-build",
                hash,
                "--arm-seconds",
                "1"
            ]))
            .is_some()
        );
        for invalid in [
            "",
            "0",
            &hash[..63],
            "g123456789abcdefABCDEF019876543200000000ffffffff8000000000000001",
        ] {
            assert!(digest(invalid).is_none());
        }
        for values in [
            vec!["public-case", "123"],
            vec!["public-empty", "123"],
            vec!["public-case", "123", "EMPTY"],
            vec!["host", "123", "--expect-server-build", hash],
            vec!["service", "123", "--expect-server-build"],
            vec![
                "service",
                "123",
                "--expect-server-build",
                hash,
                "--expect-server-build",
                hash,
            ],
            vec!["service", "123", "--arm-seconds", "1", "--arm-seconds", "2"],
            vec!["service", "123", "--ignore-build", hash],
        ] {
            assert!(parse(&args(&values)).is_none(), "{values:?}");
        }
    }
    #[test]
    fn editor_mode_arguments() {
        assert_eq!(FixtureMode::parse(&[]), Some(FixtureMode::Normal));
        for (flag, mode) in [
            ("--deny-tsf-locks", FixtureMode::DenyTsfLocks),
            ("--prove-owned-shutdown", FixtureMode::ProveOwnedShutdown),
            ("--prove-owned-en-to-ru", FixtureMode::ProveOwnedEnToRu),
        ] {
            assert_eq!(FixtureMode::parse(&args(&[flag])), Some(mode));
        }
        assert!(FixtureMode::parse(&args(&["--deny-tsf-locks", "extra"])).is_none());
    }
}
