# Cliprove

Cliprove is a KDE Plasma 6 clipboard popup based on KDE's Klipper/Clipboard QML, with two behavioral changes:

- click a history item to paste it directly into the previously focused application;
- hold **Ctrl** and click multiple text items, release Ctrl, then press **Enter** to paste them together in list order, separated by newlines.
- select multiple images the same way to paste each image separately, in list order, from one Enter press.

Everything else intentionally stays close to KDE Clipboard: search, starred items, editing, QR/barcode actions, history model, Plasma styling, and theme integration.

## Current behavior

Cliprove owns **Meta+V** directly through KDE GlobalAccel. It does not use a panel applet or system-tray popup, so the window is not anchored to an auto-hidden panel.

On first launch the popup is centered on the active display. If you move it, the position is saved and restored the next time it opens.

The popup is a native `PlasmaQuick::PlasmaWindow`. Direct paste is injected through the XDG RemoteDesktop portal and libei.

The original window is remembered before opening the popup. Konsole and recognized terminal applications receive **Ctrl+Shift+V**; other applications receive **Ctrl+V**. For an embedded terminal (for example in an editor), use **Shift+click** or **Shift+Enter** to force Ctrl+Shift+V. Release the modifier keys to complete the paste. These shortcuts assume the target application's default paste bindings.

Single text and file entries use Klipper's original MIME formats. Images are restored as PNG. An image batch keeps each image on the clipboard until a read is observed after the paste shortcut, with a short interval between items. Reopening the popup cancels the remaining queue; changing the target window or an unread item stops the queue with an error. Multiple file entries are not yet combined.

When pasting images into a terminal application such as Codex, the image shortcut is Ctrl+V; terminal text still uses Ctrl+Shift+V. The target application must support pasted images.

The per-user installer registers `org.veexi.cliprove-popup.desktop` with an absolute executable path and KDE's window-management interface declaration. This is required for target-window detection and terminal shortcut selection.

The helper starts with the graphical session and retries transient portal failures with a bounded backoff. Failed paste requests are shown in the popup. No delayed paste is queued while permission is unavailable; allow the keyboard-control prompt and click again.

## Regression tests

With `BUILD_TESTING=ON` and Qt Test installed, build `paste-controller-test` and run `ctest -R paste-controller-test --output-on-failure`. It uses a private D-Bus session and an offscreen clipboard. For mouse-selection tests, run the **Qt 6** `qmltestrunner -platform offscreen -input tests/qml` with `QT_QUICK_BACKEND=software`.

## Upstream

The clipboard QML in `plasmoid/contents/ui` is forked from:

- repository: `KDE/plasma-workspace`
- tag: `v6.7.3`
- paths: `applets/clipboard` and `klipper/declarative/qml`
- license: GPL-2.0-or-later

See `plasmoid/UPSTREAM.md`.

## Build

The current target is KDE Plasma 6 on Wayland. The build requires CMake 3.24+, Ninja, a C++23 compiler, Qt 6.6+ (Core, DBus, Gui, Qml, Quick), PlasmaQuick, KWayland, KF6 GlobalAccel, and libei/liboeffis development files.

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --target cliprove-popup cliprove-paste-daemon -j2
```

For a per-user install:

```sh
./packaging/install-user.sh
```

This installs two user services:

- `cliprove-popup.service`: persistent Plasma popup and Meta+V owner;
- `cliprove-paste-daemon.service`: portal/libei paste injector.

The installer also disables KDE's stock `show-on-mouse-pos` Meta+V shortcut so there is no conflict.

### SteamOS

SteamOS uses the same `main` source, but its immutable base system and older Plasma runtime require a local development sysroot plus a few compatibility details. The first successful SteamOS 3.8.16 / Plasma 6.4.3 port is documented in detail in [docs/STEAMOS.md](docs/STEAMOS.md), including the exact sysroot build flow, Plasma 6.4 QML/API differences, KWin position handling, Meta+V registration, paste portal verification, and a fast rebuild checklist for future OS updates.

Read that document before debugging a SteamOS rebuild from scratch.

## AppImage

A KDE-Plasma-specific AppImage can be built with `packaging/stage_appdir.py`. It intentionally uses the host Plasma/Qt/KF6 runtime because Cliprove imports KDE's private clipboard QML module and must stay ABI-compatible with the installed Plasma version.

See `packaging/README.md`. This is therefore a convenient single-file package for compatible Plasma 6 systems, not a desktop-agnostic universal AppImage.

## Source layout

- `src/popup`: standalone Plasma popup, position persistence, GlobalAccel integration;
- `src/paste`: D-Bus paste helper;
- `src/platform/PortalEisPasteBackend.*`: RemoteDesktop portal + libei key injection;
- `plasmoid/contents/ui`: forked KDE Clipboard QML with Ctrl-click multi-select;
- `resources/popup.qrc`: embeds the modified clipboard QML;
- `packaging`: user-service installer and AppImage staging.

The older QWidget proof-of-concept remains in the repository for history, but it is no longer the active Cliprove UI.
