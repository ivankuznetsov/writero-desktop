# Contributing

Thanks for helping build Writero Desktop.

## Development setup

1. Install the build dependencies for your distribution:

   **Arch Linux / Omarchy**

   ```sh
   sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative \
       qt6-svg libsecret sqlite
   ```

2. Configure and build:

   ```sh
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ```

3. Run the test suite:

   ```sh
   ctest --test-dir build --output-on-failure
   ```

4. Run the application:

   ```sh
   ./build/writero-desktop
   ```

## Guidelines

- Keep the storage format versioned. Every schema change needs a migration
  and a recovery test.
- The document core is authoritative; UI code never writes storage directly.
  All mutations go through document commands so undo, revision history, and
  the pending-operation queue stay consistent.
- Prefer small, focused commits with a clear message. Match the format used
  in the history: `area: imperative summary`.
- Do not add placeholder implementations for user-visible features. A block
  type either works or it is not offered.
- Network tests must not run by default. They belong behind an explicit
  opt-in (see `docs/verification.md`).
- Never commit credentials, wording from private Writero prompts, or user
  documents. Probe fixtures are synthetic.

## Testing expectations

| Change | Required evidence |
|---|---|
| Document/storage | `ctest` unit tests, including fault/recovery cases |
| Editor UI | Qt Quick Test plus a manual pass on Omarchy |
| Providers | Contract tests with a local stub server; opt-in real-provider run |
| Import/export | Golden fixture round trips |

## Code style

- C++20, Qt conventions (camelCase methods, lowercase members with `m_`).
- QML: components in `qml/`, one component per file, no inline styles —
  colors and metrics come from `Theme.qml`.
- Prefer Qt's own types and containers at API boundaries.

## License

By contributing you agree that your contributions are licensed under the MIT
License (see `LICENSE`).
