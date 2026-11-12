//! Independent diagnostic COM schema and canonical fixture publications.
use crate::protocol::{valid_lifecycle, valid_service};
use std::ffi::c_void;
use windows::Win32::Foundation::*;
use windows::Win32::UI::TextServices::*;
use windows::core::{GUID, HRESULT, IUnknown, IUnknown_Vtbl, Interface};

pub const E_PENDING: HRESULT = HRESULT(0x8000000au32 as i32);
pub const SERVICE_ID: GUID = GUID::from_u128(0x1a746212_84db_4acf_88b3_caaa26352774);
pub const SNAPSHOT_IID: GUID = GUID::from_u128(0x4f89429e_a955_4885_bb33_c78ad1982481);
pub const RUNTIME_IID: GUID = GUID::from_u128(0xcda9ed25_1dbc_4fb6_b2a5_bc132945e082);
pub const STAGES: [&str; 26] = [
    "Test",
    "Key",
    "Keyboard",
    "Profile",
    "Layout",
    "Mode",
    "Handle",
    "Request",
    "Session",
    "Selection",
    "Focus",
    "Context",
    "ProofWindow",
    "Status",
    "ContextDisabled",
    "EmptyContext",
    "ThreadDisabled",
    "AppProperty",
    "AppValue",
    "ScopeInterface",
    "Scopes",
    "Foreign",
    "Boundary",
    "Plan",
    "Apply",
    "Acknowledge",
];
pub const CAUSES: [&str; 15] = [
    "None",
    "Native",
    "Predicate",
    "Inactive",
    "Stopped",
    "ModeUnset",
    "Reentry",
    "Stale",
    "Missing",
    "WrongType",
    "WrongService",
    "NotActive",
    "WrongProfile",
    "Substitute",
    "Exception",
];
pub const COUNTERS: [&str; 12] = [
    "TestEntered",
    "Interested",
    "KeyEntered",
    "Consumed",
    "HandleEntered",
    "Requested",
    "SessionEntered",
    "SessionCompleted",
    "Attempted",
    "Verified",
    "Refused",
    "Unknown",
];
pub const AVAILABILITY: [&str; 6] = [
    "Missing",
    "Error",
    "Inactive",
    "Busy",
    "Available",
    "UnsupportedVersion",
];
pub const CONTINUITY: [&str; 4] = ["None", "Reset", "Same", "Indeterminate"];
#[repr(C)]
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ServiceSnapshot {
    pub base: [u32; 55],
    pub lifecycle: [u32; 86],
}
impl Default for ServiceSnapshot {
    fn default() -> Self {
        Self {
            base: [0; 55],
            lifecycle: [0; 86],
        }
    }
}
const _: () = assert!(std::mem::size_of::<ServiceSnapshot>() == 564);
const _: () = assert!(std::mem::offset_of!(ServiceSnapshot, lifecycle) == 220);
impl ServiceSnapshot {
    pub fn valid(&self) -> bool {
        valid_service(&self.base) && valid_lifecycle(&self.lifecycle)
    }
    pub fn saturated(&self) -> bool {
        self.base[4] != 0 || self.lifecycle[2] != 0
    }
    pub fn events(&self) -> impl Iterator<Item = &[u32]> {
        self.lifecycle[6..6 + self.lifecycle[3].min(16) as usize * 5].chunks_exact(5)
    }
}
#[repr(C)]
struct ReaderVtable {
    base: IUnknown_Vtbl,
    copy: unsafe extern "system" fn(*mut c_void, u32, u32, *mut c_void) -> HRESULT,
}
pub struct Endpoint {
    pub identity: IUnknown,
    reader: IUnknown,
}
impl Endpoint {
    pub fn discover(manager: &ITfThreadMgr) -> Result<Self, HRESULT> {
        Self::discover_with(manager, || true)
    }
    pub fn discover_with(
        manager: &ITfThreadMgr,
        alive: impl Fn() -> bool,
    ) -> Result<Self, HRESULT> {
        let mut raw = std::ptr::null_mut();
        let result = unsafe {
            (manager.vtable().GetFunctionProvider)(manager.as_raw(), &SERVICE_ID, &mut raw)
        };
        let provider = take_interface::<ITfFunctionProvider>(result, raw)?;
        if !alive() {
            return Err(E_ABORT);
        }
        let identity: IUnknown = query_exact(&provider)?;
        if !alive() {
            return Err(E_ABORT);
        }
        let mut raw = std::ptr::null_mut();
        let result = unsafe {
            (provider.vtable().GetFunction)(
                provider.as_raw(),
                &GUID::zeroed(),
                &SNAPSHOT_IID,
                &mut raw,
            )
        };
        let function = take_interface::<IUnknown>(result, raw)?;
        if !alive() {
            return Err(E_ABORT);
        }
        let mut raw = std::ptr::null_mut();
        let result = unsafe {
            (function.vtable().QueryInterface)(function.as_raw(), &SNAPSHOT_IID, &mut raw)
        };
        let reader = take_interface::<IUnknown>(result, raw)?;
        if !alive() {
            return Err(E_ABORT);
        }
        let reader_identity: IUnknown = query_exact(&reader)?;
        if !alive() {
            return Err(E_ABORT);
        }
        if identity != reader_identity {
            return Err(E_UNEXPECTED);
        }
        Ok(Self { identity, reader })
    }
    pub fn same_provider(&self, manager: &ITfThreadMgr) -> Result<bool, HRESULT> {
        self.same_provider_with(manager, || true)
    }
    pub fn same_provider_with(
        &self,
        manager: &ITfThreadMgr,
        alive: impl Fn() -> bool,
    ) -> Result<bool, HRESULT> {
        let mut raw = std::ptr::null_mut();
        let result = unsafe {
            (manager.vtable().GetFunctionProvider)(manager.as_raw(), &SERVICE_ID, &mut raw)
        };
        let provider = take_interface::<ITfFunctionProvider>(result, raw)?;
        if !alive() {
            return Err(E_ABORT);
        }
        let identity: IUnknown = query_exact(&provider)?;
        if !alive() {
            return Err(E_ABORT);
        }
        Ok(identity == self.identity)
    }
    pub fn copy(&self) -> Result<ServiceSnapshot, HRESULT> {
        let mut output = ServiceSnapshot::default();
        let vtable = unsafe { &**self.reader.as_raw().cast::<*const ReaderVtable>() };
        let result = unsafe {
            (vtable.copy)(
                self.reader.as_raw(),
                std::mem::size_of::<ServiceSnapshot>() as u32,
                2,
                (&mut output as *mut ServiceSnapshot).cast(),
            )
        };
        if result != S_OK {
            return Err(result);
        }
        if !output.valid() {
            return Err(E_UNEXPECTED);
        }
        Ok(output)
    }
}
fn query_exact<T: Interface>(object: &impl Interface) -> Result<T, HRESULT> {
    let mut raw = std::ptr::null_mut();
    let result = unsafe { object.query(&T::IID, &mut raw) };
    take_interface(result, raw)
}
fn take_interface<T: Interface>(result: HRESULT, raw: *mut c_void) -> Result<T, HRESULT> {
    if raw.is_null() {
        return Err(if result == S_OK {
            E_NOINTERFACE
        } else {
            result
        });
    }
    let value = unsafe { T::from_raw(raw) };
    if result == S_OK {
        Ok(value)
    } else {
        Err(result)
    }
}
#[repr(C, align(8))]
struct Runtime([u32; 12]);
pub fn read_runtime(manager: &ITfThreadMgr) -> Result<[u32; 12], HRESULT> {
    let mut raw = std::ptr::null_mut();
    let result =
        unsafe { (manager.vtable().GetFunctionProvider)(manager.as_raw(), &SERVICE_ID, &mut raw) };
    let provider = take_interface::<ITfFunctionProvider>(result, raw)?;
    let mut raw = std::ptr::null_mut();
    let result = unsafe {
        (provider.vtable().GetFunction)(provider.as_raw(), &GUID::zeroed(), &RUNTIME_IID, &mut raw)
    };
    let function = take_interface::<IUnknown>(result, raw)?;
    let mut raw = std::ptr::null_mut();
    let result =
        unsafe { (function.vtable().QueryInterface)(function.as_raw(), &RUNTIME_IID, &mut raw) };
    let reader = take_interface::<IUnknown>(result, raw)?;
    let vtable = unsafe { &**reader.as_raw().cast::<*const ReaderVtable>() };
    let mut value = Runtime([0; 12]);
    let result =
        unsafe { (vtable.copy)(reader.as_raw(), 48, 1, (&mut value as *mut Runtime).cast()) };
    if result == S_OK {
        Ok(value.0)
    } else {
        Err(result)
    }
}
pub fn format_runtime(value: &[u32; 12]) -> String {
    format!(
        "Keyrill runtime v1; activation={}; requested={}; effective={}; model_ready={}; revision={}; error={}; configuration_error={}; en_state={}; en_error={}; ru_state={}; ru_error={}",
        value[0],
        value[1],
        value[2],
        value[3],
        u64::from(value[4]) | (u64::from(value[5]) << 32),
        value[6],
        value[7],
        value[8],
        value[9],
        value[10],
        value[11]
    )
}
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Publication {
    pub publication: u32,
    pub saturated: u32,
    pub source: u32,
    pub availability: u32,
    pub continuity: u32,
    pub result: u32,
    pub snapshot: ServiceSnapshot,
}
impl Publication {
    pub fn valid(&self) -> bool {
        if self.availability >= 6
            || self.continuity >= 4
            || self.saturated > 1
            || ((self.publication == u32::MAX || self.source == u32::MAX) && self.saturated == 0)
            || !self.snapshot.valid()
            || (self.availability == 5 && self.result != E_NOINTERFACE.0 as u32)
        {
            return false;
        }
        match self.availability {
            4 => {
                self.snapshot.base[1] != 0
                    && self.result == 0
                    && self.source != 0
                    && self.continuity != 0
                    && ((self.saturated != 0 || self.snapshot.saturated())
                        == (self.continuity == 3))
            }
            2 => self.snapshot.base[1] == 0 && self.result == 0 && self.continuity == 0,
            _ => self.continuity == 0 && self.snapshot == ServiceSnapshot::default(),
        }
    }
    pub fn format(&self) -> Option<String> {
        if !self.valid() {
            return None;
        }
        let mut result = format!(
            "Keyrill service v2; state={}; continuity={}",
            AVAILABILITY[self.availability as usize], CONTINUITY[self.continuity as usize]
        );
        let header = [
            "publication",
            "host_saturated",
            "source",
            "result",
            "activation",
            "active",
            "stopped",
            "mode",
            "saturated",
            "request_present",
            "request_result",
            "session_present",
            "session_result",
        ];
        let values = [self.publication, self.saturated, self.source, self.result]
            .into_iter()
            .chain(self.snapshot.base[..9].iter().copied());
        for (label, value) in header.into_iter().zip(values) {
            result.push_str(&format!("; {label}={value}"));
        }
        for (label, value) in COUNTERS
            .into_iter()
            .chain(STAGES)
            .zip(self.snapshot.base[9..47].iter())
        {
            result.push_str(&format!("; {label}={value}"));
        }
        for (label, refusal) in ["first", "latest"]
            .into_iter()
            .zip(self.snapshot.base[47..].chunks_exact(4))
        {
            result.push_str(&format!(
                "; {label}={}/{}/{}/{}",
                refusal[0], STAGES[refusal[1] as usize], CAUSES[refusal[2] as usize], refusal[3]
            ));
        }
        for (label, value) in [
            "event_sequence",
            "mark_allocated",
            "lifecycle_saturated",
            "event_count",
            "owned_present",
            "owned_serial",
        ]
        .into_iter()
        .zip(self.snapshot.lifecycle)
        {
            result.push_str(&format!("; {label}={value}"));
        }
        for event in self.snapshot.lifecycle[6..].chunks_exact(5) {
            result.push_str(&format!(
                "; event={}/{}/{}/{}/{}",
                event[0], event[1], event[2], event[3], event[4]
            ));
        }
        Some(result)
    }
    pub fn parse(text: &str) -> Option<Self> {
        if text.len() >= 8192 {
            return None;
        }
        let fields: Vec<_> = text.split("; ").collect();
        if fields.len() != 78 || fields[0] != "Keyrill service v2" {
            return None;
        }
        let mut result = Self::default();
        result.availability = AVAILABILITY
            .iter()
            .position(|label| fields[1].strip_prefix("state=") == Some(label))?
            as u32;
        result.continuity = CONTINUITY
            .iter()
            .position(|label| fields[2].strip_prefix("continuity=") == Some(label))?
            as u32;
        let values: Option<Vec<u32>> = fields[3..54]
            .iter()
            .map(|field| field.split_once('=')?.1.parse().ok())
            .collect();
        let values = values?;
        result.publication = values[0];
        result.saturated = values[1];
        result.source = values[2];
        result.result = values[3];
        result.snapshot.base[..47].copy_from_slice(&values[4..]);
        for (index, field) in fields[54..56].iter().enumerate() {
            let parts: Vec<_> = field.split_once('=')?.1.split('/').collect();
            if parts.len() != 4 {
                return None;
            }
            result.snapshot.base[47 + index * 4..51 + index * 4].copy_from_slice(&[
                parts[0].parse().ok()?,
                STAGES.iter().position(|&label| label == parts[1])? as u32,
                CAUSES.iter().position(|&label| label == parts[2])? as u32,
                parts[3].parse().ok()?,
            ]);
        }
        for (index, field) in fields[56..62].iter().enumerate() {
            result.snapshot.lifecycle[index] = field.split_once('=')?.1.parse().ok()?;
        }
        for (index, field) in fields[62..].iter().enumerate() {
            let parts: Option<Vec<u32>> = field
                .split_once('=')?
                .1
                .split('/')
                .map(|part| part.parse().ok())
                .collect();
            let parts = parts?;
            if parts.len() != 5 {
                return None;
            }
            result.snapshot.lifecycle[6 + index * 5..11 + index * 5].copy_from_slice(&parts);
        }
        (result.format().as_deref() == Some(text)).then_some(result)
    }
}
pub fn comparable(before: &Publication, after: &Publication) -> bool {
    if !before.valid()
        || !after.valid()
        || before.availability != 4
        || after.availability != 4
        || before.publication == 0
        || after.publication <= before.publication
        || before.source != after.source
        || before.snapshot.base[0] == 0
        || before.snapshot.base[0] != after.snapshot.base[0]
        || after.continuity != 2
        || before.saturated != 0
        || after.saturated != 0
        || before.snapshot.saturated()
        || after.snapshot.saturated()
    {
        return false;
    }
    history(&before.snapshot, &after.snapshot)
}
pub fn history(before: &ServiceSnapshot, after: &ServiceSnapshot) -> bool {
    let a = &before.lifecycle;
    let b = &after.lifecycle;
    if b[0] < a[0] || b[1] < a[1] || b[0] - a[0] > b[3] {
        return false;
    }
    let oldest = b[0] - b[3];
    before
        .events()
        .filter(|event| event[0] > oldest)
        .all(|event| {
            let offset = 6 + (event[0] - oldest - 1) as usize * 5;
            event == &b[offset..offset + 5]
        })
}
#[derive(Default)]
pub struct Poller {
    pub value: Publication,
    identity: Option<IUnknown>,
    activation: u32,
}
impl Poller {
    pub fn poll(&mut self, manager: &ITfThreadMgr) {
        self.value.publication = self.value.publication.saturating_add(1);
        self.value.saturated |= u32::from(self.value.publication == u32::MAX);
        self.value.snapshot = ServiceSnapshot::default();
        self.value.continuity = 0;
        match Endpoint::discover(manager)
            .and_then(|endpoint| endpoint.copy().map(|snapshot| (endpoint, snapshot)))
        {
            Ok((endpoint, snapshot)) => {
                self.value.result = 0;
                self.value.snapshot = snapshot;
                if self.value.snapshot.base[1] == 0 {
                    self.value.availability = 2;
                    self.identity = None;
                    self.activation = 0;
                    return;
                }
                let same = self.identity.as_ref() == Some(&endpoint.identity)
                    && self.activation == self.value.snapshot.base[0];
                if !same {
                    self.value.source = self.value.source.saturating_add(1);
                    self.value.saturated |= u32::from(self.value.source == u32::MAX);
                }
                self.value.availability = 4;
                self.value.continuity =
                    if self.value.saturated != 0 || self.value.snapshot.saturated() {
                        3
                    } else if same {
                        2
                    } else {
                        1
                    };
                self.activation = self.value.snapshot.base[0];
                self.identity = Some(endpoint.identity);
            }
            Err(error) => {
                self.value.result = error.0 as u32;
                self.value.availability = if error == TF_E_NOPROVIDER {
                    0
                } else if error == E_PENDING {
                    3
                } else if error == E_NOINTERFACE {
                    5
                } else {
                    1
                };
                self.identity = None;
                self.activation = 0;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn canonical_service_publication_roundtrip_and_negative_fields() {
        let value = Publication::default();
        let text = value.format().unwrap();
        assert_eq!(Publication::parse(&text), Some(value));
        assert!(Publication::parse(&(text.clone() + " ")).is_none());
        assert!(Publication::parse(&text.replace("active=0", "active=2")).is_none());
        assert!(Publication::parse(&text.replace("publication=0", "publication=00")).is_none());
    }
}
