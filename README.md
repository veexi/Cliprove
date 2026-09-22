# Cliprove

Cliprove is an experimental native clipboard history manager for Linux. It saves multiple MIME formats for each clipboard item, lets you search past items, and can paste a selected item directly into the previous window.

## Current status

- KDE Plasma on Wayland: clipboard capture, multi-format history, search, restore, and direct paste have been tested.
- X11: Qt clipboard capture and XTest direct paste passed an isolated Xvfb/Openbox end-to-end test; testing on a regular X11 desktop remains to be done.
- GNOME on Wayland: a Clipboard Portal backend is experimental and is not yet connected as a fallback.
- Starred entries are stored in SQLite and shown first. Right-click an entry to star it, or select it and press Ctrl+D. Clicking an entry restores it, hides the window, and requests direct paste. These new UI interactions still need a KDE desktop acceptance test.

The window is named Cliprove. The executable and application data directory still use the development name `ClipTool`, preserving the existing history under `veexi/ClipTool`.

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

The initial release target is KDE Plasma on Wayland and X11. GNOME Portal fallback is deferred. Multi-entry paste, image thumbnails, settings and release packaging are still pending. File history stores clipboard metadata such as file URIs, not file contents.
