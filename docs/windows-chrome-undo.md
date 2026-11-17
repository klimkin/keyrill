# Chrome Undo investigation

Status is open. This blocks complete Chrome acceptance.

The approved end-only experiment was installed and tested after full x64/x86 verification,
sanitizer and independent review. The loaded DLL matched candidate `940ee5df` and published
its expected production identity. In the rich fixture, generated `x` followed by Ctrl+Z still
left `x` selected. The exact initial-state oracle failed with TextMismatch. Live diagnostics
confirmed two verified operations, no unknown result and released composition ownership.
Removing the unchanged Finish replacement therefore did not resolve the observed Undo behavior.
Further causal investigation and physical keyboard comparison remain open.

Physical input reproduced the same selected `x` on the experimental candidate. A second
physical trial used a word whose composition had already ended through Escape. The exact
word and caret matched before Ctrl+Z, and service ownership was empty. Physical Ctrl+Z left
the word selected. Verified native effect counters stayed unchanged during this Undo.
The currently loaded Chrome version for these experimental trials is 157.0.8103.79. The
157.0.8103.52 version below belongs to the earlier baseline.

These observations rule out generated shortcut delivery and finalization during the Undo
keystroke as sufficient explanations. The experiment remains unsuccessful. Chromium's own
composition-confirmation replacement and Blink Undo registration require further investigation.

A minimal rich fixture with the same prefix, suffix and initial caret, but no input,
composition or selection-change listeners, changed the result. First Undo removed `x`, while
the exact selection oracle still failed because the following space was selected. The original
reporting fixture retained selected `x`. This comparison establishes that the fixture affects
the observed text outcome. Synchronous report DOM updates during composition are under review.
The remaining selection discrepancy requires a separate explanation.

The installed candidate built from `4471d61` reproduces different native Undo behavior in
Chrome 157.0.8103.52 x64 rich contenteditable. The public fixture contains a fixed prefix,
including an astral character, and a suffix. Its compiled UIA oracle checks exact UTF-16 text
and selection.

| Source and sequence | Observed result |
|---|---|
| Ordinary English, `x`, Ctrl+Z | Original text and collapsed caret match |
| Keyrill English, `x`, Ctrl+Z | `x` remains selected, text comparison fails |
| A second Ctrl+Z | Original text returns, selection comparison fails |
| Keyrill English, `x`, Right, Ctrl+Z | `x` remains, text comparison fails |

These sequences use generated individual keyboard events. Physical confirmation remains open.
Fresh service counters were unavailable during these comparisons. Earlier observations establish
verified owned writes in this Chrome process, while the comparisons establish document behavior.

Ctrl+Z maps to Finish and then passes to the application. The installed candidate replaces the
exact owned range with its verified text before ending composition. An extra replacement Undo
unit is a possible explanation. Native call-level causality remains unproved.

Microsoft documents `TF_ST_CORRECTION` as preserving text properties. The documented contract
provides no Undo grouping guarantee. Pinned Chromium source ignores the SetText flags and
performs selection followed by insertion, including unchanged replacements. That revision has
not been matched to the installed Chrome executable.
[Microsoft SetText documentation](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfrange-settext)
and [pinned Chromium implementation](https://chromium.googlesource.com/chromium/src/+/17ba7a0067e02c76f95b74344571788981ce0138/ui/base/ime/win/tsf_text_store.cc).

The approved Windows experiment ends ordinary unchanged owned compositions without `SetText`.
It requires exact verified text, range extent, context, generation and full selection around
`EndComposition`, and marks the effect attempted before termination. A failed or unknown end
stops correction without replay, retry or recovery writes. Changed finalization and retirement
retain their replacement paths. Platform-requested focus-loss and unknown-effect readback
commits retain their existing protocols.

Controlled providers test native call counts and failure handling. Chrome Undo remains open
until the experimental artifact passes ordinary-source comparisons of complete text, caret and
selection through Undo and Redo in the actual target controls. Cross-host and physical
acceptance remain pending.
