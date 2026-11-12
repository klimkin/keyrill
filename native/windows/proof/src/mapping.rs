//! Logon-scoped diagnostic mappings with a read-only seqlock reader.
use crate::native::{Handle, wide};
use crate::protocol::{
    Identity, PAGE_BYTES, PAYLOAD_WORDS, PROTOCOL, Payload, decode_payload, mapping_name,
};
use std::sync::atomic::{AtomicU32, Ordering, fence};
use windows::Win32::Foundation::*;
use windows::Win32::Security::Authorization::*;
use windows::Win32::Security::*;
use windows::Win32::System::Memory::*;
use windows::Win32::System::Threading::*;
use windows::core::{PCWSTR, PWSTR, Result};

const MAGIC: u32 = 0x4b525031;
#[repr(C)]
struct Page {
    header: [AtomicU32; 8],
    allocated: AtomicU32,
    claim: AtomicU32,
    sequence: AtomicU32,
    words: [AtomicU32; PAYLOAD_WORDS],
}
const _: () = assert!(std::mem::offset_of!(Page, words) == 44);
const _: () = assert!(std::mem::align_of::<Page>() == 4);
const _: () = assert!(std::mem::size_of::<Page>() <= PAGE_BYTES);
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Availability {
    Available,
    Missing,
    Busy,
    Incompatible,
    Stale,
}

fn expected_header(id: &Identity) -> [u32; 8] {
    [
        MAGIC,
        PROTOCOL,
        PAGE_BYTES as u32,
        PAYLOAD_WORDS as u32,
        id.process,
        id.started as u32,
        (id.started >> 32) as u32,
        id.thread,
    ]
}
fn matches(page: &Page, id: &Identity) -> bool {
    page.header
        .iter()
        .zip(expected_header(id))
        .all(|(actual, expected)| actual.load(Ordering::Acquire) == expected)
}
fn read(page: &Page, id: &Identity) -> (Availability, Payload) {
    for _ in 0..3 {
        if !matches(page, id) {
            return (Availability::Incompatible, [0; PAYLOAD_WORDS]);
        }
        let claim = page.claim.load(Ordering::Acquire);
        let before = page.sequence.load(Ordering::Acquire);
        fence(Ordering::SeqCst);
        if before >= u32::MAX - 2 {
            return (Availability::Incompatible, [0; PAYLOAD_WORDS]);
        }
        if claim == 0 || before == 0 {
            return (Availability::Missing, [0; PAYLOAD_WORDS]);
        }
        if before & 1 != 0 {
            continue;
        }
        let candidate = std::array::from_fn(|index| page.words[index].load(Ordering::Acquire));
        fence(Ordering::SeqCst);
        if before == page.sequence.load(Ordering::Acquire)
            && claim == page.claim.load(Ordering::Acquire)
            && matches(page, id)
            && candidate[0] == claim
        {
            return if decode_payload(&candidate).is_ok() {
                (Availability::Available, candidate)
            } else {
                (Availability::Incompatible, [0; PAYLOAD_WORDS])
            };
        }
    }
    (Availability::Busy, [0; PAYLOAD_WORDS])
}
struct View(MEMORY_MAPPED_VIEW_ADDRESS);
impl View {
    fn map(handle: HANDLE, access: FILE_MAP) -> Result<Self> {
        let view = unsafe { MapViewOfFile(handle, access, 0, 0, PAGE_BYTES) };
        if view.Value.is_null() {
            return Err(windows::core::Error::from_hresult(E_FAIL));
        }
        Ok(Self(view))
    }
    fn page(&self) -> &Page {
        unsafe { &*self.0.Value.cast::<Page>() }
    }
}
impl Drop for View {
    fn drop(&mut self) {
        unsafe {
            let _ = UnmapViewOfFile(self.0);
        }
    }
}
struct LocalAllocation(HLOCAL);
impl Drop for LocalAllocation {
    fn drop(&mut self) {
        unsafe {
            let _ = LocalFree(Some(self.0));
        }
    }
}

pub struct ArmedPage {
    view: View,
    _mapping: Handle,
    identity: Identity,
}
impl ArmedPage {
    pub fn create(identity: Identity) -> Result<Self> {
        let mut token = HANDLE::default();
        unsafe {
            OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token)?;
        }
        let token = Handle(token);
        let mut needed = 0;
        let _ = unsafe { GetTokenInformation(token.0, TokenLogonSid, None, 0, &mut needed) };
        if needed == 0 || needed > 65536 {
            return Err(windows::core::Error::from_hresult(E_FAIL));
        }
        let mut storage = vec![0usize; (needed as usize).div_ceil(std::mem::size_of::<usize>())];
        unsafe {
            GetTokenInformation(
                token.0,
                TokenLogonSid,
                Some(storage.as_mut_ptr().cast()),
                needed,
                &mut needed,
            )?;
        }
        let groups = unsafe { &*storage.as_ptr().cast::<TOKEN_GROUPS>() };
        if groups.GroupCount != 1 {
            return Err(windows::core::Error::from_hresult(E_FAIL));
        }
        let mut sid = PWSTR::null();
        unsafe {
            ConvertSidToStringSidW(groups.Groups[0].Sid, &mut sid)?;
        }
        let sid_owner = LocalAllocation(HLOCAL(sid.0.cast()));
        let descriptor = wide(&format!("D:P(A;;GRGW;;;{})", unsafe { sid.to_string()? }));
        drop(sid_owner);
        let mut security = PSECURITY_DESCRIPTOR::default();
        unsafe {
            ConvertStringSecurityDescriptorToSecurityDescriptorW(
                PCWSTR(descriptor.as_ptr()),
                SDDL_REVISION_1,
                &mut security,
                None,
            )?;
        }
        let security_owner = LocalAllocation(HLOCAL(security.0));
        let attributes = SECURITY_ATTRIBUTES {
            nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: security.0,
            bInheritHandle: false.into(),
        };
        let name = wide(&mapping_name(&identity));
        let mapping = Handle(unsafe {
            CreateFileMappingW(
                INVALID_HANDLE_VALUE,
                Some(&attributes),
                PAGE_READWRITE,
                0,
                PAGE_BYTES as u32,
                PCWSTR(name.as_ptr()),
            )?
        });
        let error = unsafe { GetLastError() };
        drop(security_owner);
        if error == ERROR_ALREADY_EXISTS {
            return Err(windows::core::Error::from_hresult(
                HRESULT_FROM_WIN32_ALREADY_EXISTS,
            ));
        }
        let writer = View::map(mapping.0, FILE_MAP_WRITE)?;
        let page = writer.page();
        for (word, value) in page.header.iter().zip(expected_header(&identity)) {
            word.store(value, Ordering::SeqCst);
        }
        page.allocated.store(0, Ordering::SeqCst);
        page.claim.store(0, Ordering::SeqCst);
        page.sequence.store(0, Ordering::SeqCst);
        for word in &page.words {
            word.store(0, Ordering::SeqCst);
        }
        drop(writer);
        let read_only =
            Handle(unsafe { OpenFileMappingW(FILE_MAP_READ.0, false, PCWSTR(name.as_ptr()))? });
        let view = View::map(read_only.0, FILE_MAP_READ)?;
        Ok(Self {
            view,
            _mapping: read_only,
            identity,
        })
    }
    pub fn read(&self) -> (Availability, Payload) {
        read(self.view.page(), &self.identity)
    }
}
const HRESULT_FROM_WIN32_ALREADY_EXISTS: windows::core::HRESULT =
    windows::core::HRESULT(0x800700b7u32 as i32);
pub fn existing(identity: &Identity) -> (Availability, Payload) {
    let name = wide(&mapping_name(identity));
    let Ok(handle) = (unsafe { OpenFileMappingW(FILE_MAP_READ.0, false, PCWSTR(name.as_ptr())) })
    else {
        return (Availability::Missing, [0; PAYLOAD_WORDS]);
    };
    let mapping = Handle(handle);
    let Ok(view) = View::map(mapping.0, FILE_MAP_READ) else {
        return (Availability::Incompatible, [0; PAYLOAD_WORDS]);
    };
    read(view.page(), identity)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn mapping_reader_checks_frozen_layout_and_unpublished_busy_stale_payloads() {
        let identity = Identity {
            process: 17,
            thread: 19,
            started: 23,
            ..Identity::default()
        };
        let page: Page = unsafe { std::mem::zeroed() };
        for (word, value) in page.header.iter().zip(expected_header(&identity)) {
            word.store(value, Ordering::SeqCst);
        }
        assert_eq!(read(&page, &identity).0, Availability::Missing);
        page.claim.store(1, Ordering::SeqCst);
        page.sequence.store(1, Ordering::SeqCst);
        assert_eq!(read(&page, &identity).0, Availability::Busy);
        page.sequence.store(2, Ordering::SeqCst);
        page.words[0].store(1, Ordering::SeqCst);
        assert_eq!(read(&page, &identity).0, Availability::Available);
        for index in 0..8 {
            let old = page.header[index].fetch_add(1, Ordering::SeqCst);
            assert_eq!(read(&page, &identity).0, Availability::Incompatible);
            page.header[index].store(old, Ordering::SeqCst);
        }
        page.words[511].store(1, Ordering::SeqCst);
        assert_eq!(read(&page, &identity).0, Availability::Incompatible);
        page.words[511].store(0, Ordering::SeqCst);
        page.sequence.store(u32::MAX - 1, Ordering::SeqCst);
        assert_eq!(read(&page, &identity).0, Availability::Incompatible);
    }
    #[test]
    fn real_logon_secured_mapping_rejects_collision_and_has_read_only_view() {
        let identity = Identity {
            process: unsafe { GetCurrentProcessId() },
            thread: unsafe { GetCurrentThreadId() },
            started: crate::native::started(unsafe { GetCurrentProcess() }, false).unwrap(),
            ..Identity::default()
        };
        let armed = ArmedPage::create(identity).unwrap();
        assert_eq!(armed.read().0, Availability::Missing);
        assert!(ArmedPage::create(identity).is_err());
        assert_eq!(existing(&identity).0, Availability::Missing);
    }
}
