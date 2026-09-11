# Writero Desktop

A native, open-source desktop client for [Writero](https://writero.app) — the
block-based writing tool with AI assistance. Built with Qt 6 Quick/QML and
C++20, designed for Linux/Omarchy.

The goal is a first-class writing application: a fast block editor with the
same document model as the web app, local-first storage, and optional Writero
cloud services. Local documents never require an account.

## Status

Early development. The plan this repository implements lives in the Writero
service repository at
`docs/plans/2026-09-11-1126-feat-native-writero-desktop-plan.md`.

Implemented so far:

- [x] U1 — Native project foundation and editor feasibility gate
- [x] U2 — Durable local documents, media, and revision storage
- [ ] U3 — Complete block editing and document navigation
- [ ] U4 — Native import/export and media history
- [ ] U5 — Provider connections and local AI execution
- [ ] U6 — AI text and result-review parity
- [ ] U7 — Research and AI media parity
- [ ] U8–U12 — Writero service API and sync (service-side work)
- [ ] U13 — Open-source packaging and Omarchy acceptance

## Building

Requirements: CMake ≥ 3.21, a C++20 compiler, Qt 6.5+ (Core, Gui, Network,
Qml, Quick, QuickControls2, Sql, Svg, Test, QuickTest), SQLite, and libsecret
for credential storage.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/writero-desktop
```

See `docs/building.md` for distribution-specific notes and
`docs/dependencies.md` for the full dependency and license inventory.

## Philosophy

- **Local-first.** Documents live in an app-managed SQLite workspace with
  media files on disk. Markdown is an interchange format, not the storage
  format; block identity, order, metadata, and history survive round trips.
- **Native, not a webview.** The editor is a native Qt Quick component. No
  Electron, no Chromium, no embedded Rails or Node.
- **Open.** MIT licensed original code, buildable and testable without a
  Writero account, private source, or paid services.
- **Honest AI.** Provider capabilities are explicit. Personal keys and local
  models never pass through Writero, and hosted credits are never spent
  silently.

## Development

```sh
cmake -B build -G Ninja -DWRITERO_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests are Qt Test (C++ core) and Qt Quick Test (UI). No network access is
required for the default suite. Opt-in provider tests are documented in
`docs/verification.md`.

## License

MIT. See `LICENSE`. Third-party assets and libraries are listed in
`THIRD_PARTY_NOTICES` once they are introduced.
