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

## Deliberate divergences

- Backspace-at-start merges blocks. The web cannot merge; merging is a native
  writing-flow necessity and is fully undoable.
- Native undo spans the whole session rather than one browser editing session.
- Native exposes explicit move up/down buttons in addition to drag reordering.

## Evidence

| Milestone | Evidence |
|---|---|
| U1 | `tests/document/tst_document.cpp`, `tests/ui/tst_native_editor.qml`, screenshot `docs/verification.md` |
| U2 | storage/recovery tests |
| U3 | block editing QML tests |
| U4 | Markdown round-trip fixtures |
