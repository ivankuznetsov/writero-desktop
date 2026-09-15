# QA 01 — Document model and commands

Worktree: `qa-document`, branch `qa/15-sep-document`. Initial HEAD `149c6f6424c9a9613fbd8b166964b2796fc2ead7`; clean tree. Skills: wiki-researcher (local wiki fallback), ce-debug return-to-caller. Wiki notes structured sync uses stable UUIDs and optimistic typed mutations; local editing must work offline.

## Confirmed findings (regressions reproduced before fixes)

| ID | Repro / symptom | Root cause | Regression | Status |
|---|---|---|---|---|
| QA01-001 | Split text then undo once leaves truncated text; merge then undo leaves merged content | Compound commands push independent history entries | splitAndMergeAreAtomicUndoSteps | Fixed |
| QA01-002 | Type into final placeholder, undo: two empty blocks remain | Auto-appended placeholder journaled but excluded from command history | undoTrailingEditRestoresPlaceholderCount | Fixed |
| QA01-003 | Convert text to heading, type, undo typing: heading conversion is lost | Coalescing checks only block ID/kind and permits non-coalescible preceding action | typingDoesNotCoalesceWithFormatting | Fixed |
| QA01-004 | Insert existing block again: duplicate stable ID accepted | No uniqueness validation at insertion boundary | duplicateBlockIdentityIsRejected | Fixed |
| QA01-005 | Type, drain journal, type again: new journal's before state predates drained edits | Coalescing copies entire undo entry into new journal | drainedJournalKeepsActualBeforeState | Fixed |
| QA01-006 | First text block whitespace, second has text: sidebar preview empty; zero limit unbounded | Preview returns first raw nonempty block and computes left(-1) | previewSkipsWhitespaceAndHonorsZeroLimit | Fixed |
| QA01-007 | Move content block past final placeholder: no trailing typing block | Move command skips trailing invariant enforcement | moveMaintainsTrailingTypingBlock | Fixed |

Evidence: `/tmp/qa01-red.log`: 18 passed, 7 failed. Qt 6.11.2; offscreen. API gateway unnecessary for pure document-model commands.

| ID | Repro / symptom | Root cause | Regression | Status |
|---|---|---|---|---|
| QA01-008 | Remote reset/update leaves undo actions referencing the previous remote baseline | Remote application preserves history snapshots/indices that can overwrite received content or address removed rows | remoteChangesInvalidateStaleUndoHistory | Fixed; effective remote changes invalidate undo/redo, no-op echoes preserve it |
| QA01-009 | Split indented code block: both halves lose indentation | Generic split trims code whitespace | codeSplitPreservesIndentation | Fixed |
| QA01-010 | Continue list item 2147483647: next marker becomes -2147483648 | Signed machine-int increment of arbitrary user numeric text | orderedListContinuationDoesNotOverflow | Fixed; decimal text increment |

Second red evidence: `/tmp/qa01-red2.log`: 26 passed, 3 failed. Randomized stress: seed 15092026, 1,000 mixed insert/remove/update/move/split/merge commands, each checked for exact block snapshot roundtrip through undo and redo; passed.

Remote history limitation: actual remote mutations clear local undo/redo rather than rebasing undo actions across received edits. Pending journals remain intact. No-op remote update/reset/title echoes preserve history. Rebase-aware collaborative undo is outside this batch.

## Final verification and delivery

- Commit: `3f8488ed6101f0ec17f6ae181f855ec9cf27e4fd` (not pushed; caller owns review/shipping).
- `tst_document`: 29 cases pass, including 1,000 deterministic randomized command round trips.
- `ctest --test-dir build -R '^(tst_document|tst_documentio|tst_markdown|tst_workspacestore)$' --output-on-failure`: 4/4 suites pass.
- `git diff --check`: clean.
- Coverage: block IDs/types, insert/remove/move, split/merge, list normalization/continuation, code whitespace, previews, journaling/coalescing, undo/redo, remote history invalidation.
- Residual: GUI editing interactions and live sync/API flows require caller/other assignments; this worker verified model-level behavior only. Received remote edits conservatively invalidate history; undo rebasing is not implemented.
