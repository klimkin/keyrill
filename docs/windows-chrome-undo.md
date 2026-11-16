# Chrome Undo investigation

Status is open. This blocks complete Chrome acceptance.

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

Ctrl+Z maps to Finish and then passes to the application. Unchanged Finish currently replaces
the exact owned range with its verified text before ending composition. An extra replacement
Undo unit is a possible explanation. Native call-level causality remains unproved.

Microsoft documents `TF_ST_CORRECTION` as preserving text properties. The documented contract
provides no Undo grouping guarantee. Pinned Chromium source ignores the SetText flags and
performs selection followed by insertion, including unchanged replacements. That revision has
not been matched to the installed Chrome executable.
[Microsoft SetText documentation](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfrange-settext)
and [pinned Chromium implementation](https://chromium.googlesource.com/chromium/src/+/17ba7a0067e02c76f95b74344571788981ce0138/ui/base/ime/win/tsf_text_store.cc).

The current engineering contract requires unchanged Finish to replace its exact range. A design
that omits that write requires an explicit contract decision. Design review must retain owned
composition authority, exact preconditions, callback and generation checks, verified native
results and stop-on-unknown behavior. Acceptance requires ordinary-source comparisons of text,
caret and selection through Undo and Redo in the actual target controls.
