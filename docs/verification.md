# Verification guide

This document records how each surface is verified, what runs by default, and
what requires opt-in configuration. No test in the default suite touches the
network or needs an account.

## Default suite

```sh
cmake -B build -G Ninja -DWRITERO_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| Suite | Covers |
|---|---|
| `tst_app_info` | Application identity used by packaging and settings paths |
| `tst_document` | Block model, undo/redo, list normalization, trailing block invariant |
| `tst_markdown` | Markdown parse/serialize round trips against the web mapping |
| `tst_documentio` | Markdown/HTML/PDF export, bundle round trip, tamper and traversal rejection |
| `tst_workspacestore` | SQLite schema, revisions, crash recovery, media staging cleanup |
| `tst_workspace` | Workspace lock, library model, autosave, history restore, bundle import |
| `tst_aiclient` | Provider protocols against a local stub HTTP server (SSE, NDJSON, errors, images, capability refusals) |
| `tst_modelcatalog` | Model catalog parsing, modality inference, cache round trip |
| `tst_textactions` | Prompt/context assembly, humanize detection, bulk encoding |
| `tst_aicontroller` | End-to-end rewrite/bulk flows with a stub provider and persisted results |
| `tst_providerregistry` | Profile persistence without secrets, keyring fallback |
| `tst_documentcontroller` | Editor gestures: list Enter, indent, formatting, metadata |
| `tst_hosted_jobs` | Hosted AI provider against a stub desktop API; transient context, failures, ambiguity |
| `tst_reconciliation` | Remote change application, conflict retention, snapshot merge |
| `tst_sync` | Connect/push/pull against a stub desktop API, replay, lock conflicts |
| `tst_ui` | Qt Quick editor interactions on the live delegate (click, typing path, split, Tab, slash menu) |

QML lint is part of CI:

```sh
qmllint -I build qml/**/*.qml
```

### Test environment notes

- The suite sets `QT_QPA_PLATFORM=offscreen`. No display is required.
- Credential tests set `WRITERO_DISABLE_KEYRING=1` so they never read or write
  the user's keyring.
- Keyboard-layout-dependent typing is avoided in UI tests; shortcuts, Tab,
  Enter, and slash handling use real key events.

## Opt-in real-provider checks

Real providers are only contacted when a developer configures them in the app
under **AI providers** (or in `workspace.db` settings). To record evidence:

1. Add a provider (OpenRouter with a personal key, an OpenAI-compatible
   endpoint, or a local Ollama runtime).
2. Open a scratch document containing one paragraph.
3. Run Rewrite with a single model and verify the streaming preview, then
   Replace; verify undo restores the original text and history shows the
   change with `source = ai`.
4. Run with two models and verify each alternative can be accepted or
   dismissed independently.
5. Open the model list and verify the catalog loads (OpenRouter reports image
   output and image input modalities). For Generate, confirm only
   image-capable models are listed and that the reference checkbox is enabled
   only for a model that accepts image input on OpenRouter.
6. Run Image generation and verify Replace snapshots the previous media into
   media history; switch to Explain and verify a text-only model cannot be
   selected.
7. Verify unsupported paths explain themselves: a text-only model is not
   offered for Generate, and a reference image is refused with a message
   instead of being silently dropped.

Record the provider, model identifier, and app revision in the experiment
notes. Never record API keys or private article content.

## Manual Omarchy acceptance

| Check | Expected |
|---|---|
| Launcher entry | Writero appears with its icon; `StartupWMClass` matches |
| File dialogs | Native Wayland dialogs for import/export/bundle |
| Clipboard | Paste of Markdown splits into typed blocks; plain text inserts inline |
| Scaling | UI follows the compositor scale; text remains crisp |
| IME | Fcitx5/Nord input composes text in a focused block without losing content |
| Dark/light | Dark theme is intentional; the palette switches with the window palette |
| Keyboard | Ctrl+B/I/E, Ctrl+Z/Y, Ctrl+S, Ctrl+N, Tab/Shift+Tab, Enter, Shift+Enter |
| Multi-window | Second instance reports the workspace lock instead of corrupting data |
| Recovery | Kill the process mid-edit; reopening restores the last autosave |

## Performance target (engineering target, to be measured)

On a reference machine and a 1,000-block text corpus: visible input response
p95 under 50 ms and no UI stalls during background sync. Measure with the
editor's built-in timings once a benchmark document fixture is added; this is
not a current guarantee.

## Packaging evidence

```sh
cmake --install build --prefix /tmp/writero-prefix
find /tmp/writero-prefix -type f
```

The install must contain `bin/writero-desktop`,
`share/applications/app.writero.Writero.desktop`, and
`share/icons/hicolor/scalable/apps/app.writero.Writero.svg`, and no test
binaries, credentials, or user documents.
