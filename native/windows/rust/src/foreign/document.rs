//! Typed document queries and effects. Values own memory, while callers retain editing authority.

use super::{
    Call, Dependencies, InterfaceError, Lease, Returned, quarantine, query, returned_interface,
};
use crate::field_contract::{Facts, HostIdentity};
use std::ffi::c_void;
use std::mem::ManuallyDrop;
use windows::Win32::Foundation::{HWND, S_OK};
use windows::Win32::System::Com::CoTaskMemFree;
use windows::Win32::System::Variant::{VARIANT, VT_EMPTY, VT_I4, VT_UNKNOWN};
use windows::Win32::UI::TextServices::*;
use windows_core::{BOOL, GUID, HRESULT, Interface};

unsafe extern "C" {
    fn kr_foreign_focus(manager: *mut c_void, output: *mut *mut c_void) -> Call;
    fn kr_foreign_top(document: *mut c_void, output: *mut *mut c_void) -> Call;
    fn kr_foreign_view(context: *mut c_void, output: *mut *mut c_void) -> Call;
    fn kr_foreign_window(view: *mut c_void, output: *mut HWND) -> Call;
    fn kr_foreign_status(context: *mut c_void, output: *mut TS_STATUS) -> Call;
    fn kr_foreign_compartment(
        manager: *mut c_void,
        id: *const GUID,
        output: *mut *mut c_void,
    ) -> Call;
    fn kr_foreign_property(context: *mut c_void, id: *const GUID, output: *mut *mut c_void)
    -> Call;
    fn kr_foreign_whitespace(unit: u16, output: *mut BOOL) -> Call;
    fn kr_foreign_property_value(
        property: *mut c_void,
        cookie: u32,
        range: *mut c_void,
        output: *mut VARIANT,
    ) -> Call;
    fn kr_foreign_compartment_value(compartment: *mut c_void, output: *mut VARIANT) -> Call;
    fn kr_foreign_variant_clear(value: *mut VARIANT) -> Call;
    fn kr_foreign_scopes(scope: *mut c_void, output: *mut *mut InputScope, count: *mut u32)
    -> Call;
    fn kr_foreign_range_context(range: *mut c_void, output: *mut *mut c_void) -> Call;
    fn kr_foreign_extent(range: *mut c_void, start: *mut i32, length: *mut i32) -> Call;
    fn kr_foreign_selection(
        context: *mut c_void,
        cookie: u32,
        output: *mut TF_SELECTION,
        fetched: *mut u32,
    ) -> Call;
    fn kr_foreign_set_selection(
        context: *mut c_void,
        cookie: u32,
        selection: *const TF_SELECTION,
    ) -> Call;
    fn kr_foreign_clone(range: *mut c_void, output: *mut *mut c_void) -> Call;
    fn kr_foreign_shift_start(
        range: *mut c_void,
        cookie: u32,
        amount: i32,
        shifted: *mut i32,
    ) -> Call;
    fn kr_foreign_shift_end(
        range: *mut c_void,
        cookie: u32,
        amount: i32,
        shifted: *mut i32,
    ) -> Call;
    fn kr_foreign_collapse(range: *mut c_void, cookie: u32, anchor: TfAnchor) -> Call;
    fn kr_foreign_compositions(context: *mut c_void, output: *mut *mut c_void) -> Call;
    fn kr_foreign_next(entries: *mut c_void, output: *mut *mut c_void, fetched: *mut u32) -> Call;
    fn kr_foreign_text(
        range: *mut c_void,
        cookie: u32,
        output: *mut u16,
        capacity: u32,
        count: *mut u32,
    ) -> Call;
    fn kr_foreign_set_text(range: *mut c_void, cookie: u32, text: *const u16, length: i32) -> Call;
    fn kr_foreign_start_composition(
        context: *mut c_void,
        cookie: u32,
        range: *mut c_void,
        sink: *mut c_void,
        output: *mut *mut c_void,
    ) -> Call;
    fn kr_foreign_composition_range(composition: *mut c_void, output: *mut *mut c_void) -> Call;
    fn kr_foreign_end_composition(composition: *mut c_void, cookie: u32) -> Call;
    fn kr_foreign_field(
        capture: Option<
            unsafe extern "system" fn(
                *mut c_void,
                HWND,
                *const HostIdentity,
                *mut Facts,
            ) -> HRESULT,
        >,
        context: *mut c_void,
        window: HWND,
        host: *const HostIdentity,
        output: *mut Facts,
    ) -> Call;
}

fn pointer<T: Interface>(lease: &Lease<T>) -> *mut c_void {
    lease.owner.pointer.as_ptr()
}

fn interface<T: Interface, U: Interface>(
    owner: &Lease<T>,
    call: unsafe extern "C" fn(*mut c_void, *mut *mut c_void) -> Call,
) -> Returned<Option<Lease<U>>> {
    let mut output = std::ptr::null_mut();
    // SAFETY: owner retains its typed SDK object and the C++ function fences its direct host call.
    let result = unsafe { call(pointer(owner), &mut output) };
    returned_interface(result, output)
}

pub(crate) fn focus(manager: &Lease<ITfThreadMgr>) -> Returned<Option<Lease<ITfDocumentMgr>>> {
    interface(manager, kr_foreign_focus)
}
pub(crate) fn top(document: &Lease<ITfDocumentMgr>) -> Returned<Option<Lease<ITfContext>>> {
    interface(document, kr_foreign_top)
}
pub(crate) fn view(context: &Lease<ITfContext>) -> Returned<Option<Lease<ITfContextView>>> {
    interface(context, kr_foreign_view)
}
pub(crate) fn property(
    context: &Lease<ITfContext>,
) -> Returned<Option<Lease<ITfReadOnlyProperty>>> {
    let mut output = std::ptr::null_mut();
    // SAFETY: context is retained, the generated SDK GUID is immutable, and output is writable.
    let call = unsafe { kr_foreign_property(pointer(context), &GUID_PROP_INPUTSCOPE, &mut output) };
    returned_interface(call, output)
}

pub(crate) fn whitespace(unit: u16) -> Returned<bool> {
    let mut output = BOOL(0);
    // SAFETY: UCRT classifies one scalar UTF-16 unit and writes a distinct initialized BOOL.
    let call = unsafe { kr_foreign_whitespace(unit, &mut output) };
    Returned {
        call,
        value: output.as_bool(),
    }
}
pub(crate) fn range_context(range: &Lease<ITfRange>) -> Returned<Option<Lease<ITfContext>>> {
    interface(range, kr_foreign_range_context)
}
pub(crate) fn clone_range(range: &Lease<ITfRange>) -> Returned<Option<Lease<ITfRange>>> {
    interface(range, kr_foreign_clone)
}
pub(crate) fn compositions(
    context: &Lease<ITfContextComposition>,
) -> Returned<Option<Lease<IEnumITfCompositionView>>> {
    interface(context, kr_foreign_compositions)
}
pub(crate) fn composition_range(
    composition: &Lease<ITfComposition>,
) -> Returned<Option<Lease<ITfRange>>> {
    interface(composition, kr_foreign_composition_range)
}

pub(crate) fn window(view: &Lease<ITfContextView>) -> Returned<HWND> {
    let mut output = HWND::default();
    // SAFETY: view is retained and output is distinct initialized HWND storage.
    let call = unsafe { kr_foreign_window(pointer(view), &mut output) };
    Returned {
        call,
        value: output,
    }
}

pub(crate) fn status(context: &Lease<ITfContext>) -> Returned<TS_STATUS> {
    let mut output = TS_STATUS::default();
    // SAFETY: context is retained and output is a complete initialized SDK status structure.
    let call = unsafe { kr_foreign_status(pointer(context), &mut output) };
    Returned {
        call,
        value: output,
    }
}

pub(crate) fn compartment(
    manager: &Lease<ITfCompartmentMgr>,
    id: &GUID,
) -> Returned<Option<Lease<ITfCompartment>>> {
    let mut output = std::ptr::null_mut();
    // SAFETY: manager and id are live, and output is distinct writable storage.
    let call = unsafe { kr_foreign_compartment(pointer(manager), id, &mut output) };
    returned_interface(call, output)
}

pub(crate) struct Value {
    raw: VARIANT,
    normally_returned: bool,
}

impl Value {
    fn new() -> Self {
        Self {
            raw: VARIANT::default(),
            normally_returned: false,
        }
    }
    pub(crate) fn empty(&self) -> bool {
        // SAFETY: VARIANT is initialized before every host call, and vt is always a scalar field.
        unsafe { self.raw.Anonymous.Anonymous.vt == VT_EMPTY }
    }
    pub(crate) fn zero_integer(&self) -> bool {
        // SAFETY: lVal is read only when the initialized discriminator identifies its active field.
        unsafe {
            self.raw.Anonymous.Anonymous.vt == VT_I4
                && self.raw.Anonymous.Anonymous.Anonymous.lVal == 0
        }
    }
    pub(crate) fn unknown(&self) -> bool {
        // SAFETY: vt is an initialized scalar discriminator.
        unsafe { self.raw.Anonymous.Anonymous.vt == VT_UNKNOWN }
    }
    pub(crate) fn has_unknown(&self) -> bool {
        if !self.unknown() {
            return false;
        }
        // SAFETY: VT_UNKNOWN identifies this initialized interface slot.
        unsafe { self.raw.Anonymous.Anonymous.Anonymous.punkVal.is_some() }
    }
    pub(crate) fn input_scope(&self) -> Result<Lease<ITfInputScope>, InterfaceError> {
        if !self.unknown() {
            return Err(InterfaceError::Missing);
        }
        // SAFETY: VT_UNKNOWN identifies a borrowed interface slot kept alive by this VARIANT.
        let unknown = unsafe { &self.raw.Anonymous.Anonymous.Anonymous.punkVal };
        let raw = unknown
            .as_ref()
            .map_or(std::ptr::null_mut(), Interface::as_raw);
        if raw.is_null() {
            return Err(InterfaceError::Missing);
        }
        // SAFETY: the variant retains the live SDK interface for the complete fenced QI.
        unsafe { query(raw) }
    }
    pub(crate) fn clear(mut self) -> Call {
        self.release()
    }
    fn release(&mut self) -> Call {
        if !self.normally_returned {
            return Call {
                result: S_OK,
                exception: 0,
            };
        }
        self.normally_returned = false;
        // SAFETY: a normally completed typed GetValue transfers the initialized VARIANT to caller cleanup.
        let call = unsafe { kr_foreign_variant_clear(&mut self.raw) };
        if call.exception != 0 {
            quarantine();
        }
        call
    }
}

impl Drop for Value {
    fn drop(&mut self) {
        let _ = self.release();
    }
}

pub(crate) fn compartment_value(compartment: &Lease<ITfCompartment>) -> Returned<Value> {
    let mut value = Value::new();
    // SAFETY: compartment is retained and the initialized VARIANT is complete writable output storage.
    let call = unsafe { kr_foreign_compartment_value(pointer(compartment), &mut value.raw) };
    value.normally_returned = call.exception == 0;
    if call.exception != 0 {
        quarantine();
    }
    Returned { call, value }
}

pub(crate) fn property_value(
    property: &Lease<ITfReadOnlyProperty>,
    cookie: u32,
    range: &Lease<ITfRange>,
) -> Returned<Value> {
    let mut value = Value::new();
    // SAFETY: property/range are retained for this cookie, with initialized distinct VARIANT output.
    let call = unsafe {
        kr_foreign_property_value(pointer(property), cookie, pointer(range), &mut value.raw)
    };
    value.normally_returned = call.exception == 0;
    if call.exception != 0 {
        quarantine();
    }
    Returned { call, value }
}

pub(crate) struct Scopes {
    pointer: *mut InputScope,
    pub(crate) count: u32,
}
impl Scopes {
    pub(crate) fn single(&self) -> Option<InputScope> {
        if self.count != 1 || self.pointer.is_null() {
            return None;
        }
        // SAFETY: count one from the normally returned SDK allocation permits exactly one scalar read.
        Some(unsafe { *self.pointer })
    }

    pub(crate) fn ordinary(&self) -> bool {
        self.single()
            .is_some_and(|scope| scope == IS_DEFAULT || scope == IS_TEXT)
    }
}
impl Drop for Scopes {
    fn drop(&mut self) {
        // SAFETY: GetInputScopes transfers this normally returned task-allocator buffer to the caller.
        unsafe { CoTaskMemFree(Some(self.pointer.cast())) };
    }
}
pub(crate) fn scopes(scope: &Lease<ITfInputScope>) -> Returned<Scopes> {
    let mut output = std::ptr::null_mut();
    let mut count = 0;
    // SAFETY: scope is retained and outputs are distinct writable pointer/count slots.
    let call = unsafe { kr_foreign_scopes(pointer(scope), &mut output, &mut count) };
    if call.exception != 0 {
        quarantine();
        output = std::ptr::null_mut();
        count = 0;
    }
    Returned {
        call,
        value: Scopes {
            pointer: output,
            count,
        },
    }
}

pub(crate) struct Selection {
    pub(crate) range: Option<Lease<ITfRange>>,
    pub(crate) style: TF_SELECTIONSTYLE,
    pub(crate) fetched: u32,
}
pub(crate) fn selection(context: &Lease<ITfContext>, cookie: u32) -> Returned<Selection> {
    let mut selection = TF_SELECTION::default();
    let mut fetched = 0;
    // SAFETY: context is retained, and the complete SDK structure/count are initialized writable outputs.
    let call =
        unsafe { kr_foreign_selection(pointer(context), cookie, &mut selection, &mut fetched) };
    let raw = selection
        .range
        .as_ref()
        .map_or(std::ptr::null_mut(), Interface::as_raw);
    let range = returned_interface(call, raw).value;
    Returned {
        call,
        value: Selection {
            range,
            style: selection.style,
            fetched,
        },
    }
}

pub(crate) fn set_selection(
    context: &Lease<ITfContext>,
    cookie: u32,
    range: &Lease<ITfRange>,
    style: TF_SELECTIONSTYLE,
) -> Call {
    // SAFETY: this is a borrowed ABI carrier. ManuallyDrop suppresses an implicit host Release;
    // the Lease retains the one real ownership reference throughout the fenced synchronous call.
    let borrowed = unsafe { ITfRange::from_raw(pointer(range)) };
    let selection = TF_SELECTION {
        range: ManuallyDrop::new(Some(borrowed)),
        style,
    };
    // SAFETY: retained context/range and the local SDK selection remain live through the call.
    unsafe { kr_foreign_set_selection(pointer(context), cookie, &selection) }
}

pub(crate) fn extent(range: &Lease<ITfRangeACP>) -> Returned<(i32, i32)> {
    let mut start = 0;
    let mut length = 0;
    // SAFETY: range is retained and both output scalars are distinct initialized slots.
    let call = unsafe { kr_foreign_extent(pointer(range), &mut start, &mut length) };
    Returned {
        call,
        value: (start, length),
    }
}

pub(crate) fn shift_start(range: &Lease<ITfRange>, cookie: u32, amount: i32) -> Returned<i32> {
    let mut shifted = 0;
    // SAFETY: the range is retained and shifted is distinct initialized output storage.
    let call = unsafe { kr_foreign_shift_start(pointer(range), cookie, amount, &mut shifted) };
    Returned {
        call,
        value: shifted,
    }
}
pub(crate) fn shift_end(range: &Lease<ITfRange>, cookie: u32, amount: i32) -> Returned<i32> {
    let mut shifted = 0;
    // SAFETY: the range is retained and shifted is distinct initialized output storage.
    let call = unsafe { kr_foreign_shift_end(pointer(range), cookie, amount, &mut shifted) };
    Returned {
        call,
        value: shifted,
    }
}
pub(crate) fn collapse(range: &Lease<ITfRange>, cookie: u32, anchor: TfAnchor) -> Call {
    // SAFETY: the retained range and cookie are supplied by the currently guarded edit frame.
    unsafe { kr_foreign_collapse(pointer(range), cookie, anchor) }
}

pub(crate) struct CompositionEntry {
    pub(crate) view: Option<Lease<ITfCompositionView>>,
    pub(crate) fetched: u32,
}
pub(crate) fn next(entries: &Lease<IEnumITfCompositionView>) -> Returned<CompositionEntry> {
    let mut output = std::ptr::null_mut();
    let mut fetched = 0;
    // SAFETY: entries is retained, and both output slots are initialized and distinct.
    let call = unsafe { kr_foreign_next(pointer(entries), &mut output, &mut fetched) };
    let view = returned_interface(call, output).value;
    Returned {
        call,
        value: CompositionEntry { view, fetched },
    }
}

pub(crate) struct Text {
    pub(crate) units: Vec<u16>,
    pub(crate) count: u32,
}
pub(crate) fn text(range: &Lease<ITfRange>, cookie: u32, capacity: usize) -> Returned<Text> {
    let mut units = vec![0; capacity];
    let mut count = 0;
    // SAFETY: the bounded buffer is live, range retained, and count is a distinct initialized output.
    let call = unsafe {
        kr_foreign_text(
            pointer(range),
            cookie,
            units.as_mut_ptr(),
            capacity as u32,
            &mut count,
        )
    };
    units.truncate((count as usize).min(capacity));
    Returned {
        call,
        value: Text { units, count },
    }
}
pub(crate) fn set_text(range: &Lease<ITfRange>, cookie: u32, units: &[u16]) -> Call {
    // SAFETY: the guarded frame retains range and bounded UTF-16 input through this single write.
    unsafe { kr_foreign_set_text(pointer(range), cookie, units.as_ptr(), units.len() as i32) }
}
pub(crate) fn start_composition(
    context: &Lease<ITfContextComposition>,
    cookie: u32,
    range: &Lease<ITfRange>,
    sink: *mut c_void,
) -> Returned<Option<Lease<ITfComposition>>> {
    let mut output = std::ptr::null_mut();
    // SAFETY: context/range are retained and the detached frame retains its own generated sink object.
    let call = unsafe {
        kr_foreign_start_composition(pointer(context), cookie, pointer(range), sink, &mut output)
    };
    returned_interface(call, output)
}
pub(crate) fn end_composition(composition: &Lease<ITfComposition>, cookie: u32) -> Call {
    // SAFETY: the frame retains and freshly validates its exact owned composition before this call.
    unsafe { kr_foreign_end_composition(pointer(composition), cookie) }
}

pub(crate) fn capture(
    dependencies: &Dependencies,
    window: HWND,
    host: &HostIdentity,
) -> Returned<Facts> {
    let mut output = Facts::default();
    // SAFETY: the dependency owner retains this typed callback/context, and outputs are complete objects.
    let call = unsafe {
        kr_foreign_field(
            dependencies.capture,
            dependencies.context,
            window,
            host,
            &mut output,
        )
    };
    Returned {
        call,
        value: output,
    }
}
