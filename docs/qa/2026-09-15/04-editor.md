# QA04 — Native editor controllers and formatting

Scope: `src/editor`, `tests/editor`. Isolated branch `qa/15-sep-editor`; baseline `149c6f6424c9a9613fbd8b166964b2796fc2ead7`, clean. QA uses synthetic documents and Qt 6.11.2 offscreen. Skills: wiki-researcher and ce-debug return-to-caller. Past knowledge: Writero wiki establishes block revisions and atomic mutations; no native cursor coverage found. No pre-existing PR covers these findings.

| ID | Reproduction | Root cause | Red evidence | Status |
| --- | --- | --- | --- | --- |
| QA04-001 | Create UL content `- `, Enter at 2 | Empty-item branch changes only block type, retaining literal marker in resulting paragraph | `exitingSoleEmptyListItemRemovesMarker`: content was `- `, expected empty | Fixed; regression green |
| QA04-002 | Content `  - one`, Shift+Tab at cursor 0 | Outdent subtracts full indentation even when cursor is inside removed prefix; QML treats negative return as unhandled | `outdentAtStartKeepsCursorOnCurrentLine`: cursor -2, expected 0 | Fixed; regression green |
| QA04-003 | Content `12. first`, Enter at cursor 1 | List Enter inserts continuation inside marker, splitting numeric syntax into malformed lines | `enterInsideListMarkerDoesNotCorruptMarker`: cursor 6, expected 9; raw insertion splits original marker | Fixed; regression green |
| QA04-004 | Apply inline code to `a\`b`, backtick-delimited text, or surrounding spaces | Fixed one-backtick delimiter collides with selected text and omits CommonMark padding | Round-trip Qt Markdown renderer changed `a\`b` to `ab\`` and stripped selected boundary characters | Fixed; regression green |
| QA04-005 | Link label `a]b` to URL ending `/a)b` | Link builder concatenates Markdown metacharacters without escaping label or encoding destination | Qt Markdown rendered complete literal Markdown instead of hyperlink | Fixed; regression green |
| QA04-006 | Paste `alpha\n\nbeta` over block containing `alpha` | No-op first block update treated as whole-paste failure; remaining blocks never inserted | `pasteIdenticalFirstBlockStillInsertsRemainingBlocks`: returned false | Fixed; regression green |
| QA04-007 | Paste three blocks, Undo once | Controller records each paste mutation as independent gesture | `multiBlockPasteUndoesAsOneGesture`: 3 blocks remain instead of original 2 | Fixed; atomic undo regression and stress green |

First list batch: all 20 Qt cases passed after fix. QA01 commit cherry-picked as dependency for core undo grouping; its 10 findings are **not counted here**. Parent authorized the small DocumentSession grouping API addition.


## Final verification

- 7 distinct root causes fixed. No counts added for duplicate symptoms or dependency QA01 findings.
- Controller suite: **29 passed, 0 failed**, including rendered Markdown round trips and a 100-iteration whole-document paste/undo/redo/typing stress test.
- Relevant broader suites: **3/3 passed** (`tst_document`, `tst_documentcontroller`, `tst_ui`).
- Repeat controller suite **20/20 passed**, exercising **2,000** generated multi-block paste histories with snapshot checks after undo, redo, and subsequent typing.
- Native Wayland compositor Qt Quick run: **10 passed, 0 failed**. This is real compositor automated interaction, not a claimed manual review.
- `git diff --check`: passed.
- Build limited to `-j2`; no external API calls or credentials used by this assignment.
- Source scope: controller list/paste operations; Markdown format helpers; a public scoped session grouping callback built on QA01's undo grouping. Regression tests remain in the existing editor suite.
- Limitations: live API behavior and full cross-subsystem regression/PR review are delegated to parent and other assignments. Parent owns PR publishing.

Evidence logs: `04-list-red.log`, `04-format-red.log`, `04-validation.log`, `04-stress.log`, `04-wayland-ui.log` beside this report.
