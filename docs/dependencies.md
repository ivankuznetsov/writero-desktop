# Dependencies and licenses

This file is the running inventory for U13 (open-source packaging). Entries
are added when a dependency is introduced; the release checklist verifies the
list against the built binary under `docs/verification.md`.

## Build dependencies

| Component | Version | License | Purpose |
|---|---|---|---|
| Qt 6 (Core, Gui, Network, Qml, Quick, QuickControls2, Sql, Svg, Test, QuickTest) | ≥ 6.5 | LGPL-3.0-only / GPL-3.0-only (module terms vary; see below) | Application framework |
| Qt Image Formats plugins | Match Qt version | Qt module license terms apply | WebP image reading and writing |
| SQLite | ≥ 3.35 | Public domain | Workspace storage |
| libsecret | ≥ 0.20 | LGPL-2.1-or-later | Secret Service access |

Qt module licensing varies by module and by whether they are used in a
statically linked build. The desktop application uses only LGPL-compatible
modules; Qt WebEngine and other GPL-only modules are deliberately excluded
(Requirement R1 forbids a web renderer). License review before the first
public release must re-check each selected module against
<https://doc.qt.io/qt-6/licensing.html> and record the outcome here.

## Vendored assets

Nothing vendored yet.

## Data and prompts

AI prompt templates in `resources/prompts/` are original to this repository,
written from behavioral descriptions of the Writero web app rather than
copied from its service code. Any future reuse of upstream prompt text must
be recorded here with its licensing basis.

## Product font

The interface uses system fonts through Qt's font database. If a bundled font
is added later, its license must be listed here.
