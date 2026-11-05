# Windows TSF proof

This is an x64, fixture-only Text Services Framework adapter. It links the existing Rust engine
through [the C ABI](../include/keyrill.h). It has no replacement backend, hooks, input injection,
clipboard transport, runtime network access, model policy, or personal dictionaries.

The live acceptance gate is **not passed**. A maintainer registration attempt failed with an
unspecified HRESULT and left partial state. Read-only TSF enumeration confirms both proof profiles
are absent. Installed source selection and physical TSF input remain untested.
The stock RichEdit editor and read-only UIA
inspector build. All four named text and caret oracles pass against the real UIA provider with
literal fixture data. Installed-service routing remains untested. Controlled COM schedules and
observer calibration do not establish it.

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
| `KeyrillProofEditor.exe` | Stock `RICHEDIT50W` editor with writable A and B, read-only, and password controls |
| `KeyrillProofInspect.exe` | Read-only, content-free UIA fixture assertions |
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
101 and 102, ordinary input scope, and non-disabled context and thread compartments. Secure-mode
activation is refused. Read-only, password, sensitive, uncertain, foreign-composed, and unknown
controls get no text read and no composition. This executable-name restriction is a proof
support policy, not a security boundary or approval for arbitrary RichEdit hosts.

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

The active TSF profile supplies a validated actual substitute HKL. Translation uses that layout
with the current Shift and Caps Lock state and `ToUnicodeEx`'s no-state-change flag. Synthetic
TIP handles are not treated as keyboard layouts. Only the stable EN and RU proof profiles are
accepted. An unsupported profile or layout invalidates authority and passes without a guessed
finish. Shortcuts, dead keys, and coalesced repeat messages request a verified owned finish before
passing through.

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
TSF's profile and category APIs separately. **Their registration scope and ordinary-user success
have not been established.** Each registration and enablement API boundary reports its stage,
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

| Schedule | Controlled evidence | Physical evidence required |
|---|---|---|
| `ghbdtn` plus Space and Escape | Exact whole documents, caret, owned identity, and disposition pass | Installed EN routing, displayed composition, and physical keys untested |
| Rapid next letter and moved caret | Bounded finish/replan, exact prefix/suffix preservation pass | Repeat physical typing without dropped or duplicated characters |
| Denied request, failed session result, missing callback, late callback, null composition | Same-service zero-write and pass-through schedules pass | Real editor lock-denial behavior untested |
| Range, text, caret, active-membership drift; extra foreign composition | Refusal before write passes | Real host notifications and composition identity untested |
| Reentry at exercised COM boundaries and exceptions | No new write after invalidation, outcome-derived consumption, and no retry pass | Native callback schedules untested |
| Unknown MARK and FINISH, reserved Escape | Stop without replay or repair passes | Unknown-effect hosts remain unadmitted |
| Focus A to B to A, late old termination | Old authority never revives in doubles | Live mark termination and focus acceptance blocked |
| Password, read-only, sensitive scope, disabled compartments, secure activation | Zero text reads and writes pass | Physical excluded-control checks untested |
| Native Undo after correction and continuation | Not modeled as a TSF guarantee | Whole document, caret, Undo grouping, and subsequent input untested |
| EN/RU profile and source changes, teardown | Profile-table checks and invalidation pass | Registration, enablement, selected-source changes, and clean teardown untested |

Only x64 MSVC build evidence exists. ARM64, generic editors, real Undo, source registration,
and physical acceptance remain open. [ADR 007](adr/007-windows-tsf.md) remains proposed.
