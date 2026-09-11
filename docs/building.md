# Building Writero Desktop

## Requirements

| Dependency | Minimum | Purpose |
|---|---|---|
| CMake | 3.21 | Build system |
| Ninja | any | Recommended generator |
| C++ compiler | GCC 12 / Clang 16 (C++20) | Application core |
| Qt | 6.5 | UI, networking, storage, tests |
| SQLite | 3.35 | Local workspace database (via Qt Sql) |
| libsecret | 0.20 | Secret Service credential storage |

### Arch Linux / Omarchy

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative \
    qt6-svg libsecret sqlite
```

### Fedora

```sh
sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel \
    qt6-qtdeclarative-devel qt6-qtsvg-devel libsecret-devel sqlite-devel
```

### Debian / Ubuntu (24.04+)

```sh
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-declarative-dev \
    qt6-svg-dev libsecret-1-dev libsqlite3-dev
```

## Configure and build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful options:

| Option | Default | Meaning |
|---|---|---|
| `WRITERO_BUILD_TESTS` | `ON` | Build the Qt Test / Qt Quick Test suites |

## Run

```sh
./build/writero-desktop
```

On a headless machine, tests run with `QT_QPA_PLATFORM=offscreen`; the
application itself needs a Wayland or X11 session.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

The default suite uses only the local filesystem and stub servers. Real
provider calls are opt-in; see `docs/verification.md`.

## Install

```sh
cmake --install build --prefix /usr/local
```

Packaging for Arch lives in `packaging/arch/`. The desktop entry is installed
as `app.writero.Writero.desktop`.

## Troubleshooting

- **`Qt6QmlMacros` not found** — install `qt6-declarative`; the QML module
  macros ship with it.
- **SQLite driver missing** — the `QSQLITE` plugin ships with `qt6-base`.
  Verify with `QT_DEBUG_PLUGINS=1 ./build/writero-desktop` if storage fails
  to open.
- **Credentials not persisted** — ensure a Secret Service provider (GNOME
  Keyring, KWallet with the Secret Service bridge) is running. Without one,
  the app offers session-only credentials instead of writing plaintext.
