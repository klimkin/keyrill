use crate::{cargo, pe, sanitize};
use cargo::Result;
use keyrill_windows_build::{BuildConfig, Digest, Identity, Instrumentation, identity};
use serde_json::json;
use std::collections::BTreeSet;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

const TARGET: &str = "x86_64-pc-windows-msvc";
const PUBLIC_EXPORTS: [&str; 2] = ["DllCanUnloadNow", "DllGetClassObject"];

pub(crate) fn common_checks(root: &Path) -> Result<()> {
    for arguments in [
        vec!["fmt", "--all", "--", "--check"],
        vec!["fmt", "--manifest-path", "fuzz/Cargo.toml", "--", "--check"],
        vec![
            "clippy",
            "--locked",
            "--workspace",
            "--all-targets",
            "--",
            "-D",
            "warnings",
        ],
        vec![
            "clippy",
            "--locked",
            "-p",
            "keyrill-windows",
            "--all-targets",
            "--features",
            "test-api",
            "--",
            "-D",
            "warnings",
        ],
        vec![
            "clippy",
            "--locked",
            "-p",
            "keyrill-windows-proof",
            "--all-targets",
            "--features",
            "dll-tests",
            "--",
            "-D",
            "warnings",
        ],
        vec!["test", "--locked", "--workspace"],
        vec!["doc", "--locked", "--workspace", "--no-deps"],
        vec!["build", "--locked", "--workspace", "--release"],
    ] {
        let mut command = Command::new("cargo");
        command
            .current_dir(root)
            .args(arguments)
            .env("RUSTDOCFLAGS", "-D warnings");
        cargo::run(&mut command)?;
    }
    Ok(())
}

pub(crate) struct Build {
    pub(crate) directory: PathBuf,
    pub(crate) identity: Identity,
    pub(crate) test_executable: PathBuf,
    runtime_path: Option<OsString>,
    compiler_environment: Vec<(&'static str, &'static str)>,
}

impl Build {
    fn command(&self, executable: &Path) -> Command {
        let mut command = Command::new(executable);
        if let Some(path) = &self.runtime_path {
            command.env("PATH", path);
        }
        command
            .env(
                "KEYRILL_TEST_DLL",
                self.directory.join("KeyrillTsfTest.dll"),
            )
            .env(
                "KEYRILL_PRODUCTION_DLL",
                self.directory.join("KeyrillTsf.dll"),
            )
            .env(
                "KEYRILL_WORKER_SMOKE",
                self.directory.join("KeyrillWorkerSmoke.exe"),
            )
            .env(
                "KEYRILL_TEST_BUILD_DIGEST",
                self.identity
                    .test_api
                    .words()
                    .map(|word| format!("{word:08x}"))
                    .join(","),
            );
        command
    }
}

fn copy(source: &Path, directory: &Path, name: &str) -> Result<PathBuf> {
    let destination = directory.join(name);
    fs::copy(source, &destination)?;
    Ok(destination)
}

fn validate_exports(path: &Path, private: bool) -> Result<()> {
    let actual = pe::exports(&fs::read(path)?)?;
    let required: BTreeSet<_> = PUBLIC_EXPORTS.into_iter().map(str::to_owned).collect();
    if !required.is_subset(&actual) {
        return Err("DLL is missing public COM exports".into());
    }
    let private_count = actual
        .iter()
        .filter(|name| name.starts_with("KeyrillTest"))
        .count();
    if (private && private_count == 0) || (!private && private_count != 0) {
        return Err("DLL test feature export boundary violated".into());
    }
    Ok(())
}

pub(crate) fn build(root: &Path, instrumentation: Instrumentation) -> Result<Build> {
    if !cfg!(all(windows, target_arch = "x86_64")) {
        return Err("Windows artifacts require an x64 Windows host".into());
    }
    let config = BuildConfig {
        target: TARGET,
        profile: "release",
        instrumentation,
    };
    let before = identity(root, config)?;
    let (directory_name, environment, runtime_path) = match instrumentation {
        Instrumentation::None => (
            "target/windows-rust-check",
            vec![("KEYRILL_INSTRUMENTATION", "none"), ("RUSTFLAGS", "")],
            None,
        ),
        Instrumentation::Address => (
            "target/windows-rust-sanitize",
            vec![
                ("KEYRILL_INSTRUMENTATION", "address"),
                ("RUSTC_BOOTSTRAP", "1"),
                ("RUSTFLAGS", "-Zsanitizer=address -Cdebuginfo=1"),
            ],
            Some(sanitize::prepare(root)?),
        ),
    };
    let directory = root.join(directory_name);
    fs::create_dir_all(directory.join("tests"))?;
    let target_directory = directory.join("cargo-production");
    let production = cargo::artifacts(
        root,
        &[
            "build",
            "--release",
            "--target",
            TARGET,
            "-p",
            "keyrill-windows",
            "--target-dir",
            target_directory
                .to_str()
                .ok_or("target path is not Unicode")?,
        ],
        &environment,
    )?;
    let production_dll = copy(&cargo::dll(&production)?, &directory, "KeyrillTsf.dll")?;
    copy(
        &cargo::executable(&production, "KeyrillRegister", false)?,
        &directory,
        "KeyrillRegister.exe",
    )?;
    validate_exports(&production_dll, false)?;

    let target_directory = directory.join("cargo-test-api");
    let test_api = cargo::artifacts(
        root,
        &[
            "build",
            "--release",
            "--target",
            TARGET,
            "-p",
            "keyrill-windows",
            "--features",
            "test-api",
            "--target-dir",
            target_directory
                .to_str()
                .ok_or("target path is not Unicode")?,
        ],
        &environment,
    )?;
    let test_dll = copy(&cargo::dll(&test_api)?, &directory, "KeyrillTsfTest.dll")?;
    validate_exports(&test_dll, true)?;

    let target_directory = directory.join("cargo-proof");
    let proof = cargo::artifacts(
        root,
        &[
            "build",
            "--release",
            "--target",
            TARGET,
            "-p",
            "keyrill-windows-proof",
            "--bins",
            "--target-dir",
            target_directory
                .to_str()
                .ok_or("target path is not Unicode")?,
        ],
        &environment,
    )?;
    for name in [
        "KeyrillProofEditor",
        "KeyrillProofInspect",
        "KeyrillHostInspect",
        "KeyrillDllSmoke",
        "KeyrillWorkerSmoke",
        "KeyrillDiagnosticDecoder",
    ] {
        copy(
            &cargo::executable(&proof, name, false)?,
            &directory,
            &format!("{name}.exe"),
        )?;
    }
    let tests = cargo::artifacts(
        root,
        &[
            "test",
            "--no-run",
            "--release",
            "--target",
            TARGET,
            "-p",
            "keyrill-windows-proof",
            "--test",
            "controlled",
            "--features",
            "dll-tests",
            "--target-dir",
            target_directory
                .to_str()
                .ok_or("target path is not Unicode")?,
        ],
        &environment,
    )?;
    let test_executable = copy(
        &cargo::executable(&tests, "controlled", true)?,
        &directory.join("tests"),
        "KeyrillProofEditor.exe",
    )?;
    let after = identity(root, config)?;
    if before != after {
        return Err("source inputs changed while Windows artifacts were built".into());
    }
    let mut hashes = serde_json::Map::new();
    for entry in fs::read_dir(&directory)? {
        let entry = entry?;
        if entry.file_type()?.is_file()
            && matches!(
                entry
                    .path()
                    .extension()
                    .and_then(|extension| extension.to_str()),
                Some("dll" | "exe")
            )
        {
            hashes.insert(
                entry.file_name().to_string_lossy().into_owned(),
                json!(Digest::of_bytes(&fs::read(entry.path())?).to_string()),
            );
        }
    }
    hashes.insert(
        "tests/KeyrillProofEditor.exe".to_owned(),
        json!(Digest::of_bytes(&fs::read(&test_executable)?).to_string()),
    );
    let manifest = json!({"schema": 2, "target": TARGET, "profile": "release", "instrumentation": match instrumentation { Instrumentation::None => "none", Instrumentation::Address => "address" }, "source": before.source.to_string(), "production": before.production.to_string(), "testApi": before.test_api.to_string(), "files": hashes});
    fs::write(
        directory.join("artifacts.json"),
        serde_json::to_vec_pretty(&manifest)?,
    )?;
    Ok(Build {
        directory,
        identity: before,
        test_executable,
        runtime_path,
        compiler_environment: environment,
    })
}

pub(crate) fn controlled(build: &Build) -> Result<BTreeSet<String>> {
    run_tests(build, &build.test_executable, "controlled", None)
}

fn run_tests(
    build: &Build,
    executable: &Path,
    prefix: &str,
    decoder_identity: Option<Digest>,
) -> Result<BTreeSet<String>> {
    let mut command = build.command(executable);
    let listing = command
        .args(["--list", "--format", "terse"])
        .stdin(Stdio::null())
        .output()?;
    if !listing.status.success() {
        return Err("controlled test enumeration failed".into());
    }
    let tests: BTreeSet<_> = std::str::from_utf8(&listing.stdout)?
        .lines()
        .filter_map(|line| {
            line.strip_suffix(": test")
                .map(|name| format!("{prefix}::{name}"))
        })
        .collect();
    let count = tests.len();
    if count == 0 {
        return Err("controlled DLL suite has zero tests".into());
    }
    println!("Running {count} independently enumerated tests from {prefix}.");
    let mut command = build.command(executable);
    command.args(["--test-threads=1", "--include-ignored"]);
    if let Some(digest) = decoder_identity {
        command
            .env(
                "KEYRILL_DIAGNOSTIC_DECODER",
                build.directory.join("KeyrillDiagnosticDecoder.exe"),
            )
            .env("KEYRILL_DECODER_BUILD_DIGEST", digest.to_string());
    }
    cargo::run(&mut command)?;
    Ok(tests)
}

pub(crate) fn native_tests(root: &Path, build: &Build) -> Result<BTreeSet<String>> {
    let mut executed = BTreeSet::new();
    for (variant, target_directory, features, digest) in [
        (
            "production",
            "cargo-production",
            None,
            build.identity.production,
        ),
        (
            "test-api",
            "cargo-test-api",
            Some("test-api"),
            build.identity.test_api,
        ),
    ] {
        let target = build.directory.join(target_directory);
        let mut arguments = vec![
            "test",
            "--no-run",
            "--release",
            "--target",
            TARGET,
            "-p",
            "keyrill-windows",
            "--lib",
            "--target-dir",
            target.to_str().ok_or("target path is not Unicode")?,
        ];
        if let Some(features) = features {
            arguments.extend(["--features", features]);
        }
        let artifacts = cargo::artifacts(root, &arguments, &build.compiler_environment)?;
        let executable = cargo::executable(&artifacts, "keyrill_windows", true)?;
        executed.extend(run_tests(
            build,
            &executable,
            &format!("runtime::{variant}"),
            Some(digest),
        )?);
    }
    let target = build.directory.join("cargo-proof");
    let artifacts = cargo::artifacts(
        root,
        &[
            "test",
            "--no-run",
            "--release",
            "--target",
            TARGET,
            "-p",
            "keyrill-windows-proof",
            "--lib",
            "--features",
            "dll-tests",
            "--target-dir",
            target.to_str().ok_or("target path is not Unicode")?,
        ],
        &build.compiler_environment,
    )?;
    let directory = build.directory.join("tests/fixtures");
    fs::create_dir_all(&directory)?;
    let executable = copy(
        &cargo::executable(&artifacts, "keyrill_windows_proof", true)?,
        &directory,
        "KeyrillProofEditor.exe",
    )?;
    executed.extend(run_tests(build, &executable, "proof", None)?);

    let target = build.directory.join("cargo-production");
    let artifacts = cargo::artifacts(
        root,
        &[
            "test",
            "--no-run",
            "--release",
            "--target",
            TARGET,
            "-p",
            "keyrill-ffi",
            "--test",
            "windows_abi",
            "--target-dir",
            target.to_str().ok_or("target path is not Unicode")?,
        ],
        &build.compiler_environment,
    )?;
    let executable = cargo::executable(&artifacts, "windows_abi", true)?;
    executed.extend(run_tests(build, &executable, "abi", None)?);
    Ok(executed)
}

pub(crate) fn smoke_tests(build: &Build) -> Result<BTreeSet<String>> {
    let executable = build.directory.join("KeyrillDllSmoke.exe");
    cargo::run(
        build
            .command(&executable)
            .arg("factory")
            .arg(build.directory.join("KeyrillTsf.dll")),
    )?;
    cargo::run(build.command(&executable).arg("activate"))?;
    let negative = build.directory.join("missing-adjacent");
    fs::create_dir_all(&negative)?;
    if negative.join("KeyrillTsf.dll").try_exists()? {
        return Err("negative activation directory contains a DLL".into());
    }
    let executable = copy(&executable, &negative, "KeyrillDllSmoke.exe")?;
    cargo::run(build.command(&executable).arg("missing"))?;
    Ok(BTreeSet::from([
        "smoke::factory".to_owned(),
        "smoke::activation".to_owned(),
        "smoke::missing-adjacent".to_owned(),
    ]))
}
