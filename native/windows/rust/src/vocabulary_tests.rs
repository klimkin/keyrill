use super::*;
use std::os::windows::ffi::OsStrExt;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, AtomicUsize};

struct PublicFiles(PathBuf);

impl PublicFiles {
    fn new() -> Self {
        static SERIAL: AtomicU32 = AtomicU32::new(0);
        let name = format!(
            "keyrill-worker-public-{}-{}",
            std::process::id(),
            SERIAL.fetch_add(1, Ordering::Relaxed)
        );
        let path = std::env::temp_dir().join(name);
        std::fs::create_dir(&path).expect("create fresh owned public fixture directory");
        Self(path)
    }

    fn source(&self, file: &str, defaults: bool) -> Source {
        let text = self
            .0
            .join(file)
            .as_os_str()
            .encode_wide()
            .collect::<Vec<_>>();
        let path = parse_path(&text).expect("bounded absolute fixture path");
        if defaults {
            Source::Default(Some(path))
        } else {
            Source::Explicit(path)
        }
    }

    fn write(&self, file: &str, bytes: &[u8]) {
        std::fs::write(self.0.join(file), bytes).expect("write public vocabulary fixture");
    }
}

impl Drop for PublicFiles {
    fn drop(&mut self) {
        let path = self
            .0
            .canonicalize()
            .expect("resolve owned fixture cleanup root");
        assert!(path.is_absolute());
        assert!(
            path.file_name()
                .expect("fixture basename")
                .to_string_lossy()
                .starts_with("keyrill-worker-public-")
        );
        let metadata = std::fs::symlink_metadata(&path).expect("inspect cleanup root");
        assert!(metadata.is_dir() && !metadata.file_type().is_symlink());
        std::fs::remove_dir_all(path).expect("remove owned fixture directory");
    }
}

struct Events {
    published: Handle,
    resume: Handle,
    finished: Handle,
    retained: AtomicUsize,
    released: AtomicUsize,
    after_count: AtomicBool,
    failed: AtomicBool,
    pause_at: Boundary,
}

impl Events {
    fn new() -> Arc<Self> {
        Self::at(Boundary::Published)
    }

    fn at(pause_at: Boundary) -> Arc<Self> {
        Arc::new(Self {
            published: Hooks::default()
                .event(Error::Startup)
                .expect("publication event"),
            resume: Hooks::default()
                .event(Error::Startup)
                .expect("resume event"),
            finished: Hooks::default()
                .event(Error::Startup)
                .expect("completion event"),
            retained: AtomicUsize::new(0),
            released: AtomicUsize::new(0),
            after_count: AtomicBool::new(false),
            failed: AtomicBool::new(false),
            pause_at,
        })
    }

    fn hooks(self: &Arc<Self>) -> Hooks {
        Hooks {
            call: hook as *mut c_void,
            context: Arc::as_ptr(self).cast_mut().cast(),
            retain_context: retain as *mut c_void,
            release_context: release as *mut c_void,
            ..Hooks::default()
        }
    }

    fn next(&self, worker: &mut Worker) -> Ready {
        // SAFETY: these events remain owned by the test context through the worker lease.
        assert_eq!(
            unsafe { WaitForSingleObject(self.published.0, 15000) },
            WAIT_OBJECT_0
        );
        let ready = worker.poll().expect("whole immutable packet published");
        // SAFETY: Published is held until the owner has taken exactly that packet.
        unsafe { ResetEvent(self.published.0) }.expect("reset publication event");
        unsafe { SetEvent(self.resume.0) }.expect("release publication boundary");
        ready
    }

    fn stop(&self, worker: &mut Worker) {
        worker.stop();
        // SAFETY: resume permits a callback already held at Published to complete cancellation.
        unsafe { SetEvent(self.resume.0) }.expect("release stopped publication");
        assert_eq!(
            unsafe { WaitForSingleObject(self.finished.0, 15000) },
            WAIT_OBJECT_0
        );
        assert!(worker.poll().is_none());
        assert!(self.after_count.load(Ordering::Acquire));
        assert_eq!(self.retained.load(Ordering::Acquire), 1);
        assert_eq!(self.released.load(Ordering::Acquire), 1);
        assert!(!self.failed.load(Ordering::Acquire));
    }
}

unsafe extern "C" fn hook(context: *mut c_void, boundary: u32, _: HANDLE, _: usize) {
    // SAFETY: HookLease retains this immutable Events allocation until AfterCount returns.
    let events = unsafe { &*context.cast::<Events>() };
    if boundary == events.pause_at as u32 {
        // SAFETY: all event handles belong to the retained Events allocation.
        unsafe {
            let _ = SetEvent(events.published.0);
            if WaitForSingleObject(events.resume.0, 15000) != WAIT_OBJECT_0 {
                events.failed.store(true, Ordering::Release);
            }
            let _ = ResetEvent(events.resume.0);
        }
    }
    if boundary == Boundary::AfterCount as u32 {
        events.after_count.store(true, Ordering::Release);
    }
}

#[test]
fn stopped_worker_cannot_publish_after_held_boundary() {
    let files = PublicFiles::new();
    files.write("en.add", b"hello\n");
    let events = Events::at(Boundary::BeforePublish);
    let mut worker = Worker::default();
    let model = Model {
        sources: [files.source("en.add", false), files.source("en.add", false)],
    };
    assert!(worker.start(&model, 9, ModuleOwner::Process, events.hooks()));
    // SAFETY: this event belongs to the retained test context and witnesses the held callback.
    assert_eq!(
        unsafe { WaitForSingleObject(events.published.0, 15000) },
        WAIT_OBJECT_0
    );
    assert!(worker.poll().is_none());
    worker.stop();
    // SAFETY: the callback remains held until the test releases its resume event.
    assert_eq!(
        unsafe { WaitForSingleObject(events.finished.0, 0) },
        WAIT_TIMEOUT
    );
    events.stop(&mut worker);
}

unsafe extern "system" fn retain(context: *mut c_void) {
    // SAFETY: the test owner lends one Arc-backed context and retains its original strong reference.
    unsafe { Arc::increment_strong_count(context.cast::<Events>()) };
    let events = unsafe { &*context.cast::<Events>() };
    events.retained.fetch_add(1, Ordering::AcqRel);
}

unsafe extern "system" fn release(context: *mut c_void) {
    // SAFETY: successful retain transferred one strong count to HookLease, consumed exactly here.
    let events = unsafe { Arc::from_raw(context.cast::<Events>()) };
    events.released.fetch_add(1, Ordering::AcqRel);
    if !events.after_count.load(Ordering::Acquire) {
        events.failed.store(true, Ordering::Release);
    }
    // SAFETY: the test owner still holds its original Arc until this event is observed.
    let _ = unsafe { SetEvent(events.finished.0) };
}

#[test]
fn bounded_path_admission() {
    for text in [
        r"\\server\share\file",
        r"C:file",
        r"C:\a\..\b",
        r"C:\NUL.add",
        r"C:\a:stream",
        r"C:\a.\b",
        "C:\\a\\",
        r"C:\a//b",
        r"C:\\a",
        r"C:\COM¹.add",
        r"C:\conout$",
    ] {
        assert!(
            parse_path(&text.encode_utf16().collect::<Vec<_>>()).is_none(),
            "unsafe path admitted"
        );
    }
    let path = parse_path(
        &r"C:\public\vocabulary.add"
            .encode_utf16()
            .collect::<Vec<_>>(),
    )
    .expect("valid bounded path");
    assert_eq!(path.components.len(), 2);
}

#[test]
fn real_worker_settles_retains_and_releases_after_count() {
    let files = PublicFiles::new();
    files.write("en.add", b"hello\n");
    files.write("ru.add", b"");
    let events = Events::new();
    let mut worker = Worker::default();
    let model = Model {
        sources: [files.source("en.add", false), files.source("ru.add", false)],
    };
    assert!(worker.start(&model, 7, ModuleOwner::Process, events.hooks()));
    let first = events.next(&mut worker);
    assert_eq!(first.activation, 7);
    assert_eq!(first.sources[0].status, Status::Pending);
    assert!(first.sources[0].dictionary.is_none());
    let stable = events.next(&mut worker);
    assert!(stable.model);
    assert_eq!(stable.sources[0].status, Status::Ready);
    assert_eq!(stable.sources[1].status, Status::Ready);
    let original = stable.sources[0]
        .dictionary
        .as_ref()
        .expect("settled dictionary")
        .clone();
    files.write("en.add", b"bad\0data");
    let malformed = events.next(&mut worker);
    assert_eq!(malformed.sources[0].status, Status::Retained);
    assert_eq!(malformed.sources[0].error, Error::Parse);
    assert!(Arc::ptr_eq(
        malformed.sources[0]
            .dictionary
            .as_ref()
            .expect("last good dictionary retained"),
        &original
    ));
    files.write("en.add", b"world\n");
    let staged = events.next(&mut worker);
    assert_eq!(staged.sources[0].status, Status::Retained);
    assert_eq!(staged.sources[0].error, Error::None);
    assert!(Arc::ptr_eq(
        staged.sources[0]
            .dictionary
            .as_ref()
            .expect("replacement settling retains last good"),
        &original
    ));
    let replacement = events.next(&mut worker);
    assert_eq!(replacement.sources[0].status, Status::Ready);
    assert!(!Arc::ptr_eq(
        replacement.sources[0]
            .dictionary
            .as_ref()
            .expect("settled replacement"),
        &original
    ));
    std::fs::remove_file(files.0.join("en.add")).expect("remove published source");
    let missing = events.next(&mut worker);
    assert_eq!(missing.sources[0].status, Status::Retained);
    assert_eq!(missing.sources[0].error, Error::Missing);
    events.stop(&mut worker);

    let events = Events::new();
    let absent = Model {
        sources: [
            files.source("missing\\default.add", true),
            files.source("explicit.add", false),
        ],
    };
    assert!(worker.start(&absent, 8, ModuleOwner::Process, events.hooks()));
    let packet = events.next(&mut worker);
    assert_eq!(packet.sources[0].status, Status::Ready);
    assert!(packet.sources[0].dictionary.is_some());
    assert_eq!(packet.sources[1].status, Status::Missing);
    assert!(packet.sources[1].dictionary.is_none());
    events.stop(&mut worker);
}
