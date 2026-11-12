//! Explicit-target UIA evidence. Character reads follow native and provider metadata.
use crate::field::{self, Metadata, NativeFocus, Provider, ReadOnly, Refusal, WindowFacts};
use crate::native::{Target, ThreadCheck, ThreadState, capture_thread, started};
use windows::Win32::Foundation::*;
use windows::Win32::System::Com::*;
use windows::Win32::System::Threading::*;
use windows::Win32::System::Variant::*;
use windows::Win32::UI::Accessibility::*;
use windows::Win32::UI::Input::KeyboardAndMouse::IsWindowEnabled;
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::{BOOL, BSTR, HRESULT, IUnknown, Interface};

pub(crate) fn exact_value<T: Default>(call: impl FnOnce(*mut T) -> HRESULT) -> Result<T, Refusal> {
    let mut output = T::default();
    if call(&mut output) == S_OK {
        Ok(output)
    } else {
        Err(Refusal::Provider)
    }
}
pub(crate) fn exact_interface<T: Interface>(
    call: impl FnOnce(*mut *mut std::ffi::c_void) -> HRESULT,
) -> Result<T, Refusal> {
    let mut raw = std::ptr::null_mut();
    let result = call(&mut raw);
    let output = unsafe { T::from_raw_borrowed(&raw) }.cloned();
    if !raw.is_null() {
        drop(unsafe { IUnknown::from_raw(raw) });
    }
    if result == S_OK {
        output.ok_or(Refusal::Provider)
    } else {
        Err(Refusal::Provider)
    }
}
macro_rules! value {
    ($object:expr, $method:ident $(, $argument:expr)*) => {{
        let object = &$object;
        exact_value(|output| unsafe { (object.vtable().$method)(object.as_raw(), $($argument,)* output) })
    }};
}
macro_rules! interface {
    ($object:expr, $method:ident $(, $argument:expr)*) => {{
        let object = &$object;
        exact_interface(|output| unsafe { (object.vtable().$method)(object.as_raw(), $($argument,)* output) })
    }};
}

pub fn describe(handle: HWND) -> WindowFacts {
    let mut result = WindowFacts {
        handle: handle.0 as usize,
        ..Default::default()
    };
    if handle.is_invalid() || !unsafe { IsWindow(Some(handle)) }.as_bool() {
        return result;
    }
    result.tid = unsafe { GetWindowThreadProcessId(handle, Some(&mut result.pid)) };
    let mut name = [0; 128];
    let length = unsafe { GetClassNameW(handle, &mut name) };
    result.class_truncated = length == 127;
    result.class_name = name[..length.max(0) as usize].to_vec();
    unsafe {
        SetLastError(ERROR_SUCCESS);
    }
    result.style = unsafe { GetWindowLongPtrW(handle, GWL_STYLE) };
    let style_error = unsafe { GetLastError() };
    unsafe {
        SetLastError(ERROR_SUCCESS);
    }
    result.exstyle = unsafe { GetWindowLongPtrW(handle, GWL_EXSTYLE) };
    let exstyle_error = unsafe { GetLastError() };
    unsafe {
        SetLastError(ERROR_SUCCESS);
    }
    result.control = unsafe { GetDlgCtrlID(handle) };
    let control_error = unsafe { GetLastError() };
    result.enabled = unsafe { IsWindowEnabled(handle) }.as_bool();
    result.complete = result.tid != 0
        && length > 0
        && !result.class_truncated
        && style_error == ERROR_SUCCESS
        && exstyle_error == ERROR_SUCCESS
        && control_error == ERROR_SUCCESS;
    result
}

pub struct NativeProvider<'a> {
    target: &'a Target,
    foreground_thread: ThreadCheck,
    focused_thread: ThreadCheck,
    foreground_started: u64,
    focused_started: u64,
    native: NativeFocus,
    automation: IUIAutomation,
    element: Option<IUIAutomationElement>,
    text: Option<IUIAutomationTextPattern>,
    document: Option<IUIAutomationTextRange>,
}
impl<'a> NativeProvider<'a> {
    pub fn open(target: &'a Target) -> Result<Self, Refusal> {
        if target.identity.kind == 0 || !target.current() {
            return Err(Refusal::Target);
        }
        let foreground = unsafe { GetForegroundWindow() };
        let mut pid = 0;
        let tid = unsafe { GetWindowThreadProcessId(foreground, Some(&mut pid)) };
        if foreground.is_invalid() || pid != target.identity.process || tid == 0 {
            return Err(Refusal::NativeFocus);
        }
        let foreground_thread = capture_thread(target.process.0, pid, tid);
        let foreground_started = foreground_thread
            .handle
            .as_ref()
            .filter(|_| foreground_thread.state == ThreadState::Live)
            .and_then(|handle| started(handle.0, true).ok())
            .ok_or(Refusal::Stale)?;
        let mut info = GUITHREADINFO {
            cbSize: std::mem::size_of::<GUITHREADINFO>() as u32,
            ..Default::default()
        };
        unsafe { GetGUIThreadInfo(tid, &mut info) }.map_err(|_| Refusal::NativeFocus)?;
        if info.hwndFocus.is_invalid()
            || unsafe { GetAncestor(info.hwndFocus, GA_ROOT) } != foreground
        {
            return Err(Refusal::NativeFocus);
        }
        let focused_tid = unsafe { GetWindowThreadProcessId(info.hwndFocus, Some(&mut pid)) };
        if focused_tid == 0 || pid != target.identity.process {
            return Err(Refusal::NativeFocus);
        }
        let focused_thread = capture_thread(target.process.0, pid, focused_tid);
        let focused_started = focused_thread
            .handle
            .as_ref()
            .filter(|_| focused_thread.state == ThreadState::Live)
            .and_then(|handle| started(handle.0, true).ok())
            .ok_or(Refusal::Stale)?;
        let native = sample(
            target,
            &foreground_thread,
            foreground_started,
            &focused_thread,
            focused_started,
        )?;
        let safety = field::native_safety(&native);
        if safety != Refusal::None {
            return Err(safety);
        }
        let automation = unsafe { CoCreateInstance(&CUIAutomation, None, CLSCTX_INPROC_SERVER) }
            .map_err(|_| Refusal::Provider)?;
        let result = Self {
            target,
            foreground_thread,
            focused_thread,
            foreground_started,
            focused_started,
            native,
            automation,
            element: None,
            text: None,
            document: None,
        };
        result.guard()?;
        Ok(result)
    }
    fn guard(&self) -> Result<(), Refusal> {
        let now = sample(
            self.target,
            &self.foreground_thread,
            self.foreground_started,
            &self.focused_thread,
            self.focused_started,
        )?;
        if now == self.native {
            Ok(())
        } else {
            Err(Refusal::Stale)
        }
    }
    fn call<T>(&self, function: impl FnOnce() -> Result<T, Refusal>) -> Result<T, Refusal> {
        self.guard()?;
        let result = function();
        self.guard()?;
        result
    }
    fn attribute(&self, value: &VARIANT) -> ReadOnly {
        let raw = unsafe { &value.Anonymous.Anonymous };
        if raw.vt == VT_BOOL {
            return match unsafe { raw.Anonymous.boolVal }.0 {
                0 => ReadOnly::Writable,
                -1 => ReadOnly::ReadOnly,
                _ => ReadOnly::Unknown,
            };
        }
        if raw.vt != VT_UNKNOWN {
            return ReadOnly::Unknown;
        }
        let Some(unknown) = (unsafe { &*raw.Anonymous.punkVal }) else {
            return ReadOnly::Unknown;
        };
        let Ok(identity) = unknown.cast::<IUnknown>() else {
            return ReadOnly::Unknown;
        };
        let mixed: Result<IUnknown, _> =
            self.call(|| interface!(self.automation, ReservedMixedAttributeValue));
        let unsupported: Result<IUnknown, _> =
            self.call(|| interface!(self.automation, ReservedNotSupportedValue));
        if mixed.ok().as_ref() == Some(&identity) {
            ReadOnly::Mixed
        } else if unsupported.ok().as_ref() == Some(&identity) {
            ReadOnly::Unsupported
        } else {
            ReadOnly::Unknown
        }
    }
    fn metadata_inner(&mut self) -> Result<Metadata, Refusal> {
        self.guard()?;
        let mut result = Metadata {
            native: self.native.clone(),
            ..Default::default()
        };
        let safety = field::native_safety(&result.native);
        if safety != Refusal::None {
            return Err(safety);
        }
        let focused: IUIAutomationElement =
            self.call(|| interface!(self.automation, GetFocusedElement))?;
        result.provider_pid = self.call(|| value!(focused, CurrentProcessId))?;
        let window: HWND = self.call(|| value!(focused, CurrentNativeWindowHandle))?;
        result.provider_window = window.0 as usize;
        if result.provider_pid != self.target.identity.process as i32
            || result.provider_window == 0
            || result.provider_window != self.native.focused.handle
        {
            return Err(Refusal::Binding);
        }
        let from_handle: IUIAutomationElement =
            self.call(|| interface!(self.automation, ElementFromHandle, window))?;
        let same: BOOL = self.call(|| {
            value!(
                self.automation,
                CompareElements,
                focused.as_raw(),
                from_handle.as_raw()
            )
        })?;
        if same != BOOL(1) {
            return Err(Refusal::Binding);
        }
        if let Some(element) = &self.element {
            let same: BOOL = self.call(|| {
                value!(
                    self.automation,
                    CompareElements,
                    focused.as_raw(),
                    element.as_raw()
                )
            })?;
            if same != BOOL(1) {
                return Err(Refusal::Stale);
            }
        }
        self.element = Some(focused.clone());
        result.exact_binding = true;
        result.control_type = self
            .call(|| value!(focused, CurrentControlType))
            .map(|id: UIA_CONTROLTYPE_ID| id.0)?;
        let password: BOOL = self.call(|| value!(focused, CurrentIsPassword))?;
        if password != BOOL(0) {
            return Err(Refusal::Password);
        }
        result.nonpassword = true;
        let enabled: BOOL = self.call(|| value!(focused, CurrentIsEnabled))?;
        if enabled != BOOL(1) {
            return Err(Refusal::Disabled);
        }
        result.enabled = true;
        result.value_readonly = ReadOnly::Unavailable;
        let value: Result<IUIAutomationValuePattern, _> = self.call(|| {
            interface!(
                focused,
                GetCurrentPatternAs,
                UIA_ValuePatternId,
                &IUIAutomationValuePattern::IID
            )
        });
        if let Ok(value) = value {
            result.value_readonly = match self
                .call(|| value!(value, CurrentIsReadOnly))
                .map(|value: BOOL| value.0)
            {
                Ok(0) => ReadOnly::Writable,
                Ok(1) => ReadOnly::ReadOnly,
                Ok(_) => ReadOnly::Unknown,
                Err(_) => ReadOnly::Unavailable,
            };
        }
        result.text_readonly = ReadOnly::Unavailable;
        let text: Result<IUIAutomationTextPattern, _> = self.call(|| {
            interface!(
                focused,
                GetCurrentPatternAs,
                UIA_TextPatternId,
                &IUIAutomationTextPattern::IID
            )
        });
        let mut document: Option<IUIAutomationTextRange> = None;
        if let Ok(text) = &text {
            result.text_pattern = true;
            document = self.call(|| interface!(text, DocumentRange)).ok();
            if let Some(document) = &document {
                if let Ok(attribute) =
                    self.call(|| value!(document, GetAttributeValue, UIA_IsReadOnlyAttributeId))
                {
                    result.text_readonly = self.attribute(&attribute);
                }
            }
        }
        self.guard()?;
        self.text = text.ok();
        self.document = document;
        result.refusal = Refusal::None;
        Ok(result)
    }
}
impl Provider for NativeProvider<'_> {
    fn metadata(&mut self) -> Metadata {
        match self.metadata_inner() {
            Ok(result) => result,
            Err(refusal) => Metadata {
                native: self.native.clone(),
                refusal,
                ..Default::default()
            },
        }
    }
    fn document(&mut self, bound: usize) -> Result<Vec<u16>, Refusal> {
        let document = self.document.as_ref().ok_or(Refusal::TextUnavailable)?;
        let text: BSTR = self.call(|| {
            let mut raw = std::ptr::null_mut();
            let result =
                unsafe { (document.vtable().GetText)(document.as_raw(), bound as i32, &mut raw) };
            let text = unsafe { BSTR::from_raw(raw.cast()) };
            if result == S_OK && !raw.is_null() {
                Ok(text)
            } else {
                Err(Refusal::TextUnavailable)
            }
        })?;
        if text.len() > bound {
            return Err(Refusal::TextUnavailable);
        }
        Ok(text.to_vec())
    }
    fn end_caret(&mut self) -> Result<(), Refusal> {
        let text = self.text.as_ref().ok_or(Refusal::TextUnavailable)?;
        let document = self.document.as_ref().ok_or(Refusal::TextUnavailable)?;
        let result = self.call(|| {
            let selections: IUIAutomationTextRangeArray = interface!(text, GetSelection)?;
            if value!(selections, Length)? != 1 {
                return Err(Refusal::Selection);
            }
            let selection: IUIAutomationTextRange = interface!(selections, GetElement, 0)?;
            let collapsed: i32 = value!(
                selection,
                CompareEndpoints,
                TextPatternRangeEndpoint_Start,
                selection.as_raw(),
                TextPatternRangeEndpoint_End
            )?;
            let end: i32 = value!(
                selection,
                CompareEndpoints,
                TextPatternRangeEndpoint_End,
                document.as_raw(),
                TextPatternRangeEndpoint_End
            )?;
            if collapsed == 0 && end == 0 {
                Ok(())
            } else {
                Err(Refusal::Selection)
            }
        });
        result.map_err(|failure| {
            if failure == Refusal::Stale {
                failure
            } else {
                Refusal::Selection
            }
        })
    }
}
fn sample(
    target: &Target,
    foreground_thread: &ThreadCheck,
    foreground_started: u64,
    focused_thread: &ThreadCheck,
    focused_started: u64,
) -> Result<NativeFocus, Refusal> {
    if !target.current() {
        return Err(Refusal::Stale);
    }
    for (thread, timestamp) in [
        (foreground_thread, foreground_started),
        (focused_thread, focused_started),
    ] {
        let handle = thread.handle.as_ref().ok_or(Refusal::Stale)?.0;
        if unsafe { WaitForSingleObject(handle, 0) } != WAIT_TIMEOUT
            || unsafe { GetProcessIdOfThread(handle) } != target.identity.process
            || unsafe { GetThreadId(handle) } != thread.tid
            || started(handle, true).ok() != Some(timestamp)
        {
            return Err(Refusal::Stale);
        }
    }
    let foreground = unsafe { GetForegroundWindow() };
    let mut pid = 0;
    let foreground_tid = unsafe { GetWindowThreadProcessId(foreground, Some(&mut pid)) };
    if foreground.is_invalid()
        || pid != target.identity.process
        || foreground_tid != foreground_thread.tid
    {
        return Err(Refusal::Stale);
    }
    let mut info = GUITHREADINFO {
        cbSize: std::mem::size_of::<GUITHREADINFO>() as u32,
        ..Default::default()
    };
    unsafe { GetGUIThreadInfo(foreground_tid, &mut info) }.map_err(|_| Refusal::Stale)?;
    if info.hwndFocus.is_invalid() || unsafe { GetAncestor(info.hwndFocus, GA_ROOT) } != foreground
    {
        return Err(Refusal::Stale);
    }
    let focused = describe(info.hwndFocus);
    let root = describe(unsafe { GetAncestor(info.hwndFocus, GA_ROOT) });
    let mut focused_info = GUITHREADINFO {
        cbSize: std::mem::size_of::<GUITHREADINFO>() as u32,
        ..Default::default()
    };
    unsafe { GetGUIThreadInfo(focused_thread.tid, &mut focused_info) }
        .map_err(|_| Refusal::Stale)?;
    if focused.pid != pid
        || focused.tid != focused_thread.tid
        || info.hwndFocus != focused_info.hwndFocus
        || root.handle != foreground.0 as usize
    {
        return Err(Refusal::Stale);
    }
    let mut parent = unsafe { GetAncestor(info.hwndFocus, GA_PARENT) };
    let mut parents = Vec::new();
    while !parent.is_invalid() && parents.len() != 8 {
        parents.push(describe(parent));
        parent = unsafe { GetAncestor(parent, GA_PARENT) };
    }
    Ok(NativeFocus {
        pid,
        foreground_tid,
        process_started: target.identity.started,
        foreground_thread_started: foreground_started,
        focused_thread_started: focused_started,
        foreground: foreground.0 as usize,
        focused,
        root,
        parents,
        parents_truncated: !parent.is_invalid(),
    })
}
