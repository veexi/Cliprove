#!/bin/bash
set -euo pipefail

systemctl --user disable --now cliprove-popup.service cliprove-paste-daemon.service 2>/dev/null || true

if command -v qdbus >/dev/null 2>&1; then
    qdbus org.kde.kglobalaccel /kglobalaccel org.kde.KGlobalAccel.unregister cliprove-popup show-cliprove >/dev/null 2>&1 || true
fi

rm -f "$HOME/.local/bin/cliprove-popup" \
      "$HOME/.local/bin/cliprove-paste-daemon" \
      "$HOME/.local/bin/cliprove-shortcut-fix" \
      "$HOME/.config/systemd/user/cliprove-popup.service" \
      "$HOME/.config/systemd/user/cliprove-paste-daemon.service" \
      "$HOME/.config/systemd/user/plasma-plasmashell.service.d/90-cliprove-shortcut.conf"

systemctl --user daemon-reload

echo "Cliprove removed. KDE Clipboard can be re-enabled from System Settings if desired."
