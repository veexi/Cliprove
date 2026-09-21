# Cliprove

Cliprove is an experimental native clipboard history manager for Linux. It saves multiple MIME formats for each clipboard item, lets you search past items, and can paste a selected item directly into the previous window.

## Current status

- KDE Plasma on Wayland: clipboard capture, multi-format history, search, restore, and direct paste have been tested.
- X11: Qt clipboard capture and XTest paste backends are implemented but need full desktop testing.
- GNOME on Wayland: a Clipboard Portal backend is experimental and is not yet connected as a fallback.

The application and executable still use the development name `ClipTool`. The stored history currently lives under the Qt application data directory for `veexi/ClipTool`.

## Build

Requires CMake 3.24+, Ninja, a C++23 compiler, Qt 6.6+ (Core, Gui, Widgets, Sql), SQLite support for Qt, Wayland client libraries and scanner, XCB with XTest and keysyms, and `libei-1.0` / `liboeffis-1.0`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
./build/cliptool
```

On the current SteamOS development machine, build inside the `cliptool-dev` distrobox and set `PATH=/usr/bin:/bin` to avoid old host toolchain shims.

## Implementation

- `src/core`: SQLite history with SHA-256 deduplication and MIME payload storage.
- `src/platform`: Wayland data-control, X11 clipboard, and direct paste backends.
- `src/ui`: Qt Widgets proof-of-concept interface.
- `protocols`: Wayland `ext-data-control-v1` protocol definition.

The active Wayland path uses `ext-data-control-v1` for clipboard access and the RemoteDesktop Portal with libei for direct paste. Experimental Portal clipboard and D-Bus paste code is present but is not part of the current build.
