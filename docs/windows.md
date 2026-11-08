# Windows TSF proof

This is an x64, fixture-only Text Services Framework adapter. It links the existing Rust engine
through [the C ABI](../include/keyrill.h). It has no replacement backend, hooks, input injection,
clipboard transport, runtime network access, model policy, or personal dictionaries.

The live acceptance gate is **not passed**. The maintainer registered and enabled both proof
profiles and selected **Keyrill proof EN**. Later fixture runs passed case-level correction,
continuation, Undo, focus-preservation, exclusion, source, and native lock-denial oracles.
These supersede the earlier unchanged-text correction attempt. Automated keyboard runs also
passed correction and continuation. They do not count as physical keyboard evidence.
A fresh automated Escape reversal exposed a separate end-caret defect. The changing-finish
caret fix passed its controlled regression and the same automated correction and reversal
oracles. That case does not establish lifecycle acceptance.

The final lifecycle-journal build has case-level Tab and A-to-B-to-A evidence under automated
keys and clicks. Tab recorded EndRequested and FinishVerified for the owned serial before
document-focus invalidation. Clicking B and then A each recorded ExactTermination for the mark
owned in the departing field before focus invalidation. The exact correction and final focus
document/caret oracles passed, with zero Unknown effects. These runs establish the observed
fixture sequence, not physical keyboard evidence or general application support. The automated
owned-shutdown and empty-close cases below also passed. Later physical owned-shutdown and
fresh empty-close controls passed their bounded assertions. Unretained final destruction and
the complete first-editor reliability gate remain open.

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

Focus loss, source changes, context changes, and teardown invalidate authority. If a mark
exists, these paths stop the activation without writing it back. A termination callback clears
only its exact captured composition identity. Returning A to B to A never restores old editing
authority. The final fixture run observed exact native termination on each mouse focus change
before invalidation, alongside the expected whole documents and caret. This does not admit
other clients or establish Undo behavior. A later RU source-selection run preserved the public
literal and caret, but its mark had already reached FinishVerified before profile change. That
run does not isolate termination caused by the source-change callback.

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
key origin, composition, correction, Undo, or other applications. Physical verification of this
promoted mode remains a separate maintainer gate.

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

The observer must not substitute Alt+F4 for the title-bar click. An Alt key can finish the mark
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

The host-only `--prove-owned-en-to-ru` experiment is **unrun on the desktop**. It changes no
Service editing policy, Rust code, ABI, profile registration, or journal capacity. The maintainer
alone launches this mode because it selects a source. Default verification exercises controlled
COM doubles and never launches this mode. There are no target arguments, and repeated or
combined flags exit 2 before COM initialization.

The earlier source UI workflow passed its bounded normal-flow checks. Its journal recorded
Acquired, EndRequested, FinishVerified, document-focus notifications, and then ProfileChanged.
The maintainer selected RU, and the document/caret oracle passed after
agent-generated typing and Space. That sequence completed the mark before profile notification.
It does not establish the stronger native experiment below.

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
passes establish those controlled properties only. The maintainer-launched experiment and the
full first-editor reliability gate remain open.

[source-activate]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile
[source-language]: https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofiles-changecurrentlanguage

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
| Focus A to B to A, late old termination | Old authority never revives in doubles | Automated fixture run observed exact termination before each focus invalidation and passed the final whole-document/caret oracle; physical keyboard evidence remains open |
| Password, read-only, sensitive scope, disabled compartments, secure activation | Zero text reads and writes pass | Physical excluded-control checks untested |
| Native Undo after correction and continuation | Not modeled as a TSF guarantee | Whole document, caret, Undo grouping, and subsequent input untested |
| EN/RU profile and source changes, teardown | Profile-table checks, invalidation, and host-only source-probe schedules pass | Ordinary source UI flow and bounded physical owned-shutdown/empty-close cases passed; native source invocation with fresh owned evidence remains unrun |

Only x64 MSVC build evidence exists. ARM64, generic editors, real Undo, ordinary-user registration
scope, and physical acceptance remain open. [ADR 007](adr/007-windows-tsf.md) remains proposed.
