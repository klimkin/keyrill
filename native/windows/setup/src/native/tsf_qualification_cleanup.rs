use super::*;

struct PreferencePaths {
    paths: [String; 4],
}

impl PreferencePaths {
    fn from_root(root: String, profile: GUID) -> Self {
        let profiles = format!("{root}\\LanguageProfile");
        let language = format!("{profiles}\\0x00000409");
        let leaf = format!("{language}\\{{{profile:?}}}");
        Self {
            paths: [root, profiles, language, leaf],
        }
    }

    fn remove(&self, _enable: Option<bool>) -> Result<()> {
        for view in [KEY_WOW64_64KEY, KEY_WOW64_32KEY] {
            if registry::exists(HKEY_CURRENT_USER, view, &self.paths[0])? {
                return Err("disposable preference cleanup incomplete".into());
            }
        }
        Ok(())
    }
}

struct Isolated {
    preference: PreferencePaths,
    sentinel: String,
}

impl Isolated {
    fn new() -> Result<Self> {
        // SAFETY: GUID allocation creates no registration or platform editing state.
        let (root, profile) = unsafe { (CoCreateGuid()?, CoCreateGuid()?) };
        let enclosing = format!("Software\\KeyrillTests\\Qualification-{root:?}");
        let preference = PreferencePaths::from_root(format!("{enclosing}\\Fixture"), profile);
        let sentinel = format!("{enclosing}\\Sentinel");
        for view in [KEY_WOW64_64KEY, KEY_WOW64_32KEY] {
            if registry::exists(HKEY_CURRENT_USER, view, &enclosing)? {
                return Err("isolated qualification test identity already exists".into());
            }
        }
        registry::write(
            HKEY_CURRENT_USER,
            KEY_WOW64_64KEY,
            &sentinel,
            "unchanged",
            &registry::dword(42),
        )?;
        Ok(Self {
            preference,
            sentinel,
        })
    }

    fn finish(self) {
        for view in [KEY_WOW64_64KEY, KEY_WOW64_32KEY] {
            registry::write(
                HKEY_CURRENT_USER,
                view,
                &self.preference.paths[3],
                "Enable",
                &Value::Null,
            )
            .unwrap();
            for path in self.preference.paths.iter().rev() {
                registry::remove_empty_key(HKEY_CURRENT_USER, view, path).unwrap();
            }
            registry::write(
                HKEY_CURRENT_USER,
                view,
                &self.sentinel,
                "unchanged",
                &Value::Null,
            )
            .unwrap();
            registry::remove_empty_key(HKEY_CURRENT_USER, view, &self.sentinel).unwrap();
            let enclosing = self.sentinel.rsplit_once('\\').unwrap().0;
            registry::remove_empty_key(HKEY_CURRENT_USER, view, enclosing).unwrap();
        }
    }
}

#[test]
fn isolated_native_preference_cleanup_requires_complete_absence_for_both_dwords() {
    for enable in [false, true] {
        let fixture = Isolated::new().unwrap();
        for view in [KEY_WOW64_64KEY, KEY_WOW64_32KEY] {
            registry::write(
                HKEY_CURRENT_USER,
                view,
                &fixture.preference.paths[3],
                "Enable",
                &registry::dword(u32::from(enable)),
            )
            .unwrap();
        }
        let root = fixture.preference.paths[0].clone();
        let outcome = fixture.preference.remove(Some(enable));
        for view in [KEY_WOW64_64KEY, KEY_WOW64_32KEY] {
            if outcome.is_ok() {
                assert!(!registry::exists(HKEY_CURRENT_USER, view, &root).unwrap());
            }
            assert_eq!(
                registry::read(HKEY_CURRENT_USER, view, &fixture.sentinel, "unchanged").unwrap(),
                registry::dword(42)
            );
        }
        fixture.finish();
        outcome.unwrap();
    }
}
