# SteamOS build and deployment notes

This document records the issues found while bringing the same Cliprove `main` branch to SteamOS, so a later rebuild does not repeat the original investigation.

## Known-good environment

Verified on 2026-09-22:

- SteamOS 3.8.16
- KDE Plasma 6.4.3
- Qt 6.9.1
- Wayland / x86_64
- source branch: `main`

SteamOS and Fedora/Bazzite use the same source tree. There is no SteamOS-only branch.

## Release policy

Prefer stability over a universal binary.

Cliprove links to PlasmaQuick, KWayland and KGlobalAccel and imports KDE's private clipboard QML module. A prebuilt binary can stop loading after a Plasma ABI/SONAME change even when the source code itself is still valid.

Preferred upgrade path:

1. update the OS;
2. try the existing Cliprove binary;
3. if the loader reports a Plasma/KF6 ABI mismatch, rebuild the same source on that system;
4. change source only if KDE actually changed an API used by Cliprove.

Do not redesign Cliprove merely to make one AppImage span unrelated Plasma ABIs.

## Do not mutate the SteamOS base system

SteamOS has the runtime libraries Cliprove needs, but it is not intended to be turned into a conventional development workstation by unlocking the root filesystem and installing a large set of development packages.

The first successful build used a private sysroot:

```text
~/.cache/cliprove-steamos-sysroot/
├── pkgs/
└── root/
```

Packages were downloaded with `pacman -Sp` and extracted with `bsdtar`; no development packages were installed into the immutable SteamOS root.

The package set used during the first successful build was:

```text
qt6-base qt6-declarative qt6-wayland qt6-5compat
libplasma kwayland kglobalaccel
kconfig kcoreaddons kcolorscheme kguiaddons ki18n kiconthemes
kio kirigami knotifications kpackage ksvg kwidgetsaddons kwindowsystem
plasma-activities karchive kcodecs kcompletion kjobwidgets kservice
kxmlgui solid libei extra-cmake-modules
wayland libxcb xcb-util-keysyms libglvnd libffi libxau libxdmcp
xorgproto systemd-libs
cmake ninja pkgconf rhash libuv cppdap jsoncpp
```

This is a record of the known-good build, not a promise that future SteamOS releases will use identical package names.

## Recreate the local sysroot

```bash
BASE="$HOME/.cache/cliprove-steamos-sysroot"
mkdir -p "$BASE/pkgs" "$BASE/root"

packages=(
  qt6-base qt6-declarative qt6-wayland qt6-5compat
  libplasma kwayland kglobalaccel
  kconfig kcoreaddons kcolorscheme kguiaddons ki18n kiconthemes
  kio kirigami knotifications kpackage ksvg kwidgetsaddons kwindowsystem
  plasma-activities karchive kcodecs kcompletion kjobwidgets kservice
  kxmlgui solid libei extra-cmake-modules
  wayland libxcb xcb-util-keysyms libglvnd libffi libxau libxdmcp
  xorgproto systemd-libs
  cmake ninja pkgconf rhash libuv cppdap jsoncpp
)

for pkg in "${packages[@]}"; do
    url=$(pacman -Sp --print-format '%n|%l' "$pkg" 2>/dev/null |
        awk -F'|' -v p="$pkg" '$1==p { print $2; exit }')
    test -n "$url" || { echo "No package URL for $pkg"; exit 1; }

    file="$BASE/pkgs/${url##*/}"
    if [[ ! -f "$file" ]]; then
        curl -L --fail --retry 2 "$url" -o "$file"
    fi
    bsdtar -xf "$file" -C "$BASE/root"
done
```

If SteamOS changes repositories or package names, fix only this bootstrap step first. Do not modify Cliprove source merely because a development package moved.

## Configure and build

The first working SteamOS build used the SteamOS headers/package metadata while writing everything into the user's home directory.

```bash
cd ~/Cliprove

SYS="$HOME/.cache/cliprove-steamos-sysroot/root"

export PATH="$SYS/usr/bin:$HOME/.local/bin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$SYS/usr/lib:/usr/lib"
export CC="$HOME/.local/bin/gcc"
export CXX="$HOME/.local/bin/g++"

export CMAKE_PREFIX_PATH="$SYS/usr"
export CMAKE_INCLUDE_PATH="$SYS/usr/include"
export CMAKE_LIBRARY_PATH="$SYS/usr/lib"

export PKG_CONFIG_LIBDIR="$SYS/usr/lib/pkgconfig:$SYS/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SYS"

"$SYS/usr/bin/cmake" -S . -B build/steamos -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$SYS/usr/bin/ninja" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DCMAKE_SKIP_RPATH=ON \
  -DQt6_DIR="$SYS/usr/lib/cmake/Qt6" \
  -DPlasmaQuick_DIR="$SYS/usr/lib/cmake/PlasmaQuick" \
  -DKWayland_DIR="$SYS/usr/lib/cmake/KWayland" \
  -DKF6GlobalAccel_DIR="$SYS/usr/lib/cmake/KF6GlobalAccel"

"$SYS/usr/bin/cmake" --build build/steamos \
  --target cliprove-popup cliprove-paste-daemon -j4
```

`CMAKE_SKIP_RPATH=ON` matters. The final binaries should use the installed SteamOS runtime, not retain a RUNPATH into the temporary sysroot.

Check before deployment:

```bash
ldd build/steamos/cliprove-popup | grep 'not found'
ldd build/steamos/cliprove-paste-daemon | grep 'not found'
readelf -d build/steamos/cliprove-popup | grep -E 'RPATH|RUNPATH'
```

The first two commands should print nothing.

## Plasma 6.4 vs 6.7 findings

### `Plasma::setupPlasmaStyle()`

The Fedora/Bazzite build uses Plasma 6.7, where `Plasma::setupPlasmaStyle()` exists.

SteamOS Plasma 6.4 does not provide it.

The source now uses a compile-time version guard:

- Plasma 6.7+ calls `setupPlasmaStyle()`;
- Plasma 6.4 skips it.

Do not remove this guard unless the minimum supported Plasma version is raised.

### Standalone clipboard QML changed

The Plasma 6.7 clipboard QML expects standalone pieces that are not available in the SteamOS 6.4 environment, including:

```text
org.kde.plasma.plasmoid
PlasmaExtras.Representation
PlasmaExtras.PlasmoidHeading
```

Typical errors were:

```text
module "org.kde.plasma.plasmoid" is not installed
Type PlasmaExtras.Representation unavailable
Type PlasmaExtras.PlasmoidHeading unavailable
```

Cliprove therefore embeds two popup paths:

- Plasma 6.7+ uses the normal popup QML;
- older Plasma 6.x uses `src/popup/legacy/`.

The legacy layer is intentionally small. It replaces the incompatible standalone wrapper/header types while keeping clipboard history, Ctrl-click selection, Enter multi-paste and direct-paste behavior shared.

Do not copy the Plasma 6.7 popup QML over the legacy QML on SteamOS 6.4.

## Wayland/KWin position behavior on Plasma 6.4

This was the most misleading issue in the first port.

On SteamOS Plasma 6.4, Cliprove can report its own `QWindow::x()/y()` as `0,0` even when KWin has actually positioned the window correctly.

During verification KWin reported the real popup geometry as:

```text
x=956 y=356 width=648 height=728
```

while the client still reported `0,0`.

Therefore:

- do not diagnose the popup as "stuck at 0,0" from client-side geometry alone;
- use KWin as the authority for actual placement on Plasma 6.4;
- use a KWin `Remember` rule for `org.veexi.cliprove-popup`.

Known-good rule shape:

```ini
[cliprove-position]
Description=Cliprove remembered position
position=<initial centered x>,<initial centered y>
positionrule=4
types=1
wmclass=org.veexi.cliprove-popup
wmclasscomplete=false
wmclassmatch=1
```

After editing `~/.config/kwinrulesrc`:

```bash
qdbus org.kde.KWin /KWin org.kde.KWin.reconfigure
```

KWin's `Remember` rule can track a user-moved position even if Cliprove's own `QSettings` value remains `@Point(0 0)` on this Plasma generation.

## Global shortcut issue

KGlobalAccel can autoload an old or empty shortcut registration. A bad state looks like:

```ini
show-cliprove=,Meta+V,Show Cliprove
```

The current source explicitly reassigns Meta+V with `KGlobalAccel::NoAutoloading`.

Desired state:

```ini
show-cliprove=Meta+V,Meta+V,Show Cliprove
```

The stock KDE Clipboard shortcut must remain disabled:

```ini
show-on-mouse-pos=none,Meta+V,...
```

Check with:

```bash
grep -n -B1 -A1 -E 'show-cliprove=|show-on-mouse-pos=' \
  ~/.config/kglobalshortcutsrc
```

## Paste helper / portal permission

After restarting or reinstalling the paste helper it can briefly report:

```text
Waiting for keyboard input permission...
```

Do not immediately treat this as a failure. On the successful SteamOS deployment it became ready after a few seconds.

Verify:

```bash
qdbus org.veexi.CliprovePaste /Paste org.veexi.CliprovePaste.ready
qdbus org.veexi.CliprovePaste /Paste org.veexi.CliprovePaste.status
```

Expected:

```text
true
Direct paste ready
```

If it stays in the waiting state, check for an XDG Desktop Portal RemoteDesktop/keyboard permission prompt.

## Deploy the binaries

```bash
cd ~/Cliprove

install -Dm755 build/steamos/cliprove-popup \
  ~/.local/bin/cliprove-popup

install -Dm755 build/steamos/cliprove-paste-daemon \
  ~/.local/bin/cliprove-paste-daemon

install -Dm755 packaging/cliprove-shortcut-fix.sh \
  ~/.local/bin/cliprove-shortcut-fix

install -Dm644 packaging/cliprove-popup.service \
  ~/.config/systemd/user/cliprove-popup.service

install -Dm644 packaging/cliprove-paste-daemon.service \
  ~/.config/systemd/user/cliprove-paste-daemon.service

mkdir -p ~/.config/systemd/user/plasma-plasmashell.service.d
install -Dm644 packaging/90-cliprove-shortcut.conf \
  ~/.config/systemd/user/plasma-plasmashell.service.d/90-cliprove-shortcut.conf

systemctl --user daemon-reload
systemctl --user enable --now \
  cliprove-popup.service cliprove-paste-daemon.service

~/.local/bin/cliprove-shortcut-fix
```

## Final verification checklist

Run this before starting any new debugging:

```bash
systemctl --user is-enabled cliprove-popup.service cliprove-paste-daemon.service
systemctl --user is-active cliprove-popup.service cliprove-paste-daemon.service

qdbus org.veexi.CliprovePaste /Paste org.veexi.CliprovePaste.ready
qdbus org.veexi.CliprovePaste /Paste org.veexi.CliprovePaste.status

grep -n -B1 -A1 -E 'show-cliprove=|show-on-mouse-pos=' \
  ~/.config/kglobalshortcutsrc

journalctl --user -u cliprove-popup.service -n 30 --no-pager
journalctl --user -u cliprove-paste-daemon.service -n 30 --no-pager
```

Expected high-level result:

- both services: enabled and active;
- paste helper: `true` / `Direct paste ready`;
- `show-cliprove=Meta+V,Meta+V,...`;
- KDE stock `show-on-mouse-pos=none,...`;
- popup log reaches `QQmlComponent::Ready`.

## Fast path after a future SteamOS update

If the old binary stops starting after an OS update:

1. `git pull --ff-only`;
2. check `plasmashell --version`;
3. refresh the local sysroot from that SteamOS release;
4. rebuild the same source;
5. redeploy the two binaries;
6. restart the two user services;
7. run the verification checklist above.

Only investigate source changes if compilation or QML loading still fails after rebuilding against the new system ABI.
