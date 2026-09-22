#!/bin/bash
set -u
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$(id -u)/bus}"

for _ in $(seq 1 20); do
    qdbus org.kde.kglobalaccel /kglobalaccel >/dev/null 2>&1 && break
    sleep 0.25
done

qdbus org.kde.kglobalaccel /kglobalaccel \
    org.kde.KGlobalAccel.unregister \
    plasmashell show-on-mouse-pos >/dev/null 2>&1 || true

kwriteconfig6 --file "$HOME/.config/kglobalshortcutsrc" \
    --group plasmashell --key show-on-mouse-pos \
    'none,Meta+V,在鼠标位置显示剪贴板项目'
