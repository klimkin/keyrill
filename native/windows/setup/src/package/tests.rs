use super::*;
use serde_json::json;
use std::sync::atomic::{AtomicU64, Ordering};
static NEXT: AtomicU64 = AtomicU64::new(0);

pub(crate) struct Fixture(pub(crate) PathBuf);
impl Fixture {
    pub(crate) fn new() -> Self {
        let base = std::env::temp_dir().join("Keyrill-package-tests");
        fs::create_dir_all(&base).unwrap();
        loop {
            let path = base.join(format!(
                "{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            match fs::create_dir(&path) {
                Ok(()) => return Self(path),
                Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {}
                Err(error) => panic!("{error}"),
            }
        }
    }
    pub(crate) fn candidate(&self) -> Value {
        let source = Digest::of_bytes(b"synthetic source").to_string();
        let model = Digest::of_bytes(b"synthetic model").to_string();
        let mut architectures = serde_json::Map::new();
        let mut files = serde_json::Map::new();
        for target in [Target::X64, Target::X86] {
            architectures.insert(target.name().into(),json!({"target":target.triple(),"production":Digest::of_bytes(format!("keyrill-windows-v2\nsource={source}\ntarget={}\nprofile=release\ninstrumentation=none\nfeatures=\n",target.triple()).as_bytes()).to_string()}));
            for name in BINARIES {
                self.put(
                    &format!("{}/{name}", target.name()),
                    &synthetic_pe(target, false),
                    &mut files,
                );
            }
        }
        for name in RESOURCES {
            let bytes=match name {
                "resources/english.ico"=>icon(false),"resources/russian.ico"=>icon(true),
                "model-provenance.json"=>serde_json::to_vec(&json!({"output":{"sha256":model}})).unwrap(),
                "licenses/dependencies.json"=>serde_json::to_vec(&json!([{"name":"synthetic","version":"1","license":"MIT","files":["licenses/synthetic-1/LICENSE"]}])).unwrap(),
                _=>b"Synthetic public license test text\n".to_vec(),
            };
            self.put(name, &bytes, &mut files);
        }
        self.put(
            "licenses/synthetic-1/LICENSE",
            b"Synthetic MIT license",
            &mut files,
        );
        let manifest = json!({"schema":1,"unsigned":true,"version":env!("CARGO_PKG_VERSION"),"commit":"1".repeat(40),"compiler":"rustc synthetic-test-only","source":source,"model":model,"dependencies":Digest::of_bytes(b"synthetic lock").to_string(),"architectures":architectures,"files":files});
        self.save(&manifest);
        manifest
    }
    fn put(&self, name: &str, bytes: &[u8], files: &mut serde_json::Map<String, Value>) {
        let path = self.0.join(name);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, bytes).unwrap();
        files.insert(name.into(), json!(Digest::of_bytes(bytes).to_string()));
    }
    fn replace(&self, manifest: &mut Value, name: &str, bytes: &[u8]) {
        fs::write(self.0.join(name), bytes).unwrap();
        manifest["files"][name] = json!(Digest::of_bytes(bytes).to_string());
        self.save(manifest);
    }
    fn save(&self, value: &Value) {
        fs::write(
            self.0.join("package.json"),
            serde_json::to_vec_pretty(value).unwrap(),
        )
        .unwrap();
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        assert_eq!(
            self.0.parent(),
            Some(std::env::temp_dir().join("Keyrill-package-tests").as_path())
        );
        fs::remove_dir_all(&self.0).unwrap();
    }
}

fn synthetic_pe(target: Target, test_export: bool) -> Vec<u8> {
    fn word(b: &mut [u8], i: usize, n: u16) {
        b[i..i + 2].copy_from_slice(&n.to_le_bytes());
    }
    fn dword(b: &mut [u8], i: usize, n: u32) {
        b[i..i + 4].copy_from_slice(&n.to_le_bytes());
    }
    let mut b = vec![0; 1024];
    b[..2].copy_from_slice(b"MZ");
    dword(&mut b, 0x3c, 64);
    b[64..68].copy_from_slice(b"PE\0\0");
    word(&mut b, 68, target.pe_machine());
    word(&mut b, 70, 1);
    word(&mut b, 84, 120);
    word(&mut b, 88, target.pe_magic());
    let export = if target == Target::X64 { 200 } else { 184 };
    dword(&mut b, export - 4, 1);
    dword(&mut b, export, 0x1000);
    dword(&mut b, export + 4, 40);
    dword(&mut b, 220, 0x1000);
    dword(&mut b, 224, 512);
    dword(&mut b, 228, 512);
    let names = if test_export {
        vec!["DllCanUnloadNow", "DllGetClassObject", "KeyrillTestUnsafe"]
    } else {
        vec!["DllCanUnloadNow", "DllGetClassObject"]
    };
    dword(&mut b, 532, names.len() as u32);
    dword(&mut b, 536, names.len() as u32);
    dword(&mut b, 540, 0x1040);
    dword(&mut b, 544, 0x1050);
    dword(&mut b, 548, 0x1060);
    let mut position = 640;
    for (index, name) in names.iter().enumerate() {
        dword(&mut b, 576 + index * 4, 0x2000);
        dword(&mut b, 592 + index * 4, 0x1000 + position as u32 - 512);
        word(&mut b, 608 + index * 2, index as u16);
        b[position..position + name.len()].copy_from_slice(name.as_bytes());
        position += name.len() + 1;
    }
    b
}

#[test]
fn synthetic_pe_package_is_deterministic_and_copy_verifiable_without_execution() {
    let one = Fixture::new();
    let two = Fixture::new();
    one.candidate();
    two.candidate();
    let one = Package::open(&one.0).unwrap();
    let two = Package::open(&two.0).unwrap();
    assert_eq!(one.digest(), two.digest());
    assert_eq!(one.files(), two.files());
}
#[test]
fn refuses_missing_resources_stale_files_and_missing_client_architecture() {
    for name in [
        "resources/english.ico",
        "x86/KeyrillTsf.dll",
        "licenses/synthetic-1/LICENSE",
    ] {
        let fixture = Fixture::new();
        fixture.candidate();
        fs::remove_file(fixture.0.join(name)).unwrap();
        assert!(Package::open(&fixture.0).is_err());
    }
    for name in [
        "stale.dll",
        "words-en.txt",
        "KeyrillProofEditor.exe",
        "KeyrillTsfTest.dll",
    ] {
        let fixture = Fixture::new();
        fixture.candidate();
        fs::write(fixture.0.join(name), b"unexpected").unwrap();
        assert!(Package::open(&fixture.0).is_err());
    }
}
#[test]
fn refuses_wrong_target_test_exports_resource_tampering_and_changed_source() {
    let fixture = Fixture::new();
    let mut manifest = fixture.candidate();
    fixture.replace(
        &mut manifest,
        "x86/KeyrillTsf.dll",
        &synthetic_pe(Target::X64, false),
    );
    assert!(Package::open(&fixture.0).is_err());
    fixture.replace(
        &mut manifest,
        "x86/KeyrillTsf.dll",
        &synthetic_pe(Target::X86, true),
    );
    assert!(Package::open(&fixture.0).is_err());
    fixture.replace(
        &mut manifest,
        "x86/KeyrillTsf.dll",
        &synthetic_pe(Target::X86, false),
    );
    assert!(Package::open(&fixture.0).is_ok());
    fixture.replace(&mut manifest, "resources/english.ico", b"stale icon");
    assert!(Package::open(&fixture.0).is_err());
    fixture.replace(&mut manifest, "resources/english.ico", &icon(false));
    manifest["source"] = json!("f".repeat(64));
    fixture.save(&manifest);
    assert!(Package::open(&fixture.0).is_err());
}
#[test]
fn refuses_hash_mismatch_and_narrow_or_wide_developer_paths() {
    let fixture = Fixture::new();
    fixture.candidate();
    fs::write(fixture.0.join("LICENSE"), b"changed").unwrap();
    assert!(Package::open(&fixture.0).is_err());
    let mut manifest = fixture.candidate();
    for path in [
        b"C:\\Users\\fixture\\source.rs".to_vec(),
        "C:\\Users\\fixture\\source.rs"
            .encode_utf16()
            .flat_map(u16::to_le_bytes)
            .collect(),
    ] {
        let mut image = synthetic_pe(Target::X64, false);
        image.extend(path);
        fixture.replace(&mut manifest, "x64/KeyrillSetup.exe", &image);
        assert!(Package::open(&fixture.0).is_err());
    }
    assert!(reject_developer_paths(b"c:\\users\\\0/home/\0/users/\0").is_ok());
}
