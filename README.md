# Cliprove

Cliprove is a KDE Plasma 6 clipboard popup based on KDE's Klipper/Clipboard QML, with two behavioral changes:

- click a history item to paste it directly into the previously focused application;
- hold **Ctrl** and click multiple items, then press **Enter** to paste them in list order.

Everything else intentionally stays close to KDE Clipboard: search, starred items, editing, QR/barcode actions, history model, Plasma styling, and theme integration.

## Current behavior

Cliprove owns **Meta+V** directly through KDE GlobalAccel. It does not use a panel applet or system-tray popup, so the window is not anchored to an auto-hidden panel.

On first launch the popup is centered on the active display. If you move it, the position is saved and restored the next time it opens.

The popup is a native `PlasmaQuick::PlasmaWindow`. Direct paste is injected through the XDG RemoteDesktop portal and libei.

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
