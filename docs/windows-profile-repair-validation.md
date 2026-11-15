# Windows profile repair validation

The maintainer's migration stopped after the EN profile description acquired an unexpected
suffix. The original operation remains at its profile write. This change corrects the string
storage and supplies an explicitly approved single-profile recovery path. Actual native
qualification has stopped at disposable cleanup. Installation remains pending.

The buffer regression is committed as `aa65701`, its correction as `eba8195`, and the bounded
repair as `4fe3846`. The separately gated native qualification is in `bfc073b` and `863607b`.
The frozen candidate was built at `e3bcddbddc73a934b0535923b0c845b4a7c1edd5`.
Later documentation does not change its immutable bytes or source inputs.

## Frozen candidate

| Identity | SHA-256 |
|---|---|
| Package | `deb2dc1bdb4ff7ce6044355342f0794161354c8eac67bfa9cb66a9ba23f9b454` |
| Source inputs | `0b6493e7c5626e4083d675e76227b71c060c07e480de1926ff2d7ef7fb1dfee9` |
| x64 production build | `feb2eda9094fa3e5512bab375176ff228400d9f52de544d4f71df4e7f3d48d8e` |
| x86 production build | `dafcd6456a245fd350a8fe3f768db7979d5d93d8b7d5216c65aaf9a4a9312167` |
| x64 DLL bytes | `0fde566da0d770f79c0a0e75b92dbdb4c64dd1a63389de8f06ee209ab78da595` |
| x86 DLL bytes | `6e1602066a0df067dd3ad6dd91fea3bd9f725b0d83f55ab1527da25b5c99479a` |
| x64 Setup bytes | `8dc6f1ec21f239dd912d35bd7d2d6f0db45a8b2e767934086c9b5ebd547f2ae7` |

## Implemented behavior

Both TSF profile strings have terminated backing storage. Their explicit SDK counts exclude
the terminators. Safe regressions cover full buffers and counted UTF-16, including empty
strings and surrogate pairs. They do not read beyond the pre-fix allocation.

The recovery approval binds the original receipt, exact raw source, both registry views,
protected resources, current user and verified corrected executable. It permits one profile
write to the original desired value. A durable attempt marker prevents replay after an
ambiguous interruption. Ordinary reconciliation remains unchanged. The original receipt is
archived without editing it to accept the corrupted state.

Unresolved repair history blocks ordinary mutation. After upgrade, retained history prevents
selecting the defective predecessor through corrected Setup. Old executables cannot enforce
that guard and must not be used. Restart readback can verify persistent observations but
explicitly records that preservation of the terminated tool thread's source is unavailable.

Three recovery designs were compared. The selected design avoids the broader unjournaled
legacy registrar batch and the extra EN rewrite of restoration to the original before-state.
Independent design and actual-diff reviews found no remaining actionable issue in this scope.

## Completed checks

| Check | Result |
|---|---|
| Focused Setup tests | 41 passed on x64 and x86 |
| Clippy with warnings denied | Passed on x64 and x86 |
| `cargo xtask windows check --arch all` | Exit 0 |
| `cargo xtask windows sanitize` | Exit 0, required use-after-free canary detected |
| Controlled TSF suite | 214 passed per normal architecture and under x64 ASan |
| Runtime suites | 80 production and 81 test-API cases passed per native run |
| Setup, proof and ABI suites | 41, 53 and 3 passed per native run |
| Clean dual-architecture package rebuild | All 119 paths and file hashes identical |
| Native package verification and read-only status | Passed on both architectures |
| Default test isolation | Maintainer qualification entries absent from both test lists and normal runs |
| Explicit token refusal | Both feature executables refused the exact native test before registration |
| Real read-only qualification observer | Passed on x64 and x86 under an ordinary token |
| Final package's read-only repair inspection | Exit 0, original cursor and 25 protected resources bound |
| Local handoff checks | Exact test enumeration rejects zero-test success; PowerShell 5.1 and 7 syntax, SID and file checks passed |

The ASan configuration is experimental. Prebuilt standard libraries and Windows code remain
uninstrumented. The native qualification binary is separate from production packaging and
requires both its explicit build feature and maintainer token. Normal verification never
executes its registration cases.

## Remaining maintainer gates

The maintainer's first qualification run stopped when foreground focus changed during a
protected-state comparison. A second run passed four complete cases and verified the fifth
profile update, then failed the final cleanup assertion. Reaching that assertion establishes
that native profile removal and service unregistration returned success, but the disposable EN profile's explicit user `Enable=0`
preference remained. Read-only inspection confirmed the machine registration was absent and
the user preference remained in both registry views. The original installation receipt is
unchanged. The recovery handoff is disabled until fixture cleanup is corrected and verified.

The passing cases had no user preference tree. They do not prove that deleting the retained
value alone will remove its ancestor keys. Full thirteen-case qualification remains required;
the partial results do not establish complete native preservation or cleanup. Failed fixtures
and their evidence remain retained without retry or guessed cleanup.

After that qualification, the maintainer must execute the approved profile repair, ordinary
recovery and corrected installation. Independent final receipt and registration readback,
installed application checks, physical input, account/integrity cases, reboot and lifecycle
acceptance remain open. Hosted cross-platform checks were not run in this local pass.
See [package recovery](windows-package.md#bounded-profile-repair) and the
[maintainer checklist](windows-maintainer-checklist.md).

Fix Root Causes kept the correction at the string-storage boundary. Model the Domain shaped
the explicit profile and attempt states. Prove It Works kept actual native preservation and
physical application acceptance separate from provider and package checks.
