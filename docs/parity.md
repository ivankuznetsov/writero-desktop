# Editor parity inventory

Captured 2026-09-11 from the Writero web editor at branch
`fix/bulletproof-list-editing` (source commit `7ef2f7a9`). This is the dated
acceptance inventory required by the native desktop plan: the native client
must reach equivalent outcomes for every row, and this file records the
evidence.

Legend: `native` — implemented in this repository; `web` — documented web
behavior the native client follows; `todo` — not implemented yet.

## Blocks

| Area | Web behavior | Native |
|---|---|---|
| Text | Markdown string in `Block#content`; rendered Markdown while unfocused, raw text while editing (`app/views/blocks/_block.html.erb`, `app/javascript/controllers/editor_controller.js`) | native: same storage; Markdown preview / raw edit |
| Heading | `metadata.heading_level` h1–h4, default h2 (`app/models/block.rb:48,61-71`) | native: `Block::headingLevel()`, type menu + slash menu |
| Bullet list | Markers `- `, `* `, `+ ` stored in content, nesting by indentation (`app/javascript/services/markdown_block_parser.js`) | native: `listcontent` port of `ListContentNormalizer` |
| Numbered list | `1. `, `2. ` markers, nesting by indentation | native: same, continuation increments |
| Code | Plain content + `metadata.language`; Enter inserts a newline, never splits (`editor_controller.js:848-854`) | native: Enter keeps newline; Shift+Enter splits |
| Quote | Plain content displayed as blockquote; no `>` stored | native: quote styling, Enter moves to next block |
| Divider | Empty content, non-editable `<hr>` | native: rendered rule; content cleared on conversion |
| Media | Image/video attachment or external `metadata.src`; 50 MB limit; JPEG/PNG/GIF/WebP/MP4/WebM (`app/models/block.rb:47-49`) | native: external src preview + attach dialog; media store in U2/U4 |

## Editing operations

| Operation | Web behavior | Native |
|---|---|---|
| Insert block | Toolbar `+` inserts an empty text block after (`blocks_controller.rb#insert_after`) | native: toolbar `+` |
| Split | Shift+Enter splits at the cursor and trims both halves (`editor_controller.js:888-964`) | native: Shift+Enter via `DocumentSession::splitBlock` |
| Merge | Not available (contenteditable blocks cannot merge) | native: Backspace at block start merges upward (native enhancement) |
| Delete | Empty blocks delete immediately; non-empty blocks ask for confirmation (`editor_controller.js:1577-1633`) | native: toolbar delete; no confirm dialog yet |
| Reorder | Drag handle (SortableJS) or none; move endpoint with before/after placement | native: up/down toolbar buttons; drag in U3 |
| Convert type | Block toolbar menu preserves content; slash menu clears content (`block_type_selector_controller.js:93-179`, `editor_controller.js:672-748`) | native: type menu preserves; slash menu preserves (divider clears) |
| List Enter | Continues marker and indent; empty item exits the list (`app/javascript/services/list_continuation.js`) | native: `DocumentController::handleListEnter` |
| List normalize | Tabs = 4 spaces; loose blank lines only before markers/indented continuations (`app/services/list_content_normalizer.rb`) | native: `listcontent::normalize`, unit tested |
| Inline formatting | `**bold**`, `*italic*`, `` `code` ``, `~~strike~~`, `[text](url)` inserted around selection; no toggling (`format_toolbar_controller.js:196-214`) | native: Ctrl+B/I/E and link helper via `formatactions` |
| Slash menu | 11 items, case-insensitive filter, Enter selects (`edit.html.erb:96-170`) | native: `SlashMenu.qml` with filter |
| Undo/redo | Browser-native per editing session; does not cross saves | native: document-level undo stack, coalesced typing, crosses saves |
| Autosave | On blur, Enter for non-text blocks, visibility change; 50 ms deferral (`editor_controller.js:321-358`) | native: debounced session autosave once storage lands (U2) |
| Word count | Text/heading/quote/ul/ol blocks, `\S+` tokens (`app/models/block.rb:133-146`) | native: `Document::wordCount` |
| Empty placeholder | Server keeps a trailing empty text block (`app/models/article.rb:99-110`) | native: `DocumentSession::ensureTrailingEmptyBlock` |

## Markdown paste

The web parser (`app/javascript/services/markdown_block_parser.js`) splits
pasted Markdown into atomic blocks: headings `#`–`####` (h5/h6 degrade to
text), fenced code with language, lists with nesting and lazy continuation,
merged `>` quotes, `---`/`***`/`___` dividers, and blank-line-separated text
paragraphs. Native import/export implements the same mapping in U4.

## AI operations

| Area | Web behavior | Native |
|---|---|---|
| Rewrite | Prompt plus multi-model alternatives; individual results wait for Replace/Insert below/Dismiss while explicit bulk actions apply automatically (`app/services/ai_service.rb`, `multi_model_rewrite_service.rb`) | native: `AiController` and `AiPanel.qml`, results persisted before dispatch |
| Research | Block research with preserved sources; scheduled research creates articles (`scheduled_research_service.rb`) | cloud: runs as a service function and arrives as a synced article (U8–U12); no local research operation |
| Image generation | OpenRouter chat with image modalities; replace snapshots old media, add-below creates a media block (`image_generation_service.rb`) | native: per-model capability discovery, reference images only where the model and provider support them, results stored as workspace media |
| Image explanation | Vision model describes the block's image; read-only result (`image_explainer_service.rb`) | native: Explain tab, model filtered to image-input capability |

Model capabilities come from the provider catalog when it reports modalities
(OpenRouter's `architecture.input_modalities` / `output_modalities`); other
endpoints fall back to name-based inference and the UI says so rather than
claiming support.

## Deliberate divergences

- Backspace-at-start merges blocks. The web cannot merge; merging is a native
  writing-flow necessity and is fully undoable.
- Native undo spans the whole session rather than one browser editing session.
- Native exposes explicit move up/down buttons in addition to drag reordering.
- Research is not a local operation: it is a cloud function delivered as a
  synced article, per product direction.

## Evidence

| Milestone | Evidence |
|---|---|
| U1 | `tests/document/tst_document.cpp`, `tests/ui/tst_native_editor.qml` |
| U2 | `tests/storage/tst_workspacestore.cpp`, `tst_workspace.cpp` |
| U3 | `tst_ui` editing interactions, `tst_documentcontroller` |
| U4 | `tests/markdown/tst_markdown.cpp`, `tests/document/tst_documentio.cpp`, media history restore in `tst_workspace.cpp` |
| U5 | `tests/ai/tst_aiclient.cpp` (stub server), `tst_providerregistry.cpp` |
| U6 | `tests/ai/tst_textactions.cpp`, `tst_aicontroller.cpp` |
| U7 | on-demand image generation and explanation with catalog-based capability filtering (`tst_modelcatalog`, `tst_aiclient`, `tst_aicontroller`); research is a cloud function delivered as a synced article |
| U8 | server: `test/integration/desktop_oauth_test.rb`, `capabilities_controller_test.rb`; desktop: `tests/cloud/tst_accountsession.cpp` |
| U9 | server: `desktop_sync_contract_test.rb`, `desktop_change_capture_test.rb` |
| U10 | desktop: `tests/cloud/tst_sync.cpp` (connect, replay, conflicts, media, remote results/history, account guards), `tests/cloud/tst_reconciliation.cpp` |
| U11 | server: `hosted_ai_billing_test.rb`, `desktop_hosted_ai_test.rb` (text + image generation/explanation, reservations, settlement, upload ownership); desktop: `tests/ai/tst_hosted_jobs.cpp` |
| U12 | not implemented; shares, schedules, and remaining parity rows |
| U13 | `docs/verification.md`, `packaging/`, install rules in `CMakeLists.txt` |
