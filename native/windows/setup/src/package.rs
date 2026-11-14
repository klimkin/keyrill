//! Package validation never loads or executes candidate binaries.
use crate::Result;
use keyrill_windows_build::{Digest, Target, pe};
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Path, PathBuf};

/// Exact production executables in each client architecture.
pub const BINARIES: [&str; 4] = [
    "KeyrillTsf.dll",
    "KeyrillSettings.exe",
    "KeyrillRegister.exe",
    "KeyrillSetup.exe",
];
/// Fixed non-executable release resources.
pub const RESOURCES: [&str; 7] = [
    "resources/english.ico",
    "resources/russian.ico",
    "LICENSE",
    "model-notice.txt",
    "wordfreq-code-license.txt",
    "model-provenance.json",
    "licenses/dependencies.json",
];

/// Recognized upstream license-text filenames; their actual case is retained.
pub const LICENSE_NAMES: &[&str] = &[
    "LICENSE",
    "LICENSE-MIT",
    "LICENSE-APACHE",
    "LICENSE.txt",
    "LICENSE.md",
    "COPYING",
    "COPYRIGHT",
    "LICENSE-0BSD",
    "UNLICENSE",
    "LICENSE-APACHE.md",
    "LICENSE-MIT.md",
    "LICENSE-ZLIB.md",
    "LICENSE-BOOST",
    "LICENSE-UNICODE",
    "LICENSE-BSD",
    "license-apache-2.0",
    "license-mit",
];

/// A complete unsigned package whose bytes match its bounded manifest.
pub struct Package {
    directory: PathBuf,
    manifest: Value,
    digest: String,
    files: BTreeMap<String, String>,
}

/// Return whether a string is a canonical lowercase SHA-256 identity.
#[must_use]
pub fn is_digest(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}

fn text<'a>(object: &'a Value, key: &str) -> Result<&'a str> {
    object[key]
        .as_str()
        .ok_or_else(|| "package manifest field missing".into())
}

fn allowed_file(name: &str) -> bool {
    if RESOURCES.contains(&name) {
        return true;
    }
    if let Some((architecture, file)) = name.split_once('/')
        && ["x64", "x86"].contains(&architecture)
        && BINARIES.contains(&file)
    {
        return true;
    }
    let parts: Vec<_> = name.split('/').collect();
    parts.len() == 3
        && parts[0] == "licenses"
        && !["", ".", ".."].contains(&parts[1])
        && parts[1]
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'-' || b == b'.' || b == b'_')
        && LICENSE_NAMES.contains(&parts[2])
}

fn walk(root: &Path, directory: &Path, files: &mut BTreeSet<String>) -> Result<()> {
    for entry in fs::read_dir(directory)? {
        let entry = entry?;
        let path = entry.path();
        let metadata = fs::symlink_metadata(&path)?;
        if metadata.file_type().is_symlink() {
            return Err("package contains a symbolic link".into());
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::MetadataExt;
            if metadata.file_attributes() & 0x400 != 0 {
                return Err("package contains a reparse point".into());
            }
        }
        if metadata.is_dir() {
            walk(root, &path, files)?;
        } else if metadata.is_file() {
            files.insert(
                path.strip_prefix(root)?
                    .to_str()
                    .ok_or("non-Unicode package path")?
                    .replace('\\', "/"),
            );
        } else {
            return Err("package contains a non-file entry".into());
        }
    }
    Ok(())
}

/// Reject build-machine paths in both narrow and wide binary strings.
///
/// # Errors
/// Returns an error when a developer home or workspace path is embedded.
pub fn reject_developer_paths(bytes: &[u8]) -> Result<()> {
    fn contains_path(bytes: &[u8], marker: &[u8], wide: bool) -> bool {
        bytes
            .windows(marker.len())
            .enumerate()
            .any(|(index, window)| {
                if window != marker {
                    return false;
                }
                let tail = &bytes[index + marker.len()..];
                let step = if wide { 2 } else { 1 };
                for (letters, index) in (0..tail.len().min(256)).step_by(step).enumerate() {
                    let byte = tail[index];
                    if wide && tail.get(index + 1) != Some(&0) {
                        return false;
                    }
                    if byte == b'/' || byte == b'\\' {
                        return letters > 0;
                    }
                    if !(byte.is_ascii_alphanumeric() || b" -_.".contains(&byte)) {
                        return false;
                    }
                }
                false
            })
    }
    let narrow: Vec<_> = bytes.iter().map(u8::to_ascii_lowercase).collect();
    for marker in [
        ":\\users\\",
        "\\projects\\keyrill\\",
        "/home/",
        "/users/",
        "\\.cargo\\",
        "\\.rustup\\",
    ] {
        let wide: Vec<_> = marker.encode_utf16().flat_map(u16::to_le_bytes).collect();
        if contains_path(&narrow, marker.as_bytes(), false) || contains_path(&narrow, &wide, true) {
            return Err(
                "release artifact contains a developer path; rebuild with path remapping".into(),
            );
        }
    }
    Ok(())
}

impl Package {
    /// Validate the complete directory, manifest, architecture and public export boundary.
    ///
    /// # Errors
    /// Rejects incomplete, altered, extra, test, unsupported or malformed package contents.
    pub fn open(directory: &Path) -> Result<Self> {
        #[cfg(windows)]
        {
            use std::path::{Component, Prefix};
            if directory.components().any(|c|matches!(c,Component::Prefix(p) if matches!(p.kind(),Prefix::UNC(..)|Prefix::VerbatimUNC(..)|Prefix::DeviceNS(..)))) {
                return Err("release package must use a local filesystem path".into());
            }
            for ancestor in directory.ancestors() {
                if ancestor.as_os_str().is_empty() {
                    continue;
                }
                use std::os::windows::fs::MetadataExt;
                if fs::symlink_metadata(ancestor)?.file_attributes() & 0x400 != 0 {
                    return Err("release package ancestor is a reparse point".into());
                }
            }
        }
        let original_metadata = fs::symlink_metadata(directory)?;
        if original_metadata.file_type().is_symlink() {
            return Err("package root is a symbolic link".into());
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::MetadataExt;
            if original_metadata.file_attributes() & 0x400 != 0 {
                return Err("package root is a reparse point".into());
            }
        }
        let directory = directory.canonicalize()?;
        let manifest_bytes = fs::read(directory.join("package.json"))?;
        if manifest_bytes.len() > 1024 * 1024 {
            return Err("package manifest too large".into());
        }
        let manifest: Value = serde_json::from_slice(&manifest_bytes)?;
        if manifest["schema"] != 1 || manifest["unsigned"] != true {
            return Err("unsupported package manifest".into());
        }
        for key in ["source", "model", "dependencies"] {
            if !is_digest(text(&manifest, key)?) {
                return Err("invalid package provenance digest".into());
            }
        }
        let commit = text(&manifest, "commit")?;
        if commit.len() != 40 || !commit.bytes().all(|b| b.is_ascii_hexdigit()) {
            return Err("invalid source commit".into());
        }
        let version = text(&manifest, "version")?;
        if version != env!("CARGO_PKG_VERSION") {
            return Err("unsupported package version".into());
        }
        let compiler = text(&manifest, "compiler")?;
        if compiler.len() > 1024 || !compiler.starts_with("rustc ") {
            return Err("invalid compiler provenance".into());
        }
        let records = manifest["files"]
            .as_object()
            .ok_or("package files missing")?;
        let mut files = BTreeMap::new();
        for (name, hash) in records {
            if !allowed_file(name) {
                return Err("unapproved release file".into());
            }
            let hash = hash
                .as_str()
                .filter(|v| is_digest(v))
                .ok_or("invalid release file hash")?;
            files.insert(name.clone(), hash.to_owned());
        }
        for name in RESOURCES {
            if !files.contains_key(name) {
                return Err("required release resource missing".into());
            }
        }
        for target in [Target::X64, Target::X86] {
            let arch = &manifest["architectures"][target.name()];
            let production=Digest::of_bytes(format!("keyrill-windows-v2\nsource={}\ntarget={}\nprofile=release\ninstrumentation=none\nfeatures=\n",text(&manifest,"source")?,target.triple()).as_bytes()).to_string();
            if arch["target"] != target.triple() || text(arch, "production")? != production {
                return Err("release architecture provenance missing".into());
            }
            for binary in BINARIES {
                if !files.contains_key(&format!("{}/{binary}", target.name())) {
                    return Err("both production client architectures are required".into());
                }
            }
        }
        let mut actual = BTreeSet::new();
        walk(&directory, &directory, &mut actual)?;
        let mut expected: BTreeSet<_> = files.keys().cloned().collect();
        expected.insert("package.json".into());
        if actual != expected {
            return Err("package contains missing or stale files".into());
        }
        for (name, expected) in &files {
            let bytes = fs::read(directory.join(name))?;
            if Digest::of_bytes(&bytes).to_string() != *expected {
                return Err("package file hash mismatch".into());
            }
            if name.ends_with(".dll") || name.ends_with(".exe") {
                let target: Target = name
                    .split('/')
                    .next()
                    .ok_or("missing architecture")?
                    .parse()?;
                pe::validate(&bytes, target)?;
                reject_developer_paths(&bytes)?;
                if name.ends_with(".dll") {
                    pe::production(&bytes, target)?;
                }
            }
            if name.ends_with(".ico") && bytes != icon(name.contains("russian")) {
                return Err("profile icon resource differs from product resource".into());
            }
        }
        let provenance: Value =
            serde_json::from_slice(&fs::read(directory.join("model-provenance.json"))?)?;
        if provenance["output"]["sha256"] != manifest["model"] {
            return Err("model provenance missing".into());
        }
        let dependencies: Value =
            serde_json::from_slice(&fs::read(directory.join("licenses/dependencies.json"))?)?;
        let dependencies = dependencies
            .as_array()
            .filter(|v| !v.is_empty())
            .ok_or("dependency licenses missing")?;
        for dependency in dependencies {
            let license_files = dependency["files"]
                .as_array()
                .filter(|v| !v.is_empty())
                .ok_or("dependency license text missing")?;
            for file in license_files {
                if !files.contains_key(file.as_str().ok_or("invalid license path")?) {
                    return Err("dependency license text absent".into());
                }
            }
        }
        Ok(Self {
            directory,
            manifest,
            digest: Digest::of_bytes(&manifest_bytes).to_string(),
            files,
        })
    }

    /// Return the immutable manifest identity used for the installed version directory.
    #[must_use]
    pub fn digest(&self) -> &str {
        &self.digest
    }
    /// Return the verified source directory.
    #[must_use]
    pub fn directory(&self) -> &Path {
        &self.directory
    }
    /// Return the verified provenance document.
    #[must_use]
    pub fn manifest(&self) -> &Value {
        &self.manifest
    }
    /// Return the exact file receipt.
    #[must_use]
    pub fn files(&self) -> &BTreeMap<String, String> {
        &self.files
    }
}

/// Deterministic 32-pixel opaque profile icon with a key glyph and language color.
#[must_use]
pub fn icon(russian: bool) -> Vec<u8> {
    let size = 40 + 32 * 32 * 4 + 32 * 4;
    let mut bytes = vec![0, 0, 1, 0, 1, 0, 32, 32, 0, 0, 1, 0, 32, 0];
    bytes.extend((size as u32).to_le_bytes());
    bytes.extend(22u32.to_le_bytes());
    bytes.extend(40u32.to_le_bytes());
    bytes.extend(32i32.to_le_bytes());
    bytes.extend(64i32.to_le_bytes());
    bytes.extend(1u16.to_le_bytes());
    bytes.extend(32u16.to_le_bytes());
    bytes.extend([0u8; 24]);
    for y in (0..32i32).rev() {
        for x in 0..32i32 {
            let radius = (x - 11).pow(2) + (y - 12).pow(2);
            let key = (12..=42).contains(&radius)
                || ((15..=26).contains(&x) && (11..=14).contains(&y))
                || ((22..=25).contains(&x) && (13..=19).contains(&y));
            bytes.extend(if key {
                [255, 255, 255, 255]
            } else if russian {
                [130, 60, 160, 255]
            } else {
                [150, 105, 25, 255]
            });
        }
    }
    bytes.extend([0u8; 128]);
    bytes
}

#[cfg(test)]
pub(crate) mod tests;
