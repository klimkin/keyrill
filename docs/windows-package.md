# Windows package and installation

`KeyrillSetup.exe` owns native installation and recovery through the pinned Rust and windows-rs
dependencies. The maintainer launches machine mutations explicitly with administrator rights.
Input-source enablement and selection are separate maintainer actions. Preferences and personal
dictionaries retain their user-owned lifecycle.

## Candidate preparation

Commit the candidate source before staging. Build both production targets, then stage into a new
directory. Complete packages require the x64 and x86 artifacts from the same source closure.

```console
cargo xtask windows package-build --arch all
cargo xtask windows package --candidate target/candidate
cargo xtask windows package-verify --candidate target/candidate
```

`package-build --arch x64` and `--arch x86` prepare one architecture independently. Outputs live
under `target/windows-package-x64` and `target/windows-package-x86`. Each contains the actual
production DLL, Settings, Register, Setup, and a build receipt. Remapping removes build-machine
paths; the linker receives `/Brepro` and a relative PDB name. Package staging consumes these
receipts and refuses a different source closure, compiler, target, feature set, or file hash.

The candidate contains both client architectures, English/Russian icons, the project license,
model provenance/notices, and dependency license declarations and texts. Release validation
enforces the exact production binaries, exports, resources, and license inventory.
The manifest records the commit, source closure, compiler, model, lockfile, target fingerprints,
and every installed file hash. Its SHA-256 is the candidate identity. Verification works after
copying the directory through read-only validation. Unsigned hashes establish content consistency.
Publisher authenticity requires maintainer signing.

Validation bounds each opened file before allocating or reading its contents. The shared
package reader permits 64 MiB per executable, 4 MiB per public resource and 1 MiB per JSON
file. A complete package permits 256 files including its manifest and 256 MiB in total.
These budgets allow headroom for the embedded model, both client architectures and dependency
licenses. The allowed layout has at most three relative path components. Windows validation
uses whole-path no-reparse native opens and enumerates retained directory handles. It rejects
offline entries and retains directory and file leases until all content checks finish.
Declared PE tables permit 96 sections, 128 exports and
128-byte export names.

The native boundary uses `NtCreateFile` with `OBJ_DONT_REPARSE` for each actual open and
`NtQueryDirectoryFile` with one bounded `FileNamesInformation` entry per query. The general
`OBJECT_ATTRIBUTES` contract documents this flag's reparse refusal. The function-specific
`NtCreateFile` page lists zero and `OBJ_CASE_INSENSITIVE` for that field. Native tests verify
the selected combination on the test host, including a directory converted into a junction
while retained. A handle-based query stays on that original directory and a child path open
refuses the redirect.
[OBJECT_ATTRIBUTES](https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_object_attributes),
[NtCreateFile](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile),
[NtQueryDirectoryFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile)

An elevated host checks the retained package-root, architecture, manifest, DLL and Settings
ACLs before it reads package contents. An ordinary host can validate a complete user-owned
package. Settings launch remains bound to the loaded DLL's production identity, manifest
identity and retained peer hashes.

The launch keeps the immediate-child lease chain through process creation. Native tests on
the local test filesystem observe `ERROR_DIR_NOT_EMPTY` when a retained populated directory
is changed into a junction, and refusal of child deletion and directory replacement.
These checks cover the ordinary local-filesystem contract. Arbitrary filesystem filters and
concurrent DOS drive remapping have no qualification evidence.

Repeat staging into a second new directory from the same build receipts and compare the manifest
and file hashes. Candidate staging is deterministic for those inputs. Independently rebuilding
the binaries is a separate reproducibility check. Signing changes binary bytes and requires a
new manifest, candidate identity, and renewed validation. Signing and publication remain maintainer work.

## Explicit maintainer commands

Run `x64/KeyrillSetup.exe status` for read-only owned registration observations. `verify PACKAGE`
checks candidate contents. Machine mutation commands require an explicit administrator launch
and `--scope machine`.

```console
KeyrillSetup.exe install --scope machine PACKAGE_DIRECTORY
KeyrillSetup.exe repair --scope machine PACKAGE_DIRECTORY
KeyrillSetup.exe migrate-proof --scope machine PACKAGE_DIRECTORY EXISTING_PRODUCTION_PROOF_DLL
KeyrillSetup.exe recover --scope machine
KeyrillSetup.exe rollback --scope machine
KeyrillSetup.exe uninstall --scope machine
KeyrillSetup.exe cleanup --scope machine
```

The standard US and Russian keyboard layouts must already appear in `GetKeyboardLayoutList`.
A missing layout is a pre-mutation refusal with a Windows Settings instruction. Setup uses the
exact full native HKL for `SubstituteKeyboardLayout`. The native API requires a previously loaded
layout handle. [SubstituteKeyboardLayout](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofiles-substitutekeyboardlayout)

COM values are owned in both explicit HKLM views. TSF definitions use native profile/category
APIs with fresh registry readback in both views. HKCU profile Enable values are observed
separately and preserved during installation. Scope checks reject unexpected owned descendants,
profile metadata in HKCU, divergent machine views, and unrecognized preexisting registrations.
These registry observations define the tested installation contract. An unsupported shape causes
an explicit scope refusal.

The machine TIP service root permits either no values or one case-insensitive `Enable` value
whose type and bytes are exactly `REG_SZ` `1` with one UTF-16 terminator. This bounded recognition
comes from observed native registration metadata. Its origin and semantics remain undocumented.
Every service read validates those names and bytes and compares root presence and the raw optional
value across both views before returning the receipt's existing boolean. A mismatch refuses
mutation. The boolean treats a present root with either accepted representation as the same
service state, so receipts do not preserve absence versus string `1`. Unexpected values stop
recovery with its receipt retained. Native TSF calls retain exclusive ownership of service
registration changes. Current-user profile `Enable` values retain their separate exact-value
checks.

Stable class/profile GUIDs survive the proof-to-product transition. Explicit proof migration
accepts only the caller-selected production proof DLL, matching proof names/icon paths, and the
exact current user's shadow COM path and Apartment value.
`KeyrillRegister register --scope machine PACKAGE_DIRECTORY` and `unregister --scope machine` delegate to the
adjacent Setup owner. Enable/disable remain separate explicit maintainer operations.

## Persistence and recovery

Setup creates a protected Program Files `Keyrill` directory. Administrators and SYSTEM can write.
Users can read and execute. Later mutation runs validate that DACL. Both bitnesses resolve the
native Program Files path through the explicit 64-bit machine registry view and compare the
known-folder result where available. An embedded `asInvoker` manifest preserves the caller's
explicit privilege context. An exclusively opened file serializes installers across sessions
and bitness. Normal application and Settings use runs under the user's ordinary token.

Complete verified files are staged before COM points at an immutable `versions/CANDIDATE_HASH`
directory. `operation.json` durably records every exact before/after resource and the last
verified acknowledgement. `installed.json` records the completed result. Cleanup requires the
complete operation to match its durable installed receipt. Recovery freshly reads all resources
and accepts the recorded before/after values. Verified completed effects advance the cursor.
An API failure followed by the desired readback counts as convergence. An unavailable or third
state stops with the receipt preserved for inspection.

Rollback uses the previous verified package through the same reconciler. Same-package repair
preserves that previous version. An unrecognized changed
registration is a conflict, including during uninstall. Uninstall preserves preferences and all
dictionary contents. Old version files remain available to loaded clients. `cleanup` verifies
the exact file receipt, keeps in-use files, and durably records partial cleanup for a later retry.
The current and previous installed versions are retained for rollback. After uninstall, a Setup
copy outside those directories can remove them once client processes and the old Setup have exited.

The automated tests use synthetic PE parser fixtures, injected journal providers, real disposable
HKCU `Software/KeyrillTests` values, and temporary files. The original-name x86 Setup test executable
also passed its actual native Program Files and embedded-manifest checks under an ordinary token.
Maintainer acceptance covers both targets, standard/admin accounts, proof migration,
interrupted registration, upgrade with loaded clients, rollback, uninstall/reinstall, additional
accounts, and sign-in/reboot. Those gates stay pending until executed against the final candidate.
