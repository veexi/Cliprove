# AppImage

Cliprove can be packaged as a **thin KDE Plasma 6 AppImage**.

The final Cliprove UI imports KDE's private clipboard QML module and uses PlasmaQuick/KWayland/KGlobalAccel. To avoid ABI mismatches, the AppImage intentionally uses the host system's Qt/KF6/Plasma runtime instead of bundling another Plasma stack.

That means the AppImage is convenient for compatible Plasma 6 systems, but it is not a universal desktop-independent package.

## Build

Build the two final binaries first:

```sh
cmake -S . -B build/appimage-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/appimage-release --target cliprove-popup cliprove-paste-daemon -j2
```

Stage the AppDir:

```sh
python3 packaging/stage_appdir.py \
  build/appimage-release/cliprove-popup \
  build/appimage-release/cliprove-paste-daemon \
  build/Cliprove.AppDir
```

Create the AppImage:

```sh
ARCH=x86_64 appimagetool build/Cliprove.AppDir dist/Cliprove-0.2.0-plasma6-x86_64.AppImage
```

## Runtime requirements

- KDE Plasma 6 with the `org.kde.plasma.private.clipboard` QML module;
- compatible Qt 6 / KF6 / Plasma runtime;
- `qdbus`;
- XDG Desktop Portal RemoteDesktop support;
- libei/liboeffis.

Launching the AppImage starts both the popup process and the paste helper. If an installed Cliprove instance is already running, launching the AppImage simply asks that instance to show its popup.
