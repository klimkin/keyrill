//! Rust commands for building and verifying the Windows port.

use keyrill_windows_build::{BuildConfig, Instrumentation, Variant, identity, source_inputs};
use std::path::Path;

mod cargo;
mod closure;
mod coverage;
mod model;
mod pe;
mod sanitize;
mod windows;

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<_> = std::env::args().skip(1).collect();
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .ok_or("workspace root unavailable")?;
    match arguments.as_slice() {
        [platform, command] if platform == "windows" && command == "build" => {
            let build = windows::build(root, Instrumentation::None)?;
            println!("{}", build.directory.display());
        }
        [platform, command] if platform == "windows" && command == "controlled" => {
            let build = windows::build(root, Instrumentation::None)?;
            windows::controlled(&build)?;
        }
        [platform, command] if platform == "windows" && command == "check" => {
            closure::check(root)?;
            model::check(root)?;
            windows::common_checks(root)?;
            let build = windows::build(root, Instrumentation::None)?;
            let mut executed = windows::controlled(&build)?;
            executed.extend(windows::native_tests(root, &build)?);
            executed.extend(windows::smoke_tests(&build)?);
            coverage::check(root, &executed)?;
        }
        [platform, command] if platform == "windows" && command == "sanitize" => {
            closure::check(root)?;
            let build = windows::build(root, Instrumentation::Address)?;
            let mut executed = windows::controlled(&build)?;
            executed.extend(windows::native_tests(root, &build)?);
            executed.extend(windows::smoke_tests(&build)?);
            coverage::check(root, &executed)?;
        }
        [platform, command] if platform == "windows" && command == "model-check" => {
            model::check(root)?;
        }
        [platform, command] if platform == "windows" && command == "inventory" => {
            for path in source_inputs(root)? {
                println!(
                    "{}",
                    path.strip_prefix(root)?
                        .to_string_lossy()
                        .replace('\\', "/")
                );
            }
        }
        [platform, command, variant] if platform == "windows" && command == "fingerprint" => {
            let variant = match variant.as_str() {
                "production" => Variant::Production,
                "test-api" => Variant::TestApi,
                _ => return Err("expected production or test-api".into()),
            };
            let config = BuildConfig {
                target: "x86_64-pc-windows-msvc",
                profile: "release",
                instrumentation: Instrumentation::None,
            };
            println!("{}", identity(root, config)?.variant(variant));
        }
        _ => {
            return Err(
                "usage: windows inventory | windows fingerprint production|test-api".into(),
            );
        }
    }
    Ok(())
}

fn main() -> std::process::ExitCode {
    match run() {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            std::process::ExitCode::FAILURE
        }
    }
}
