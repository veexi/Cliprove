# AppImage preview

Build on Fedora x86_64 with Qt 6 development packages, Qt Wayland, Qt image formats, SQLite driver, and AppImage `appimagetool` installed:

```sh
cmake -S . -B build/appimage-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/appimage-release -j2
python3 packaging/stage_appdir.py build/appimage-release/cliptool build/Cliprove.AppDir
ARCH=x86_64 appimagetool build/Cliprove.AppDir dist/Cliprove-0.1.0-preview-x86_64.AppImage
```

The AppDir copies runtime libraries and Qt plugins from the build environment. Build on the oldest distribution you intend to support, and test the resulting AppImage on each target desktop. This preview was built on Fedora 44 and has only been smoke tested in an isolated X11 session; KDE Wayland acceptance remains pending.
