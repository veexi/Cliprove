#!/bin/bash
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/release}"
JOBS="${JOBS:-2}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build "$BUILD_DIR" --target cliprove-popup cliprove-paste-daemon -j"$JOBS"

install -Dm755 "$BUILD_DIR/cliprove-popup" "$HOME/.local/bin/cliprove-popup"
install -Dm755 "$BUILD_DIR/cliprove-paste-daemon" "$HOME/.local/bin/cliprove-paste-daemon"
install -Dm755 "$ROOT/packaging/cliprove-shortcut-fix.sh" "$HOME/.local/bin/cliprove-shortcut-fix"
install -Dm644 "$ROOT/packaging/org.veexi.cliprove-popup.desktop" "$HOME/.local/share/applications/org.veexi.cliprove-popup.desktop"
# KWin matches the real executable path when granting window-management access.
sed -i "s|^Exec=.*|Exec=$HOME/.local/bin/cliprove-popup|" "$HOME/.local/share/applications/org.veexi.cliprove-popup.desktop"
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental >/dev/null 2>&1
fi

install -Dm644 "$ROOT/packaging/cliprove-popup.service" "$HOME/.config/systemd/user/cliprove-popup.service"
install -Dm644 "$ROOT/packaging/cliprove-paste-daemon.service" "$HOME/.config/systemd/user/cliprove-paste-daemon.service"
install -Dm644 "$ROOT/packaging/90-cliprove-shortcut.conf" "$HOME/.config/systemd/user/plasma-plasmashell.service.d/90-cliprove-shortcut.conf"

# Remove the obsolete Cliprove panel applet from older previews if it exists.
if command -v qdbus >/dev/null 2>&1 && qdbus org.kde.plasmashell /PlasmaShell >/dev/null 2>&1; then
    qdbus org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript \
        'for (var p of panels()) for (var w of p.widgets()) if (w.type==="org.veexi.cliprove") w.remove();' \
        >/dev/null 2>&1 || true
fi
if command -v kpackagetool6 >/dev/null 2>&1; then
    kpackagetool6 -t Plasma/Applet -r org.veexi.cliprove >/dev/null 2>&1 || true
fi

systemctl --user daemon-reload
systemctl --user enable --now cliprove-paste-daemon.service cliprove-popup.service

"$HOME/.local/bin/cliprove-shortcut-fix" || true

echo "Cliprove installed. Press Meta+V to open it."
