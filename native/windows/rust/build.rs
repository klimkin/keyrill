//! Build mechanical native-call fences and the shared artifact identity.

use std::path::PathBuf;
use std::process::Command;

fn identity() {
    let manifest = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").expect("Cargo manifest"));
    let root = manifest.ancestors().nth(3).expect("workspace root");
    let script = root.join("native/windows/build_fingerprint.py");
    let python = std::env::var_os("PYTHON").unwrap_or_else(|| {
        if cfg!(windows) {
            "python".into()
        } else {
            "python3".into()
        }
    });
    println!("cargo:rerun-if-env-changed=PYTHON");
    for directory in ["include", "crates", "native/windows"] {
        println!("cargo:rerun-if-changed={}", root.join(directory).display());
    }
    let listed = Command::new(&python)
        .arg(&script)
        .arg("--list")
        .output()
        .expect("fingerprint input list");
    assert!(
        listed.status.success(),
        "fingerprint input enumeration failed"
    );
    let paths = String::from_utf8(listed.stdout).expect("UTF-8 fingerprint paths");
    for path in paths.lines() {
        println!("cargo:rerun-if-changed={}", root.join(path).display());
    }
    let feature = if std::env::var_os("CARGO_FEATURE_TEST_API").is_some() {
        "test-api"
    } else {
        "production"
    };
    let calculated = Command::new(&python)
        .arg(&script)
        .args(["--feature", feature])
        .output()
        .expect("artifact fingerprint");
    assert!(calculated.status.success(), "artifact fingerprint failed");
    let digest = String::from_utf8(calculated.stdout).expect("ASCII artifact fingerprint");
    let digest = digest.trim();
    assert!(
        digest.len() == 64 && digest.bytes().all(|byte| byte.is_ascii_hexdigit()),
        "invalid artifact fingerprint"
    );
    let words: Vec<_> = (0..64)
        .step_by(8)
        .map(|offset| format!("0x{}", &digest[offset..offset + 8]))
        .collect();
    let output = PathBuf::from(std::env::var_os("OUT_DIR").expect("Cargo output directory"));
    std::fs::write(
        output.join("build_identity.rs"),
        format!(
            "pub(crate) const BUILD_DIGEST: [u32; 8] = [{}];\n",
            words.join(", ")
        ),
    )
    .expect("write artifact fingerprint");
}

fn main() {
    identity();
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        println!("cargo:rustc-cdylib-link-arg=/EXPORT:DllGetClassObject,PRIVATE");
        println!("cargo:rustc-cdylib-link-arg=/EXPORT:DllCanUnloadNow,PRIVATE");
    }
}
