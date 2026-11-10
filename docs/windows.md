# Windows TSF proof

This is an x64 Text Services Framework adapter with manual conversion and configurable automatic
correction. It links the
existing Rust engine through [the C ABI](../include/keyrill.h). It has no replacement backend,
hooks, input injection, clipboard transport, or runtime network access. Admission covers the
proof editor and the version-bounded Notepad main-document candidate described below.

The first controlled-editor proof has passed on Windows x64 with the fixture policy. The
maintainer registered and enabled both proof profiles. Physical runs passed correction, rapid
continuation, native Undo, focus preservation, excluded-control checks, native lock denial,
and bounded owned-shutdown and empty-close assertions. Automated real-editor runs separately
passed correction, Escape reversal, continuation, and lifecycle assertions. Automated keys do
not count as physical keyboard evidence.

The earlier unchanged-text correction attempt was superseded by later verified correction.
An initial caret mismatch and a later automated Escape end-caret defect remain part of the
history. The changing-finish fix passed its controlled regression and the same automated
correction and reversal document/caret oracles without a caret repair.

The final lifecycle-journal build has case-level Tab and A-to-B-to-A evidence under automated
keys and clicks. Tab recorded EndRequested and FinishVerified for the owned serial before
document-focus invalidation. Clicking B and then A each recorded ExactTermination for the mark
owned in the departing field before focus invalidation. The exact correction and final focus
document/caret oracles passed, with zero Unknown effects. These runs establish the observed
fixture sequence, not physical keyboard evidence or general application support. The native
EN-to-RU experiment below also passed with fresh owned evidence and an independent no-Space
document/caret oracle. This completes the controlled-editor proof, not general Windows support.
Manual commands, shared model correction in both directions, and vocabulary reloads also passed
automated real-editor acceptance as described below. Application coverage, settings, and packaging
remain later work. Unretained final destruction and complete callback drainage are not established
by the bounded shutdown assertion.

## Build and check without installation

Use Windows x64 with Visual Studio 2022 Build Tools, its C++ workload and Windows SDK, CMake,
and the repository's pinned Rust toolchain. No package is downloaded or installed by the build.
The Visual Studio CMake generator discovers MSVC without a developer shell. The build queries
`cargo rustc --print native-static-libs` and uses the returned libraries with the DLL C runtime.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/windows/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File native/windows/build.ps1 -Mode sanitize
```

From Git Bash, `./tools/verify` runs the portable checks and dispatches the Windows build,
controlled schedules, C11 ABI smoke, and registration-free COM smoke tests to MSVC.
`./tools/verify sanitize` dispatches the native AddressSanitizer configuration. This instruments
the C++ service and controlled tests, not the Rust static library. MSVC does not supply UBSan.
It is not the macOS ASan/UBSan gate and does not replace physical application testing.

The build produces these separate artifacts in `target/windows-check/Release`.

| Artifact | Purpose |
|---|---|
| `KeyrillTsf.dll` | In-process STA COM text service |
| `KeyrillRegister.exe` | Explicit maintainer registration, status, and enablement commands |
| `KeyrillProofEditor.exe` | Stock `RICHEDIT50W` editor with writable A and B, excluded controls, and separate host and service status |
| `KeyrillProofInspect.exe` | Read-only UIA fixture assertions and bounded host and service snapshots |
| `KeyrillHostInspect.exe` | Explicit-PID field metadata, optional service observation, and named public-document UIA checks |
| `keyrill_dll_smoke.exe` | Registration-free DLL factory and Rust-link check, not TSF activation |
| `keyrill_activation_smoke.exe` | `CoCreateInstance` with the proof manifest and an adjacent-DLL identity check |
| `keyrill_abi_smoke.exe` | Existing C ABI behavior against its controlled document |

`KeyrillProofEditor.exe` and `keyrill_activation_smoke.exe` embed the same
`proof_activation.manifest`. Its COM mapping binds the existing service CLSID to the adjacent
`KeyrillTsf.dll` through the process activation context. The smoke checks that the manifest maps
the CLSID from `profiles.h` and that the returned interface belongs to that adjacent DLL. A second
test runs the same executable without an adjacent DLL and requires COM activation to fail.
The direct DLL factory smoke remains a separate check.

The manifest changes only activation inside the proof host. Live TSF testing still uses the
existing registered profile and the source selected by the maintainer. This does not install,
register, enable, or select a source, and does not prove activation in general applications.

The controlled COM test executable has the same fixture executable name in the separate
`target/windows-check/tests/Release` directory. It supplies fake TSF interfaces to the exact
`Service::HandleEvent` and `ITfEditSession::DoEditSession` implementation used by the DLL.
Its client ID and cookie belong only to those doubles. They never establish live routing.

## Bounded Notepad admission and observation

The runtime admits the main document of packaged x64 Notepad **11.2607.14.0** as a candidate
for the existing owned-composition executor. Its package family
`Microsoft.WindowsNotepad_8wekyb3d8bbwe`, package name `Microsoft.WindowsNotepad`, architecture,
executable basename and exact version are captured at service activation. Other Notepad versions
retain metadata observation and pass every key. Rust, the C ABI and macOS do not change.

Package identity alone grants no editing authority. Before selection ACP or character access,
the edit session validates the canonical TSF top context, active view, exact current-thread
native focus, and freshly captured native field facts. The field must be `RichEditD2DPT`,
control ID zero, on the current process and TSF thread. Its immediate parent must be
`NotepadTextBox`, control ID zero, on that same process and thread. The next parent must be
`Notepad`, control ID zero, in that process and equal both the actual foreground window and
the field's `GA_ROOT`. The root may belong to a different thread. All three nodes must have
complete metadata and be enabled. The field requires `WS_CHILD` and `ES_MULTILINE` and refuses
`ES_PASSWORD`, `ES_READONLY` and `WS_DISABLED`. Dynamic scrollbar bits do not establish identity.
Find, other controls, other hierarchies and unknown versions receive no text admission.

Writable context status and all three disabled compartments precede raw selection acquisition.
`GetStatus` requires exactly `S_OK`. A disabling compartment permits `S_OK` with `VT_EMPTY`
or `VT_I4` zero, or the legitimate unset signature `S_FALSE` with `VT_EMPTY`. Other results,
nonempty `S_FALSE` values, malformed values, and interface failures refuse admission.
The raw range must belong to that same context and have valid selection flags. The scope query
then requires exact `S_OK` results, a present property and interface, and exactly one `IS_DEFAULT`
or `IS_TEXT`. The only alternative, `NotepadUnavailableValue`, requires the freshly matched
Notepad field, `GetAppProperty` returning exactly `S_OK` with a property, and `GetValue` returning
exactly `E_FAIL` with its initialized output still `VT_EMPTY`. The compiled main-field contract
supplies ordinary-field authority; the error is not interpreted as a default scope. Other
failures, failed nonempty values, private scopes, multiple scopes and malformed results refuse
before ACP, characters or composition. Proof controls continue to require explicit ordinary scope.

The same private acquisition runs initially and after a finish that requests replan. It checks
generation and the exact native field again after scope callbacks, then obtains ACP. Field
identity lives only in that edit session. A shared private range guard checks fresh native field
identity, canonical range context, writable status, all three compartments, and scope before
each character read, composition start, text replacement, selection update, and composition end.
Callback-capable preparation precedes the guard. The guard rechecks exact field identity and
generation after policy callbacks. These metadata queries cannot make document authority atomic.
Owned range, text, caret, returned range, and acknowledgement checks remain in the single executor.
There is no UIA runtime dependency, admission override, second editor or new editing transport.

Controlled native facts and COM schedules establish these branches, including zero ACP/text/
composition/write before failed admission. They do not establish an owned operation in ordinary
Notepad. First live acceptance still requires the exact reviewed DLL loaded by a fresh unmodified
Notepad process, a nonnull owned composition, exact returned ranges and readback, an independent
full public document/caret oracle and zero Unknown effects. Correction, retained reversal,
manual conversion, model/vocabulary behavior, continuation, Undo, focus, tab, source and close
remain separate live gates. Unchanged FINISH now performs one exact owned-range replacement
before ending the composition. Controlled tests preserve moved carets and noncollapsed selections,
but fake `SetText` does not move selection. Actual Notepad selection preservation and Undo remain
unproven. The mapping of a platform-requested focus-loss commit also remains a live gate.
Focus and termination notifications grant no new write authority or saved-text writeback.

For an unadmitted Notepad version, after the existing source and loaded-layout checks, the first
key callback in a native
generation requests a separate `TF_ES_SYNC | TF_ES_READ` metadata session. It passes the key.
The session checks the focused document's exact context identity and reports the TSF view's
relationship to native focus without weakening the editing predicate. Known password, disabled
and read-only controls stop before selection. It checks disabling compartments, obtains a raw
selection range and queries the application input scope before asking whether ACP exists.
Missing, ambiguous and non-text scopes stop before ACP. It never reads document characters,
queries a boundary, plans an engine event or starts or changes a composition. Unknown hosts
receive no additional observation session. Request result, session result and callback completion
are distinct. Requests expire on return and never retry asynchronously. Lifecycle invalidation
expires cached metadata. Reentrant key callbacks abort observation through the existing generation.

Use only a fresh public test document. These commands take an explicit process ID:

```powershell
KeyrillHostInspect.exe build
KeyrillHostInspect.exe host TARGET_PID
KeyrillHostInspect.exe service TARGET_PID --arm-seconds 120
KeyrillHostInspect.exe service TARGET_PID
KeyrillHostInspect.exe service TARGET_PID --arm-seconds 120 --expect-server-build REVIEWED_SERVER_SHA256
KeyrillHostInspect.exe field TARGET_PID
KeyrillHostInspect.exe public-case TARGET_PID empty
KeyrillHostInspect.exe public-case TARGET_PID probe-g
KeyrillHostInspect.exe public-case TARGET_PID fixture-en-boundary
KeyrillHostInspect.exe public-case TARGET_PID fixture-en-corrected
```

`host` reports package version and focused native window class without reading window titles or
document text. `service --arm-seconds` creates up to 64 ephemeral pages for the target's current
threads and holds them for the specified period, capped at one hour. Arm before the maintainer
activates the source. A newly created thread needs explicit rearming and source reactivation.
The thread snapshot does not retain thread lifetime. The inspector omits only a signaled,
exited thread or an `OpenThread` failure with `ERROR_INVALID_PARAMETER`. It retains live thread
handles, requires at least one, and revalidates the held process before arming. Access denial,
owner mismatch and unexpected API errors stop capture. Content-free failures report the exact
TID, owner, Win32 error and thread/process wait results alongside the target PID and creation time.
There is no automatic snapshot refresh or retry.
The ordinary `service` command opens existing pages with read access only. A missing, busy,
stale, incompatible or inaccessible observation does not establish service inactivity.
By default, `build_match=1` compares the published `build` digest with this inspector's digest.
An updated observer can inspect a separately reviewed installed server with the service-only
`--expect-server-build` option. It requires exactly 64 hexadecimal characters, accepts either
letter case and compares canonical 32-bit words. The service header prints `observer_native_build`,
`expected_service_build` and whether that expectation was explicit. Each available publication
prints its actual `build` digest. `build_match` and successful availability then require that
actual digest to equal the expected server digest. The observer's own build command remains
unchanged, and the option does not bypass protocol, identity or payload validation.
Check the actual loaded DLL's identity separately. Registry agreement is insufficient.

`field` captures the actual foreground focus, its owning PID and TID, process and retained
foreground and focused thread creation times, root and up to eight parents. The foreground
window and focused field may belong to different threads in the same process. Both retained
threads must remain live with the same creation times, and both GUI thread queries must report
the exact focused field under the actual foreground root. Each bounded native window record contains
its class, control ID, styles and enabled status. Missing or truncated facts remain explicit.
It requires the focused UIA element's PID and native HWND to match that exact focused window,
and `CompareElements` to match `ElementFromHandle` for the window. A same-process ancestor or
windowless UIA element does not establish this binding. It reports the UIA control type,
password/enabled evidence and independent ValuePattern and TextPattern read-only observations.
It never fetches names, values, titles, document characters or selection.

`public-case` checks one compiled public document and reports only the case, verdict and refusal.
The cases are `empty` for zero UTF-16 units, `probe-g` for `g`, `fixture-en-boundary` for
`ghbdtn` followed by one Space, and `fixture-en-corrected` for `привет` followed by one Space.
The literal baseline retains its original meaning. The corrected case does not accept it.
The additional compiled cases check these exact documents. A visible `␠` denotes one Space.

| Case | Exact document |
|---|---|
| `fixture-en-word` | `ghbdtn` |
| `fixture-ru-word` | `привет` |
| `fixture-continuation` | `привет␠x` |
| `fixture-two-words` | `привет␠привет␠` |
| `manual-provisional` | `хорошо` |
| `manual-provisional-literal` | `[jhjij` |
| `manual-pc` | `ёлка` |
| `manual-pc-literal` | `` `krf␠ `` |
| `model-en` | `список␠` |
| `model-en-literal` | `cgbcjr␠` |
| `model-ru` | `world␠` |
| `model-ru-literal` | `цщкдв␠` |
| `prefix-suffix` | `left␠привет␠right` |

All require exactly one collapsed selection at the document end. The focused UIA control must
report Document type, positive enabled and non-password evidence, exact native binding and
TextPattern. Native password, read-only or disabled evidence vetoes the check. At least one
read-only channel must successfully return exact Boolean false; the other may be unavailable or
unsupported. A true, mixed or malformed observation refuses the check. Native styles never
establish positive writability.

The checker reads at most the compiled expected length plus one UTF-16 unit and compares exact
code units and length, including embedded NUL and terminal carriage return. It does not normalize
provider text. The check requires an actual returned BSTR, including an allocated empty BSTR for
`empty`. An `S_OK` result with null text returns `TextUnavailable` under this tool's contract.
It revalidates retained process/thread identity, actual native focus and bounded
native facts around provider calls, and fresh UIA identity and safety evidence before character
access, after it and before success. Observed drift refuses the check. These calls do not lock
the document atomically. Neither command changes values, selection or focus, performs editing,
or grants runtime admission. The helper module links only into the inspector and its tests.

No inspector command registers the DLL, selects an input source, changes policy, requests text
from the service or widens admission. The maintainer registers the reviewed exact DLL path with
the existing registration tool and selects the source. First confirm a fresh activation,
matching source digest and actual loaded DLL. Ordinary Notepad support still requires actual
owned-composition and physical application acceptance under the bounded contract above.

### Observation protocol

Protocol v1 uses a local-session page named by protocol, PID, process creation time and TID.
Arming refuses an existing object and gives mapping read/write rights only to the current logon
SID through an explicit protected DACL. The publisher opens only an already armed page.
An atomic nonblocking claim allows one service instance per thread page. A page-resident,
nonwrapping allocator distinguishes instances across DLL unload and reload. A colliding
instance cannot publish or release another instance's claim. Exhausted allocation or publication
sequence requires handles to drain and explicit rearming.

Every wire field is an aligned 32-bit Windows integer. Payload encoding enumerates fixed-width
fields and reserved zeros explicitly. It copies no native structs, booleans, padding, keys,
text, titles, paths, dictionary entries or absolute text ranges. The snapshot includes metadata
refusal, activation and callback counters, the existing V2 lifecycle journal and owned serial,
runtime policy/readiness and a deterministic SHA-256 digest of the Windows native sources.
Decoding validates enums and the existing service and lifecycle invariants.

Windows provides [atomic aligned 32-bit access](https://learn.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access).
Writers use [interlocked stores with a full memory barrier](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-interlockedexchange)
around an odd and even sequence. Readers use [MSVC `/volatile:ms` loads](https://learn.microsoft.com/en-us/cpp/build/reference/volatile-volatile-keyword-interpretation)
and full Windows memory barriers, then
recheck sequence, claim and immutable identity. Readers never issue an interlocked read-modify-write
against read-only pages. Three bounded attempts return Busy if coherence cannot be established.
The production tests run a separate process with an actual `PAGE_READONLY` view, plus collision,
reopen, malformed identity, exhausted identity, incomplete publication and concurrent writer schedules.
This is diagnostic evidence, not authentication against a malicious process in the same logon.

Publication happens after the outermost service callback. It copies existing scalar observations
without waiting, allocating, performing I/O or querying COM or the host. Missing diagnostics keep
the same admission and key disposition. Controlled tests compare the exact document, caret,
service snapshot and lifecycle with publication enabled and disabled. Packaged-process mapping
access and ordinary Notepad routing remain separate live gates.

## Manual conversion and reversal

In an admitted owned word, left Ctrl + left Alt + physical K asks the shared Rust session for
manual conversion. Left Ctrl + left Alt + physical Z reverses the current owned receipt.
Caps Lock is allowed. Shift, either Windows key, right Ctrl, and right Alt reject these chords.
AltGr does not invoke a command. The shortcuts are local input-method keys, not global hotkeys.
Backspace edits owned text only without Shift. Escape restores and finishes an owned receipt,
or finishes the word unchanged and passes through when no receipt exists.

Ctrl, Alt, Shift, and Windows modifier downs pass after the existing profile and loaded-layout
checks, without requesting an edit session or finishing the mark. Modifier keyups pass too.
An unmatched completed shortcut finishes a still-owned word and passes the original key.
The translator accepts bounded printable UTF-16 from the verified EN or RU substitute layout
using `ToUnicodeEx` without changing its keyboard state. Rust decides whether punctuation is
provisional word text or a finish request. PC backtick can therefore start `ёлка` as `` `krf ``,
and `[jhjij` can convert to `хорошо`. Coalesced repeats remain finish-and-pass requests.

Commands use the same owned composition, exact range/text/caret checks, readback, and Rust
acknowledgement as fixture correction. A successful acknowledged command can consume its key
without writing, including manual conversion with no word or reversal with no receipt.
Admission failure, denied edit sessions, and refusal before a write pass the original key.
An attempted unknown mark consumes without replay. An unknown finish passes an ordinary next
key once and retains a reserved command's consumed disposition. Unknown effects never trigger
a repair or another transport.

Controlled tests drive `OnKeyDown` and `OnKeyUp` through complete modifier sequences using
thread-local `SetKeyboardState`. They check whole A and B documents, prefix/suffix preservation,
the caret, composition identity, command expiry, denied and failed requests, extra modifiers,
Shift transitions, exclusions, reentry, and unknown effects. Actual `ToUnicodeEx` checks cover
PC OEM keys with Shift and Caps Lock in each already loaded supported layout. These are
controlled observations, not physical keyboard or live TSF routing evidence. Separate automated
real-editor runs passed the manual commands and PC printable-key cases described below.

## Shared model and personal vocabulary

Windows captures a complete configuration at activation. `KEYRILL_WINDOWS_POLICY` accepts
`off`, `fixture`, or `model`. An absent value preserves the fixture proof default. Invalid
configuration retains the service's previous complete configuration; its first invalid
configuration uses off. Off disables inference while preserving manual conversion and reversal.

`KEYRILL_WINDOWS_EN_DICTIONARY` and `KEYRILL_WINDOWS_RU_DICTIONARY` accept absolute local
paths. Defaults are `%USERPROFILE%\vimfiles\spell\en.utf-8.add` and `ru.utf-8.add`. A genuinely
absent default initially supplies empty protection. An explicit absent source remains missing,
so the shared model abstains until both sources are ready. A real empty file is ready after
settling. Invalid or disappearing files retain their last good snapshot for that activation.

The acquisition worker reads at most 1 MiB per source. It admits local fixed NTFS volumes,
opens each component relative to an OS volume handle, and rejects reparse points, offline
files, remote paths, and uncertain metadata before reading contents. Final-file replacement
is allowed; changed identity or observed writes discard the candidate. Two equal observations
are a settling heuristic. They cannot prove an editor has finished saving.

Rust parses the existing Vim format on the worker, including skipped invalid lines and flags.
The TSF thread adopts both immutable snapshots outside callbacks. Reloads preserve the current
word and reversal receipt. Model preparation also runs on the worker; activation remains
manual-only until the prepared policy can be enabled while idle. Workers hold a DLL reference
until their callback returns. Deactivation signals cancellation without joining the worker.

The proof editor exposes content-free readiness in control 111. `KeyrillProofInspect PID runtime`
reads requested and effective policy, revision, source states, and numeric errors.
`model-ready` requires the prepared model and both ready or retained sources in an active
service with a known EN or RU mode. Document-and-caret oracles include `manual`,
`manual-reversed`, `manual-provisional`,
`manual-pc`, `model`, `model-ru`, `model-reversed`, `model-ru-reversed`, and `vocabulary`.
`model-ru-reversed` requires exact A=`цщкдв `, empty B, and A's collapsed end caret.
They read only A and B.
These tools provide test evidence; UI Automation grants no editing authority to the adapter.

### Controlled-editor behavior acceptance

Automated key events through the live TSF proof editor passed the following exact UIA checks.
Each check compared all of A, required empty B, and required a collapsed caret at A's end.
The running editor loaded the frozen adjacent DLL whose hash matched the tested build.

| Input or change | Verified result in A |
|---|---|
| EN `ghbdtn`, then left Ctrl + left Alt + K | `привет` without Space |
| Left Ctrl + left Alt + Z on that owned receipt | `ghbdtn` without Space |
| EN `cgbcjr` then Space under model policy | `список ` |
| Settled EN vocabulary reload, then reversal of that receipt | `cgbcjr ` in the same owned composition |
| EN vocabulary protects `cgbcjr`, then a newly typed word | `cgbcjr ` |
| Malformed vocabulary reload, then a newly typed word | `cgbcjr ` with the last good protection retained |
| Restore empty vocabulary, then a newly typed word | `список ` |
| RU vocabulary forbids `список`, then EN `cgbcjr` and Space | `cgbcjr ` |
| EN `[jhjij`, then manual conversion | `хорошо` |
| EN PC `` `krf ``, then manual conversion | `ёлка` |
| RU `цщкдв` then Space under model policy | `world ` |
| Reversal of that RU receipt | `цщкдв ` in the same owned composition |

Both model directions used ready dictionary snapshots and the shared model, with no runtime
errors. Each manual command and reversal added one verified write. Vocabulary publication
preserved the existing mark and receipt without a document write. All recorded cases had zero
unknown edit effects. Tests used only public synthetic vocabulary in private fixture files.

These runs used agent-generated keys. They add real TSF routing and document evidence without
adding physical keyboard evidence. Admission still covers only the proof editor. General
application support, settings UI, and packaging remain unproved. Controlled worker tests and
actual DLL lifetime checks passed under Windows AddressSanitizer. Pending I/O completion in
those tests was simulated, so they do not establish actual kernel-pending I/O behavior.

## Editing authority

One STA service owns the engine, generation, and optional mark. An edit request retains its
context and completion result. It expires as soon as the synchronous request returns. No
asynchronous editing work, cached cookie, retry, or key replay is allowed. Admission requires both the
native focused HWND and the thread manager's current top context to match the captured target.

Proof admission requires the named proof executable and root window class, writable controls
101 and 102, a known singleton `IS_DEFAULT` or `IS_TEXT` input scope, and non-disabled context and
thread compartments. The separate version-bounded Notepad main-field contract is described
above. Secure-mode activation is refused. Read-only, password, sensitive, uncertain,
foreign-composed, and unknown controls get no text read and no composition. The proof
executable-name restriction is a support policy, not a security boundary or approval for
arbitrary RichEdit hosts.

Input scope comes from the context owner's read-only application property through
[`ITfContext::GetAppProperty`](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getappproperty).
Explicit ordinary scope requires `S_OK`, a non-null property, and `S_OK` from
[`ITfReadOnlyProperty::GetValue`](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue)
with a non-null `ITfInputScope` value. Its
[`GetInputScopes`](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nf-inputscope-itfinputscope-getinputscopes)
must return `S_OK`, a non-null array, and exactly one `IS_DEFAULT` or `IS_TEXT`. `S_FALSE` from
either property call refuses admission, including a valid-looking value returned with that status.
Missing, private, password, unknown, mixed, and multiple scopes stay pass-through before text
reads or composition starts. Only the compiled Notepad main field may use the exact
`NotepadUnavailableValue` signature described above. Controlled application-property tests do
not establish live RichEdit admission.

The fixture declares `IS_TEXT` for writable A and B. Control 103 retains its read-only style and
default scope, and control 104 retains its password style and password scope. A native metadata
probe on empty stock RichEdit A observed `S_OK` for explicit `IS_TEXT` setup, the application
getter, range value, interface query, and scope retrieval. The value was `VT_UNKNOWN` and the
array contained exactly one `IS_TEXT`, with matching context, range, native focus, and a granted
synchronous read cookie. An explicit one-element `IS_DEFAULT` list still produced an unavailable
value both empty and after public fixture letter `g`, including a fresh read. Default removes the
scope association. A later fresh launch with explicit `IS_TEXT` returned `E_FAIL` from the range
value getter despite `S_OK` from setup and valid context, range, view, and synchronous edit-session
checks. Explicit `IS_TEXT` can supply admissible metadata but does not guarantee its availability.
Missing metadata still refuses admission. Neither observation establishes an owned composition,
verified correction, or physical acceptance.

Both fixture window procedures attempt `IS_DEFAULT` for every existing known field during parent
`WM_DESTROY`, while the children still exist on their owning thread. Partial setup failure uses
the same teardown. A failed or throwing reset does not prevent later children from being attempted.
The helper returns the first non-success result and does not retry after destruction. This is an
attempt to remove metadata under the
[`SetInputScope` lifecycle contract](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nf-inputscope-setinputscope),
not a document or removal oracle. Controlled tests observe actual child validity, thread ownership,
and reset order during normal and partial teardown.

An owned mark retains canonical context and composition identities, exact ACP extent, and
bounded verified UTF-16 text. Each update checks active composition membership, generation,
expected text, returned extent, and caret. It verifies the returned range, composition range,
text, and caret after the one permitted `SetText` call. Each MARK or FINISH that reaches
replacement performs one exact owned-range `SetText`, including an unchanged finish.
`EndComposition` removes the composing property and is a separate effect, not a second character
write. Unchanged finish requires no expected end caret and does not force selection collapse.
Changing finish retains its caret checks and verified end-caret update. Finish then checks the
retained exact range's text and extent and verifies that no composition remains before
acknowledging Rust. A replan
reacquires the actual caret and boundary, rather than inheriting the finished mark's boundary.
FINISH followed by a replanned MARK may perform two character writes in one request, each in a
distinct plan and composition. Controlled operation traces attribute both writes by request
ordinal, composition-start serial, operation, and exact span.

If scope or writability excludes the field after an attempted character write, the guard refuses
further character reads. The result is Unknown, correction stops, and the executor performs no replan,
replay, repair, or second character write. A pre-effect refusal follows the existing key
disposition rules. Fresh policy checks do not change the owned-composition transport or make
native success a document oracle.

A refusal after creating an empty owned composition but before `SetText` retains the existing
`KR_REFUSED_BEFORE_WRITE` classification. Composition creation is already a native effect.
The service stops for the activation and performs no further character read or editing effect.
It never replays or repairs the input.
It does not resume ownership on later input.

Request HRESULT, session HRESULT, and actual callback completion all affect key disposition.
A refused edit passes the key. An unknown MARK consumes the possibly inserted key. An unknown
FINISH passes an ordinary next key without replanning, but keeps a reserved command consumed.
Unknown effects stop correction for the activation. No guessed cleanup or committed-range
replacement follows. A nested key invalidates the outer request and passes unchanged.

Each key validates the active TSF profile's exact type, service, profile, language, active flag,
and selected mode before querying `ITfInputProcessorProfileSubstituteLayout` on its captured
profile manager. `GetSubstituteKeyboardLayout` must return `S_OK` and a non-null canonical EN
or RU layout for that exact profile. The active profile's `hkl` and `hklSubstitute` fields do not
supply substitute authority. The independently returned layout must be an exact member of a
complete loaded-layout observation bounded to 64 entries, with matching counts before, during,
and after enumeration. No layout is loaded or activated on this path.

Generation checks surround native queries. Local COM references keep the queried objects alive,
and a separate key-admission guard makes nested keys invalidate and pass through before event
handling. The guard releases before `HandleEvent`, whose existing busy guard owns edit-session
reentry. Diagnostic callback depth supplies no editing authority. Controlled tests inject profile
and loaded-layout dependencies per service instance and exercise actual `OnKeyDown`; they do not
establish physical TSF routing or document acceptance.

Translation uses the validated layout with the current Shift and Caps Lock state and
`ToUnicodeEx`'s no-state-change flag. Synthetic TIP handles are not treated as keyboard layouts.
Only the stable EN and RU proof profiles are accepted. An unsupported profile or layout
invalidates authority and passes without a guessed finish. Unmatched nonmodifier shortcuts,
dead keys, and coalesced repeat messages request a verified owned finish before passing through.

Focus loss, source changes, context changes, and teardown invalidate authority. If a mark
exists, these paths stop the activation without writing it back. A termination callback clears
only its exact captured composition identity. Returning A to B to A never restores old editing
authority. The final fixture run observed exact native termination on each mouse focus change
before invalidation, alongside the expected whole documents and caret. This does not admit
other clients or establish Undo behavior. A later RU source-selection run preserved the public
literal and caret, but its mark had already reached FinishVerified before profile change. That
run does not isolate termination caused by the source-change callback.

## Maintainer registration and selection

Do not register this draft as a daily input method. The controlled-editor proof has passed;
general application reliability is not established. Installation, enablement, source selection,
and permission changes belong to the maintainer. The default build and controlled tests never
perform them. The maintainer-launched native source experiment below explicitly selects RU.

Run status before a change. It prints content-free COM and TSF profile facts without mutation.
Profile presence requires an exact TSF enumeration match and the expected proof description.
An enumeration or description API error reports unavailable, rather than absent. Only a completed
enumeration without a match establishes absence. Registry inspection errors also report unavailable.

```powershell
$bin = Resolve-Path target/windows-check/Release
& "$bin/KeyrillRegister.exe" status
& "$bin/KeyrillRegister.exe" check
```

`check` runs the same read-only, complete EN/RU presence preflight that guards enablement.
It exits unsuccessfully if either profile is absent or cannot be inspected. The utility makes no
enablement calls until both profiles pass. After enablement calls, it checks both profiles again
and reads their enabled state before reporting success. A failed readback stops without rollback
or retry; an earlier profile's enablement may already have changed.

After accepting the draft's limits, the maintainer can explicitly register it.

```powershell
& "$bin/KeyrillRegister.exe" register "$bin/KeyrillTsf.dll"
& "$bin/KeyrillRegister.exe" status
& "$bin/KeyrillRegister.exe" check
& "$bin/KeyrillRegister.exe" enable
& "$bin/KeyrillRegister.exe" status
& "$bin/KeyrillProofEditor.exe"
```

Run these commands individually and stop if one fails. Preserve the static stage and HRESULT
from the failure before choosing another registration action.

COM registration writes only the service's `HKCU/Software/Classes/CLSID` key. The utility uses
TSF's profile and category APIs separately. The successful maintainer registration does not
establish ordinary-user registration scope. Each registration and enablement API boundary reports its stage,
static EN/RU label where applicable, and HRESULT, without DLL paths or queried text.
A failure may leave partial registration. A generic HRESULT alone does not establish its cause.
Stop on failure. Do not run enablement to repair incomplete registration. Inspect status before
deciding on cleanup or a diagnostic retry. There is no automatic
elevation or machine-wide fallback. Profile registration, explicit enablement, active source
selection, and service activation are distinct steps. The utility never selects a source.
Its explicit registration command loads the standard substitute layouts without activating them.

Select **Keyrill proof EN** in the proof editor through Windows' input-source UI. For RU checks,
select **Keyrill proof RU**. If either profile is absent or cannot be selected, record the live
gate as blocked. Do not fake a client ID, direct-call the key sink, or substitute an input hook.
Close the proof editor before removing the registration.

```powershell
& "$bin/KeyrillRegister.exe" disable
& "$bin/KeyrillRegister.exe" unregister
& "$bin/KeyrillRegister.exe" status
```

## Physical acceptance still required

Use only public synthetic fixture strings. Start with both writable documents empty, focus A,
select EN, and type physical `ghbdtn` and Space. A must contain exactly `привет ` with a collapsed
caret at UTF-16 offset 7. B must remain empty. Record whole-document and caret assertions before
and after each scenario. TSF composition identity and per-key consumption require native TSF
debugger observation; UIA cannot prove either, the origin of a key, or installed-service routing.

The inspector accepts only a proof editor PID and a named fixture oracle. It refuses other
executables and never inspects excluded controls 103 and 104. It reads A and B through UIA,
checks exact known synthetic text and the collapsed end caret, and prints only pass or fail.
It exposes no replacement, selection, keystroke, clipboard, or generic text-dump operation.

```powershell
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID correction
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID reversal
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID continuation
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID focus
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID empty
```

`correction` expects `привет ` in A, `reversal` expects `ghbdtn `, and `continuation` expects
`привет x`. Those cases expect B empty. The `focus` oracle expects `привет ghbdtn ` in A with
the collapsed end caret and exactly `x` in B. It checks that returning to A does not resume
correction across a committed partial word. This fixture's RichEdit UIA Value includes one final carriage
return beyond the literal document content. The inspector compares the expected string plus
exactly that one marker; it does not trim whitespace or ignore an extra line break.

The real provider passes all four named oracles with literal fixture data. The observer rejects
a moved caret, a noncollapsed selection, an extra space, an extra line break, and a changed B
document. Restoring the expected text and caret makes it pass again. No excluded control is
queried by the inspector. Entering a target string directly calibrates the observer; it does not
prove TSF correction, key consumption, or Undo.

### Observe the host routing boundary

The diagnostic editor adds noneditable `STATIC` control 105 outside the tab order. Its initial
Name starts with `Keyrill host routing v1`. The editor STA records completed DOWN and SYSDOWN
routing outcomes in memory. A 250 ms timer publishes them only from the outer message loop,
after routing returns. Nested window-procedure timer dispatch does no diagnostic work.
Control 105 reports only host routing. Control 106 separately reports the actual service when
the reviewed diagnostic DLL is loaded through the registered profile and proof manifest.

```powershell
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID routing
```

This mode verifies the fixture executable and root class, then reads only control 105's UIA
Name. It never reads A, B, or the excluded controls. A bounded parser rejects unknown versions,
fields, malformed values, and oversized status. Output reconstructs known fields rather than
printing the queried Name. Missing or rejected status exits unsuccessfully.

Read a baseline after source selection and fixture preparation. After one physical fixture
interval, wait for `publication` to advance and read again. An unchanged publication is stale
and cannot establish a zero delta. The publication sequence and every counter saturate at
4294967295. `saturated=1` means deltas are unavailable. There is no reset operation.

| Field | Host observation |
|---|---|
| `publication` | Outer-loop status publication sequence, including quiet intervals |
| `down` | DOWN and SYSDOWN messages reaching the observed outer loop |
| `test_error` | Failed `ITfKeystrokeMgr::TestKeyDown` calls |
| `no_interest` | Successful tests with a false interest output |
| `dispatch` | `ITfKeystrokeMgr::KeyDown` calls after successful test interest |
| `dispatch_error` | Failed dispatch calls, regardless of the eaten output |
| `passed` | Successful dispatch calls with a false eaten output |
| `eaten` | Successful dispatch calls with a true eaten output |
| `first_error` | First failed host call and hexadecimal HRESULT, or `none` |

These are process-lifetime host outcomes, including source-switch shortcuts, not physical-key
counts or service-callback counts. A failure already present at baseline cannot be attributed
to the later fixture interval. No interest cannot distinguish a missing sink from an inactive,
stopped, or mode-zero service. Successful dispatch does not identify a service or refusal reason.
Consumption does not prove an owned composition or document change. Run the document oracle
separately and use one fresh interval to choose the next diagnostic boundary.

### Observe the actual service refusal

The diagnostic editor polls its current thread manager with `GetFunctionProvider(service_id)`
from the same outer-loop timer. It never constructs a service to obtain diagnostics. The service
registers its `ITfFunctionProvider` through `ITfSourceSingle` only in `KeyrillProofEditor.exe`.
Registration is best effort and does not change activation success or editing eligibility.
The v2 private interface copies one fixed native scalar snapshot with an exact size and version.
It contains the frozen v1 payload and a bounded lifecycle journal. The v1 reader keeps its
original IID, layout, and copy contract so an old editor can still read a new DLL.
It exports no document data, native identifiers, offsets, keys, text, pointer values, or cookies.

```powershell
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID service
```

This command reads only noneditable `STATIC` control 106. Its bounded typed codec rejects
unknown versions, fields, enum labels, malformed numbers, inconsistent status, and oversized
input. The inspector reconstructs accepted fields and makes one attempt. A valid unavailable
publication is printed as such. It does not establish service callbacks or physical correction.

`state=Available` means the current poll copied valid data from the registered provider.
`Missing` means TSF returned `TF_E_NOPROVIDER`. Other discovery failures, a successful null
provider, failed or malformed reads, and exceptions report `Error`. An active callback reports
`Busy` with no snapshot. A provider without the v2 interface reports `UnsupportedVersion`,
never an available empty lifecycle journal. A retained interface after deactivation reports `Inactive`. Each poll
replaces the preceding publication, including successful data. There is no cached success.
The service's existing editing `busy_` flag and a scalar callback-depth guard prevent incomplete
reads. The depth guard affects only diagnostic availability and performs no native query.

Read a baseline after fixture preparation, then read again after one physical interval and
advancing `publication`. One UIA sample alone does not prove freshness. The editor retains the
provider's canonical `IUnknown` locally. It exports only a host-local `source` sequence.
`continuity=Reset` begins a new baseline when provider identity or `activation` changes, or
availability recovers. `Same` permits comparison with the previous available baseline.
Editing generation changes do not end diagnostic activation continuity. Every counter saturates
at 4294967295. Saturation is explicit, and `Indeterminate` forbids interval deltas. Activation
serial saturation also stops new diagnostic registration, since an old activation cannot be
distinguished safely. The editor releases its retained identity before COM uninitialization.

`TestEntered`, `Interested`, `KeyEntered`, and `Consumed` count actual service callbacks and their
existing outputs. `HandleEntered`, `Requested`, `SessionEntered`, and `SessionCompleted` separate
handling from lock requests and completed callbacks. Present request and session HRESULTs belong
to the most recent returned request. `Attempted`, `Verified`, `Refused`, and `Unknown` report the
existing write and acknowledgement path, rather than inferring edits from native success.
The named stage fields count refusals. `first` and `latest` give presence, stage, cause, and the
native HRESULT for `Native` causes. A successful `S_FALSE` rejection remains a native result.
A failed predicate after successful calls has its named stage and a zero result.

Counters and first/latest refusal are service-instance lifetime history. They survive
reactivation, while `activation` advances and the host resets its interval baseline. A first
refusal from an earlier activation is not evidence for a later physical interval. `active`,
`stopped`, and `mode` describe the copied state, so a stopped active service remains inspectable.
All recording inside key and edit callbacks changes fixed scalar fields only. It adds no native
queries, allocation, formatting, document writes, or key-disposition decisions. Teardown detaches
diagnostic ownership before cleanup. Failed, throwing, or reentrant partial registration gets
one captured-source cleanup attempt without retrying an unknown operation.

### Observe owned-composition lifecycle

Control 106 publishes `Keyrill service v2` through the same poller, source identity, activation,
and timer as the refusal counters. The new inspector rejects v1 publications. Each lifecycle
record has `sequence/kind/subject/cause/unresolved` scalar fields. The fixed window contains
16 records in sequence order, followed by exactly zero unused entries. `event_count` identifies
the used entries, and `event_sequence` identifies the latest recorded event.

`owned_present` and `owned_serial` derive from the service's current `owned_` object.
`mark_allocated` is a local monotonic ownership label, never a native address or document ID.
A serial labels the ownership established by the service's existing canonical identity query.
It does not independently prove that TSF still exposes a live composition.

| Kind | Value | Observation |
|---|---|---|
| Acquired | 1 | Existing native creation and canonical identity checks installed ownership |
| EndRequested | 2 | The existing ordinary `EndComposition` call is about to run |
| FinishVerified | 3 | Existing post-end checks, local ownership reset, and final generation check succeeded |
| ExactTermination | 4 | The callback matched the existing canonical owned identity |
| DifferentTermination | 5 | The callback differed from the current owned identity, whose serial is the subject |
| TerminationWithoutOwned | 6 | A non-null callback arrived without local ownership, with no identity query |
| TerminationUnresolved | 7 | Null argument, failed identity query, stale state, or exception prevented classification |
| LifecycleInvalidation | 8 | The existing lifecycle path invalidated authority, retaining ownership if present |
| Detached | 9 | Teardown moved ownership out of the service, without claiming native termination |

The cause field is nonzero only for LifecycleInvalidation. Values 1 through 8 mean foreground
loss, document focus, document uninitialization, context push, context pop, profile change,
key admission, and deactivation. The unresolved field is nonzero only for TerminationUnresolved.
Values 1 through 4 mean null argument, failed identity query, stale generation or ownership,
and exception. Other event fields that do not apply are zero.

Compare two Available v2 publications only when publication advances, source and activation
match, the later continuity is Same, and no host, legacy, or lifecycle saturation is present.
Every sequence after the baseline must remain in the later window. Window overrun makes that
interval indeterminate. Sequence and mark allocation saturate without wrapping or changing
editing. If mark allocation is exhausted, ownership can remain present with unusable serial zero.

For a fresh eligible letter, observe Acquired and the same current owned serial. For ordinary
continuation after a retained boundary, observe EndRequested, FinishVerified, then Acquired with
a new serial. For focus, source, and teardown cases, report the actual ordered events without
assuming which callback Windows must deliver. An exact callback establishes its identity check.
An invalidation with a retained serial establishes local retention. A missing event proves neither
native cleanup nor a termination defect. Always run the separate whole-document and caret oracle.

The journal adds no native observation calls. Diagnostic callback guards remain active through
native releases, exception handling, and teardown. They affect read availability only. The bounded
journal stays in memory and never contains text, offsets, keys, pointers, application identifiers,
or paths. No diagnostic record authorizes a write or repairs an unknown edit.

### Repeatable native lock denial

The proof editor selects one immutable startup mode. Start a fresh instance with
`KeyrillProofEditor.exe --deny-tsf-locks`. Before showing the window, it sets `SES_CTFNOLOCK`
on its own writable A control and requires the setter result to match the immediate
`EM_GETEDITSTYLE` readback with that bit set. Uncertain setup exits with code 1 through the
existing teardown. The distinct window caption and noneditable `STATIC` control 107 report
confirmed setup. This is a fixture condition, not an input-method setting or an editing fallback.
Starting without arguments keeps the ordinary fixture behavior. Unknown, duplicate, or extra
arguments exit with code 2 before COM startup.

With A and B initially empty and the reviewed service selected in EN by the maintainer, type
physical `ghbdtn` and Space into A, then run the read-only oracle.

```powershell
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID denial
```

This oracle requires the known setup confirmation, exactly `ghbdtn ` with a collapsed end
caret in A, and empty B. It checks the documents before and after a single 600 ms CLI wait.
Two service publications must advance with the same provider source and activation, both
Available, active, unstopped, EN, and unsaturated. The later publication must retain `Same`
continuity. Both must show positive `Requested`, either a failed native request HRESULT or an
`S_OK` request with a present failed session HRESULT, and a matching latest native refusal.
`SessionEntered`, `SessionCompleted`, `Attempted`, `Verified`, `Unknown`, and `Consumed` must
all remain zero. Missing providers, paused publication, successful sessions, wrong source modes,
and unchanged text alone cannot pass. The wait observes outer-loop publications outside native
input callbacks and never retries an edit. Output contains only the result, without queried text.

Controlled pure schedules cover these acceptance and rejection cases without sleeps. A pass
establishes this fixture's observed native refusal and literal document state. It does not prove
key origin, composition, correction, Undo, or other applications. A later physical run of this
promoted mode passed the strict native-denial and document/caret oracle.

### Observe an owned mark through shutdown

The host-only `--prove-owned-shutdown` mode retains the actual provider's v2 reader through
ordinary window destruction and thread-manager deactivation. An automated real-editor run in RU
mode passed `shutdown-ready` with the exact public partial word, empty B, collapsed end caret,
and an active owned serial. Clicking the title-bar Close button returned exit 0 through a retained
process handle. A separate fresh empty editor passed `empty`, rejected `shutdown-ready`, and
returned exit 16 (`NoOwnedMark`) after the same Close action. Those first runs used agent-generated
key and mouse inputs. Later physical owned-shutdown and fresh empty-close controls passed the
same bounded assertions. An earlier ready-then-Close attempt returned 16 with an unclassified
precondition loss. The attempted Close cancellation actually closed the window, so that attempt
supplies no cancellation evidence.
The mode changes no service editing policy, ABI, or journal capacity.
It cannot be combined with `--deny-tsf-locks`. Unknown, repeated, or combined arguments exit with
code 2 before COM initialization.

Run the following procedure in a fresh proof editor.

1. Start `KeyrillProofEditor.exe --prove-owned-shutdown`. Confirm the caption
   **Keyrill TSF owned shutdown fixture**. The immutable `STATIC` control 108 identifies this mode.
2. Select the existing Keyrill source through the maintainer's input-source UI. Read the actual
   service mode with the `service` inspector case and wait for an Available publication.
3. Before typing, start a separate background observer that retains the process handle and
   polls `KeyrillProofInspect.exe FIXTURE_PID shutdown-ready`. Keep that observer running without
   switching away from the editor to read or acknowledge its result.
4. With A and B empty, type physical `ghbdtn` in EN or the physical `hello` keys in RU. Do not add
   Space. Keep the editor focused for five seconds so the observer can record the ready oracle.
   It checks exactly `ghbdtn` in EN or `руддщ` in RU, empty B,
   and A's collapsed end caret, together with the exact shutdown caption and mode marker.
   It selects the literal from the observed service mode and accepts no arbitrary expected text.
5. Click the real title-bar **Close** button before returning to chat or a terminal. Require the
   observer's recorded ready result and process exit code zero. A missing ready observation or
   nonzero exit cannot pass. Record which source mode supplied the physical keys.
6. Repeat in a separate fresh process with empty A and B. Verify the `empty` oracle, retain the
   process handle, and click **Close**. This empty-close control must exit nonzero.

The content-free exit code preserves the rejection reason after the window disappears.

| Code | Shutdown result |
|---|---|
| 0 | Verified |
| 1 | Host initialization, setup, or message-pump failure |
| 2 | Invalid startup arguments |
| 10 | Missing or repeated close capture |
| 11 | Provider missing after binding |
| 12 | Reader busy |
| 13 | Invalid, failed, stopped, saturated, or contradictory evidence |
| 14 | Provider or reader identity changed |
| 15 | Activation changed |
| 16 | No owned mark at capture |
| 17 | Complete close interval not retained |
| 18 | Unknown effect present |
| 19 | Required new terminal before deactivation absent |
| 20 | Detached event in the close interval |
| 21 | Thread-manager deactivation did not return `S_OK` |

The observer must not substitute Alt+F4 for the title-bar click. That shortcut can finish the mark
before `WM_CLOSE`. The document oracle records the state before close. It cannot verify final
document text after the editor disappears, key origin, or complete callback drainage.

The outer timer binds canonical identities for the current provider and its returned reader.
Both must identify the same actual Service. Once bound, missing or changed identity, activation
change, busy reads, malformed evidence, and saturation reject the run without rearming.
The host's actual window procedure makes a fresh provider identity check and retained-reader
copy on `WM_CLOSE`, before `DefWindowProcW` performs normal destruction. This placement covers
sent close messages dispatched inside the message pump. A timer snapshot never supplies the
close baseline. Reentrant or duplicate close cannot replace the baseline or clear rejection.

Capture requires an active, unstopped service with zero Unknown count and a currently owned,
nonzero, unsaturated serial. After ordinary destruction and actual `ITfThreadMgr::Deactivate`
returning `S_OK`, the probe copies the same retained reader directly. It performs no provider
lookup after destruction. The endpoint must be inactive with the same activation, unchanged
allocation, cleared ownership, zero Unknown count, and no saturation. Its stopped flag may be set.

Every event since capture must fit the fixed 16-event journal, and retained overlap must match
exactly. Counters must not decrease. A new ExactTermination or FinishVerified must name the
captured serial before the first new Deactivate invalidation. FinishVerified also requires a new
preceding EndRequested for that serial. No new Acquired, Detached, or TerminationUnresolved event
is allowed. DifferentTermination and TerminationWithoutOwned do not establish completion, but
may accompany a valid exact terminal. Old terminal records and detached-only teardown cannot pass.
The captured ownership fact does not require the older Acquired event to remain in the journal.

The probe releases its retained references while the COM apartment is alive and caches the
result. Those references deliberately extend Service and Rust engine lifetime. A pass establishes
termination of the captured owned mark before service deactivation and a valid inactive endpoint.
It does not establish unretained shutdown behavior, engine destruction before reference release,
or that all native callbacks have drained. Missing close capture, empty ownership, setup failure,
message-pump failure, and lost history all remain unsuccessful. The observer never forces a
composition end, repairs an edit, or retries an unknown operation.

Controlled tests cover the rejection cases, exact and verified-finish schedules on the actual
Service, both release orders for frozen v1 and v2 readers, and the actual host window procedure
with a hidden test window. The lifetime cases require Service object accounting to drain only
after the final retained reader releases and run under AddressSanitizer. These checks do not
replace the fresh physical positive and empty-close runs above.

### Invoke native source selection with a freshly captured owned mark

The host-only `--prove-owned-en-to-ru` experiment passed its native and independent no-Space
UIA assertions in the real proof editor. It changes no Service editing policy, Rust code, ABI,
profile registration, or journal capacity. The maintainer alone launches this mode because it
selects a source. Default verification exercises controlled
COM doubles and never launches this mode. There are no target arguments, and repeated or
combined flags exit 2 before COM initialization.

The earlier source UI workflow passed its bounded normal-flow checks. Its journal recorded
Acquired, EndRequested, FinishVerified, document-focus notifications, and then ProfileChanged.
The maintainer selected RU, and the document/caret oracle passed after
agent-generated typing and Space. That sequence completed the mark before profile notification.
That workflow does not establish the stronger native experiment below.

In the native experiment, the maintainer launched the fixture and selected EN with empty fields.
Agent-generated keys formed the owned word. One `ChangeCurrentLanguage` call reached the exact
Keyrill RU profile, so no `ActivateProfile` call was needed. The same service recorded
ExactTermination for the captured serial before ProfileChanged, with unchanged input counters
and zero Unknown effects. Before any further input, the independent inspector confirmed the
unchanged public word, empty B, collapsed end caret, frozen native result, and advancing RU
publications. A separate later Space case passed without another correction write. The result
establishes this observed transition, not every activation path or callback order.

Run the primary experiment without any post-operation input.

1. The maintainer starts `KeyrillProofEditor.exe --prove-owned-en-to-ru` from the reviewed build.
   Confirm the **Keyrill TSF owned source fixture** caption. `STATIC` control 109 identifies the
   immutable mode, and control 110 publishes the native result.
2. Select the already registered and enabled **Keyrill proof EN** profile. Start with fresh empty
   A and B. Setup focus changes are allowed before any mark allocation on the bound provider.
3. Focus A and type exactly `ghbdtn`, without Space. The complete public word starts a fixed
   two-second countdown. Leave the editor focused and send no more input.
4. After the countdown, run `KeyrillProofInspect.exe FIXTURE_PID source-activated` from the
   separate observer without clicking the editor or sending End, Space, or any other key.
   Require exact A=`ghbdtn`, empty B, and A's collapsed end caret. The inspector also requires
   the immutable mode, the frozen native Verified result, and advancing actual RU service
   publications. Native Verified alone does not pass the document oracle.
5. Record that primary result before any continuation input. A later Space or continuation
   belongs to a separate case. It must not repair a caret mismatch and then replace the original
   result. An unsuccessful run is terminal and never rearms.

The probe acquires native profile interfaces on the editor STA, then binds the actual provider
and reader by canonical identity at an empty, active, unstopped EN snapshot with zero allocations
and zero Unknown effects. An empty baseline may refresh after setup focus notifications only
while allocation remains zero on that same provider and activation. Once a mark has been
allocated, a missed or ended mark cannot return to setup. Only successive observed prefixes of
the fixed public word can reach the countdown. Focus, lifecycle, and ownership must remain
continuous after acquisition. All five input counters must remain unchanged after arming.

Readiness validates this fixture's public controls 101 and 102, their owning thread and process,
ancestry, IDs, RichEdit class, ordinary writable/non-password styles, and focus before bounded
native text/caret reads. Eligibility is checked again after reading text before querying selection.
It never reads controls 103 or 104. Native readiness is a helper gate, not an independent
document oracle or editing authority. The independent UIA client stays out of the editor STA.

The first source mutation requires a latest monotonic observation no more than 250 ms after
the fixed deadline, including time spent in preparation and the final copy. It does not slide.
Preparatory native queries and public readiness precede the final actual reader `Copy`.
That fresh snapshot is the last potentially reentrant native-object or document query before
one `ChangeCurrentLanguage(RU)` dispatch. Only a non-reentrant `GetTickCount64` observation and
local cancellation/deadline checks follow it. Scheduling still prevents a hard real-time guarantee.
It reports the actual Service's current ownership of its last verified native mark. It does not
enumerate live TSF compositions independently.

After language selection, immediate actual language/profile readbacks and a fresh same-provider
cut must remain safe. If exact Keyrill RU is already active, the probe skips redundant profile
activation and requires completed old-mark evidence. Otherwise it may call `ActivateProfile`
once for fixed Keyrill RU, with a null layout and flags zero, after confirming current language
RU. Every native query precedes the final reader copy for that second mutation too. Any issued
call must return `S_OK`. A failed HRESULT or `S_FALSE` remains a failed run even if later state
looks favorable.

Microsoft documents that zero-flag [ActivateProfile][source-activate] requires the current input
language to match. [ChangeCurrentLanguage][source-language] selects a language and requires a
calling-thread manager. Its documentation does not promise a universal thread-only effect.
The experiment uses no enable, process, session, or deferred-activation flags, and no layout
fallback. It never calls EndComposition, injects input, pumps extra messages, rolls back,
retries, or polls a failed mutation into success.

The final native result requires actual exact RU profile/language readback and the same active,
unstopped RU Service with cleared ownership. The complete first-mutation-to-final interval must
fit the existing 16-event journal, with immutable retained overlap and nondecreasing counters.
No new Acquired, Detached, TerminationUnresolved, or Deactivate event is allowed. An exact
termination of the captured serial, or a new requested-and-verified finish of that serial,
supplies terminal evidence. Foreign callbacks cannot supply it but may coexist with it.
Any change to TestEntered, Interested, KeyEntered, Consumed, or HandleEntered rejects the
source-only claim, including a reentrant key that successfully finishes the captured serial.

The strict v1 native result reports Waiting, Countdown, Running, Verified, or Failed with a
named reason. Its bounded scalars include the language/activation call counts, failed native
result, captured activation/serial, evidence sequences, terminal kind, mutation phase, and
terminal order relative to the first ProfileChanged event when present. A notification is not
required and cannot substitute for actual profile readback. The completed native report stays
frozen. The later inspector result is separate. Close cancellation is sticky, and retained COM
references release before apartment teardown.

Controlled schedules cover source call budgets, exact parameters, conditional omission,
setup/readiness failures, fresh capture, reentry and cancellation, immutable history boundaries,
input interference, and actual Service callbacks without added document writes. Native and ASan
passes establish those controlled properties only. The maintainer-launched desktop experiment
supplies the separate bounded live evidence described above.

[source-activate]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile
[source-language]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofiles-changecurrentlanguage

Controlled schedules cover exact refusal stages and native versus predicate failures, request
denial versus unentered/completed sessions, busy and incompatible reads, malformed publication,
provider absence and errors, retained inactive interfaces, independent instances, saturation,
activation continuity, and failed or reentrant registration cleanup. These are controlled COM
observations. Live evidence comes from the registered proof profiles and the separately observed
physical and automated fixture cases. Controlled schedules do not replace those cases.

| Schedule | Controlled evidence | Live fixture evidence and limits |
|---|---|---|
| `ghbdtn` plus Space and Escape | Exact whole documents, caret, owned identity, and disposition pass | Physical correction passed; automated correction and Escape reversal passed exact document/caret oracles after the caret fix |
| Rapid next letter and moved caret | Bounded finish/replan, exact prefix/suffix preservation pass | Physical rapid continuation and automated continuation passed; adversarial caret drift remains controlled evidence |
| Denied request, failed session result, missing callback, late callback, null composition | Same-service zero-write and pass-through schedules pass | Physical native lock-denial passed with literal text, exact caret, and zero consumption or writes; injected late and null outcomes remain controlled evidence |
| Range, text, caret, active-membership drift; extra foreign composition | Refusal before write passes | Live owned identity and focus notifications were observed; injected drift and foreign-composition cases remain controlled evidence |
| Reentry at exercised COM boundaries and exceptions | No new write after invalidation, outcome-derived consumption, and no retry pass | Live focus, source, and teardown callbacks were observed; adversarial reentry and exceptions remain controlled evidence |
| Unknown MARK and FINISH, reserved Escape | Stop without replay or repair passes | Unknown-effect hosts remain unadmitted |
| Focus A to B to A, late old termination | Old authority never revives in doubles | Physical focus preservation passed; automated runs recorded exact termination before focus invalidation and passed the final document/caret oracle; late old callbacks remain controlled evidence |
| Password, read-only, sensitive scope, disabled compartments, secure activation | Zero text reads and writes pass | Physical password/read-only attempts preserved A and B without additional service handling; excluded contents were not queried; other exclusion paths remain controlled evidence |
| Native Undo after correction and continuation | Not modeled as a TSF guarantee | Physical and automated Undo cases preserved the corrected word while removing the continuation; no general Undo-grouping guarantee |
| EN/RU profile and source changes, teardown | Profile-table checks, invalidation, and host-only source-probe schedules pass | Ordinary source UI flow, native EN-to-RU selection from a fresh owned mark, and bounded physical owned-shutdown/empty-close cases passed |

Only x64 MSVC build and controlled-editor evidence exists. ARM64, generic editors,
ordinary-user registration scope, and general application reliability remain open.
[ADR 007](adr/007-windows-tsf.md) remains proposed.
