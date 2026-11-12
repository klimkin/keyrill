use keyrill_ffi::{
    Engine as NativeEngine, kr_abi_version, kr_adopt_dictionaries, kr_engine_free, kr_engine_new,
    kr_reset, kr_set_automatic_policy, kr_set_keyboard,
};
use std::ptr::NonNull;

pub(crate) struct Engine(NonNull<NativeEngine>);

impl Engine {
    pub(crate) fn new() -> Result<Self, u32> {
        let mut pointer = std::ptr::null_mut();
        // SAFETY: pointer is distinct writable storage and transfers one newly allocated handle.
        let status = unsafe { kr_engine_new(kr_abi_version(), &mut pointer) };
        if status != 0 {
            return Err(status);
        }
        NonNull::new(pointer).map(Self).ok_or(1)
    }

    pub(crate) fn configure(&mut self, policy: u32) -> Result<(), u32> {
        // SAFETY: this owner serializes one live engine, with no callbacks or retained borrows.
        unsafe {
            accepted(kr_reset(self.0.as_ptr()))?;
            accepted(kr_set_keyboard(self.0.as_ptr(), 1))?;
            accepted(kr_adopt_dictionaries(
                self.0.as_ptr(),
                std::ptr::null(),
                std::ptr::null(),
            ))?;
            accepted(kr_set_automatic_policy(self.0.as_ptr(), policy))
        }
    }

    pub(crate) fn reset(&mut self) {
        // SAFETY: this owner has the unique serialized handle. Poison remains permanent in the ABI.
        unsafe { kr_reset(self.0.as_ptr()) };
    }
}

fn accepted(status: u32) -> Result<(), u32> {
    if status == 0 { Ok(()) } else { Err(status) }
}

impl Drop for Engine {
    fn drop(&mut self) {
        // SAFETY: this is the sole handle owner and Service survives every outstanding edit frame.
        unsafe { kr_engine_free(self.0.as_ptr()) };
    }
}
