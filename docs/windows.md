# Windows TSF proof

This is an x64, fixture-only Text Services Framework adapter. It links the existing Rust engine
through [the C ABI](../include/keyrill.h). It has no replacement backend, hooks, input injection,
clipboard transport, runtime network access, model policy, or personal dictionaries.

The live acceptance gate is **not passed**. After an earlier failed attempt, the maintainer
registered both profiles successfully. Fresh read-only checks report both present and enabled.
The maintainer selected **Keyrill proof EN**, and a fresh editor module snapshot contained
`KeyrillTsf.dll`. Physical `ghbdtn` plus Space remained unchanged. The correction oracle failed
and the reversal oracle passed, establishing unchanged fixture text and an end caret with B empty.
This does not establish service activation, callback entry, composition ownership, or correction.
All four named text and caret oracles pass against the real UIA provider with literal fixture
data. Controlled COM schedules and observer calibration do not establish live correction.

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
controlled schedules, C11 ABI smoke, and registration-free class-factory smoke to MSVC.
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
| `keyrill_dll_smoke.exe` | Registration-free DLL factory and Rust-link check, not TSF activation |
| `keyrill_abi_smoke.exe` | Existing C ABI behavior against its controlled document |

The controlled COM test executable has the same fixture executable name in the separate
`target/windows-check/tests/Release` directory. It supplies fake TSF interfaces to the exact
`Service::HandleEvent` and `ITfEditSession::DoEditSession` implementation used by the DLL.
Its client ID and cookie belong only to those doubles. They never establish live routing.

## Editing authority

One STA service owns the engine, generation, and optional mark. An edit request retains its
context and completion result. It expires as soon as the synchronous request returns. No
asynchronous work, cached cookie, retry, or key replay is allowed. Admission requires both the
native focused HWND and the thread manager's current top context to match the captured target.

Admission is restricted to the named proof executable and root window class, writable controls
101 and 102, a known singleton `IS_DEFAULT` or `IS_TEXT` input scope, and non-disabled context and
thread compartments. Secure-mode activation is refused. Read-only, password, sensitive, uncertain,
foreign-composed, and unknown
controls get no text read and no composition. This executable-name restriction is a proof
support policy, not a security boundary or approval for arbitrary RichEdit hosts.

Input scope comes from the context owner's read-only application property through
[`ITfContext::GetAppProperty`](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-getappproperty).
Admission requires `S_OK`, a non-null property, and `S_OK` from
[`ITfReadOnlyProperty::GetValue`](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue)
with a non-null `ITfInputScope` value. Its
[`GetInputScopes`](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nf-inputscope-itfinputscope-getinputscopes)
must return `S_OK`, a non-null array, and exactly one `IS_DEFAULT` or `IS_TEXT`. `S_FALSE` from
either property call refuses admission, including a valid-looking value returned with that status.
Missing, private, password, unknown, mixed, and multiple scopes stay pass-through before text
reads or composition starts. Controlled application-property tests do not establish live RichEdit
admission.

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
text, and caret after the one permitted `SetText` call. Unchanged finish calls `EndComposition`
without a redundant text write or caret requirement. It then checks the retained exact range's
text and extent and verifies that no composition remains before acknowledging Rust. A replan
reacquires the actual caret and boundary, rather than inheriting the finished mark's boundary.

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
invalidates authority and passes without a guessed finish. Shortcuts, dead keys, and coalesced
repeat messages request a verified owned finish before passing through.

Focus loss, source changes, context changes, and teardown invalidate authority. **The service
does not yet prove clean termination of a live RichEdit mark on these transitions.** If a mark
exists, these paths stop the activation without writing it back. A termination callback clears
only its exact captured composition identity. Returning A to B to A never restores old editing
authority. This is containment, not evidence that focus changes preserve input or Undo.

## Maintainer registration and selection

Do not register this draft as a daily input method. First review the DLL and its closed live
gate. Installation, enablement, source selection, and permission changes belong to the maintainer.
The build and tests never perform them.

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
& "$bin/KeyrillProofInspect.exe" FIXTURE_PID empty
```

`correction` expects `привет ` in A, `reversal` expects `ghbdtn `, and `continuation` expects
`привет x`. All expect B empty. This fixture's RichEdit UIA Value includes one final carriage
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
the reviewed diagnostic DLL is registered and loaded.

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
The private interface copies a fixed native scalar snapshot with an exact size and version.
It exports no document data, identifiers, offsets, keys, text, pointer values, or cookies.

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
`Busy` with no snapshot. A retained interface after deactivation reports `Inactive`. Each poll
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

Controlled schedules cover exact refusal stages and native versus predicate failures, request
denial versus unentered/completed sessions, busy and incompatible reads, malformed publication,
provider absence and errors, retained inactive interfaces, independent instances, saturation,
activation continuity, and failed or reentrant registration cleanup. These are controlled COM
observations. Real installed-service UIA evidence still requires maintainer registration of this
reviewed build and the separate physical document, composition, continuation, and focus gates.

| Schedule | Controlled evidence | Physical evidence required |
|---|---|---|
| `ghbdtn` plus Space and Escape | Exact whole documents, caret, owned identity, and disposition pass | Selected EN and loaded DLL observed; physical `ghbdtn` plus Space remains unchanged; correction and composition unproved |
| Rapid next letter and moved caret | Bounded finish/replan, exact prefix/suffix preservation pass | Repeat physical typing without dropped or duplicated characters |
| Denied request, failed session result, missing callback, late callback, null composition | Same-service zero-write and pass-through schedules pass | Real editor lock-denial behavior untested |
| Range, text, caret, active-membership drift; extra foreign composition | Refusal before write passes | Real host notifications and composition identity untested |
| Reentry at exercised COM boundaries and exceptions | No new write after invalidation, outcome-derived consumption, and no retry pass | Native callback schedules untested |
| Unknown MARK and FINISH, reserved Escape | Stop without replay or repair passes | Unknown-effect hosts remain unadmitted |
| Focus A to B to A, late old termination | Old authority never revives in doubles | Live mark termination and focus acceptance blocked |
| Password, read-only, sensitive scope, disabled compartments, secure activation | Zero text reads and writes pass | Physical excluded-control checks untested |
| Native Undo after correction and continuation | Not modeled as a TSF guarantee | Whole document, caret, Undo grouping, and subsequent input untested |
| EN/RU profile and source changes, teardown | Profile-table checks and invalidation pass | Both profiles present and enabled, EN selected; source transitions and clean teardown untested |

Only x64 MSVC build evidence exists. ARM64, generic editors, real Undo, ordinary-user registration
scope, and physical acceptance remain open. [ADR 007](adr/007-windows-tsf.md) remains proposed.
