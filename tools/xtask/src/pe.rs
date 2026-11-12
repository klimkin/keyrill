//! Read PE export names independently of a platform SDK or external executable.

use std::collections::BTreeSet;
use std::io::{self, ErrorKind};

type Result<T> = io::Result<T>;

fn invalid() -> io::Error {
    io::Error::new(ErrorKind::InvalidData, "invalid x64 PE export table")
}

fn bytes(image: &[u8], offset: usize, length: usize) -> Result<&[u8]> {
    image
        .get(offset..offset.checked_add(length).ok_or_else(invalid)?)
        .ok_or_else(invalid)
}

fn word(image: &[u8], offset: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(
        bytes(image, offset, 2)?.try_into().map_err(|_| invalid())?,
    ))
}

fn dword(image: &[u8], offset: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(
        bytes(image, offset, 4)?.try_into().map_err(|_| invalid())?,
    ))
}

struct Section {
    rva: u32,
    length: u32,
    raw: u32,
}

struct Image<'a> {
    bytes: &'a [u8],
    sections: Vec<Section>,
}

impl Image<'_> {
    fn offset(&self, rva: u32, length: usize) -> Result<usize> {
        let section = self
            .sections
            .iter()
            .find(|section| {
                rva.checked_sub(section.rva)
                    .is_some_and(|offset| offset < section.length)
            })
            .ok_or_else(invalid)?;
        let relative = rva - section.rva;
        if length > (section.length - relative) as usize {
            return Err(invalid());
        }
        let offset = section.raw.checked_add(relative).ok_or_else(invalid)? as usize;
        bytes(self.bytes, offset, length)?;
        Ok(offset)
    }

    fn name(&self, rva: u32) -> Result<String> {
        let offset = self.offset(rva, 1)?;
        let remaining = &self.bytes[offset..];
        let length = remaining
            .iter()
            .position(|&byte| byte == 0)
            .ok_or_else(invalid)?;
        self.offset(rva, length + 1)?;
        let value = &remaining[..length];
        if value.is_empty() || !value.is_ascii() {
            return Err(invalid());
        }
        Ok(std::str::from_utf8(value)
            .map_err(|_| invalid())?
            .to_owned())
    }
}

pub(crate) fn exports(image: &[u8]) -> Result<BTreeSet<String>> {
    if bytes(image, 0, 2)? != b"MZ" {
        return Err(invalid());
    }
    let signature = dword(image, 0x3c)? as usize;
    if bytes(image, signature, 4)? != b"PE\0\0" {
        return Err(invalid());
    }
    let header = signature.checked_add(4).ok_or_else(invalid)?;
    if word(image, header)? != 0x8664 {
        return Err(invalid());
    }
    let count = usize::from(word(image, header + 2)?);
    let optional_bytes = usize::from(word(image, header + 16)?);
    let optional = header + 20;
    if optional_bytes < 120 || word(image, optional)? != 0x20b || dword(image, optional + 108)? == 0
    {
        return Err(invalid());
    }
    bytes(image, optional, optional_bytes)?;
    let mut sections = Vec::new();
    let table = optional + optional_bytes;
    for index in 0..count {
        let offset = table
            .checked_add(index.checked_mul(40).ok_or_else(invalid)?)
            .ok_or_else(invalid)?;
        bytes(image, offset, 40)?;
        sections.push(Section {
            rva: dword(image, offset + 12)?,
            length: dword(image, offset + 16)?,
            raw: dword(image, offset + 20)?,
        });
    }
    let parsed = Image {
        bytes: image,
        sections,
    };
    let export_rva = dword(image, optional + 112)?;
    let export_bytes = dword(image, optional + 116)?;
    if export_rva == 0 || export_bytes < 40 {
        return Err(invalid());
    }
    let export = parsed.offset(export_rva, export_bytes as usize)?;
    let functions = dword(image, export + 20)? as usize;
    let names = dword(image, export + 24)? as usize;
    if names > functions || functions == 0 || names == 0 {
        return Err(invalid());
    }
    let pointers = parsed.offset(
        dword(image, export + 32)?,
        names.checked_mul(4).ok_or_else(invalid)?,
    )?;
    let ordinals = parsed.offset(
        dword(image, export + 36)?,
        names.checked_mul(2).ok_or_else(invalid)?,
    )?;
    let addresses = parsed.offset(
        dword(image, export + 28)?,
        functions.checked_mul(4).ok_or_else(invalid)?,
    )?;
    let mut result = BTreeSet::new();
    for index in 0..names {
        let ordinal = usize::from(word(image, ordinals + index * 2)?);
        if ordinal >= functions || dword(image, addresses + ordinal * 4)? == 0 {
            return Err(invalid());
        }
        let name = parsed.name(dword(image, pointers + index * 4)?)?;
        if !result.insert(name) {
            return Err(invalid());
        }
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn set16(image: &mut [u8], offset: usize, value: u16) {
        image[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }
    fn set32(image: &mut [u8], offset: usize, value: u32) {
        image[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    fn fixture() -> Vec<u8> {
        let mut image = vec![0; 1024];
        image[..2].copy_from_slice(b"MZ");
        set32(&mut image, 0x3c, 64);
        image[64..68].copy_from_slice(b"PE\0\0");
        set16(&mut image, 68, 0x8664);
        set16(&mut image, 70, 1);
        set16(&mut image, 84, 120);
        set16(&mut image, 88, 0x20b);
        set32(&mut image, 196, 1);
        set32(&mut image, 200, 0x1000);
        set32(&mut image, 204, 40);
        set32(&mut image, 220, 0x1000);
        set32(&mut image, 224, 512);
        set32(&mut image, 228, 512);
        set32(&mut image, 532, 1);
        set32(&mut image, 536, 1);
        set32(&mut image, 540, 0x1040);
        set32(&mut image, 544, 0x1044);
        set32(&mut image, 548, 0x1048);
        set32(&mut image, 576, 0x2000);
        set32(&mut image, 580, 0x1050);
        image[592..605].copy_from_slice(b"DllCanUnload\0");
        image
    }

    #[test]
    fn reads_literal_x64_exports_and_rejects_truncation() {
        let image = fixture();
        assert_eq!(
            exports(&image).unwrap(),
            BTreeSet::from(["DllCanUnload".to_owned()])
        );
        for length in 0..605 {
            assert!(exports(&image[..length]).is_err(), "truncation {length}");
        }
    }

    #[test]
    fn rejects_invalid_headers_offsets_and_name_tables() {
        for (offset, value) in [
            (0x3c, u32::MAX),
            (200, u32::MAX),
            (204, u32::MAX),
            (536, 2),
            (540, u32::MAX),
            (544, u32::MAX),
            (548, u32::MAX),
            (576, 0),
            (580, 0),
            (584, 1),
        ] {
            let mut image = fixture();
            set32(&mut image, offset, value);
            assert!(exports(&image).is_err(), "offset {offset}");
        }
        let mut image = fixture();
        set16(&mut image, 68, 0xaa64);
        assert!(exports(&image).is_err());
    }

    #[test]
    #[ignore = "requires explicit production and test-api DLL artifacts"]
    fn compiled_dll_feature_exports_are_separate() {
        for (variable, private) in [
            ("KEYRILL_PRODUCTION_DLL", false),
            ("KEYRILL_TEST_DLL", true),
        ] {
            let path = std::env::var_os(variable).expect("explicit DLL path required");
            let image = std::fs::read(path).unwrap();
            let names = exports(&image).unwrap();
            assert!(names.contains("DllCanUnloadNow"));
            assert!(names.contains("DllGetClassObject"));
            assert_eq!(
                names.iter().any(|name| name.starts_with("KeyrillTest")),
                private
            );
        }
    }
}
