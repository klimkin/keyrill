use crate::foreign::{self, Call, Lease};
use crate::service::{ObjectLease, Service};
use std::cell::Cell;
use std::rc::Rc;
use windows::Win32::Foundation::{E_FAIL, S_OK};
use windows::Win32::UI::TextServices::{
    ITfContext, ITfEditSession, ITfEditSession_Impl, TF_ES_READWRITE, TF_ES_SYNC,
};
use windows_core::{HRESULT, Result, implement};

#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub(crate) enum Effect {
    #[default]
    Unattempted,
    Attempted,
    Verified,
}

pub(crate) struct Frame {
    service: Rc<Service>,
    entered: Cell<bool>,
    completed: Cell<bool>,
    expired: Cell<bool>,
    effect: Cell<Effect>,
}

impl Frame {
    pub(crate) fn new(service: Rc<Service>) -> Rc<Self> {
        Rc::new(Self {
            service,
            entered: Cell::new(false),
            completed: Cell::new(false),
            expired: Cell::new(false),
            effect: Cell::new(Effect::Unattempted),
        })
    }

    fn run(&self) -> HRESULT {
        if self.expired.get() || self.entered.replace(true) {
            return E_FAIL;
        }
        let result = self.service.checked(|| {
            #[cfg(feature = "test-api")]
            self.service.inject_panic(1);
            self.effect.set(Effect::Attempted);
            #[cfg(feature = "test-api")]
            self.service.inject_panic(2);
            self.effect.set(Effect::Verified);
            S_OK
        });
        self.completed.set(true);
        result
    }

    pub(crate) fn request(self: &Rc<Self>, context: &Lease<ITfContext>, client: u32) -> Request {
        let session: ITfEditSession = EditSession {
            frame: Rc::clone(self),
            _object: ObjectLease::new(),
        }
        .into();
        let (outer, callback) =
            foreign::request(context, client, &session, TF_ES_SYNC.0 | TF_ES_READWRITE.0);
        self.expired.set(true);
        Request {
            outer,
            callback,
            entered: self.entered.get(),
            completed: self.completed.get(),
            effect: self.effect.get(),
        }
    }
}

pub(crate) struct Request {
    pub(crate) outer: Call,
    pub(crate) callback: HRESULT,
    pub(crate) entered: bool,
    pub(crate) completed: bool,
    pub(crate) effect: Effect,
}

#[implement(ITfEditSession, Agile = false)]
struct EditSession {
    frame: Rc<Frame>,
    _object: ObjectLease,
}

impl ITfEditSession_Impl for EditSession_Impl {
    fn DoEditSession(&self, _: u32) -> Result<()> {
        self.frame.run().ok()
    }
}
