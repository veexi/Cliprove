#!/usr/bin/env python3
"""Stage a thin KDE Plasma 6 AppDir for Cliprove.

Cliprove intentionally uses the host Qt/KF6/Plasma runtime so that KDE's
private clipboard QML plugin stays ABI-compatible with the running Plasma.
"""

from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path


def stage(popup: Path, paste: Path, appdir: Path) -> None:
    if appdir.exists():
        shutil.rmtree(appdir)

    project = Path(__file__).resolve().parent.parent
    bindir = appdir / "usr/bin"
    desktopdir = appdir / "usr/share/applications"
    icondir = appdir / "usr/share/icons/hicolor/256x256/apps"
    for directory in (bindir, desktopdir, icondir):
        directory.mkdir(parents=True, exist_ok=True)

    shutil.copy2(popup, bindir / "cliprove-popup")
    shutil.copy2(paste, bindir / "cliprove-paste-daemon")
    shutil.copy2(project / "packaging/cliprove.desktop", desktopdir / "cliprove.desktop")
    shutil.copy2(project / "packaging/cliprove.png", icondir / "cliprove.png")

    runner = """#!/bin/sh
set -eu
appdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)

# If an installed Cliprove instance is already running, just show it.
if command -v qdbus >/dev/null 2>&1 && qdbus org.veexi.CliprovePopup /Popup >/dev/null 2>&1; then
    qdbus org.veexi.CliprovePopup /Popup org.veexi.CliprovePopup.show >/dev/null 2>&1 || true
    exit 0
fi

paste_pid=""
if ! command -v qdbus >/dev/null 2>&1 || ! qdbus org.veexi.CliprovePaste /Paste >/dev/null 2>&1; then
    "$appdir/usr/bin/cliprove-paste-daemon" &
    paste_pid=$!
    sleep 0.35
fi

cleanup() {
    if [ -n "$paste_pid" ]; then
        kill "$paste_pid" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM HUP

"$appdir/usr/bin/cliprove-popup" &
popup_pid=$!

for _ in 1 2 3 4 5 6 7 8 9 10; do
    if command -v qdbus >/dev/null 2>&1 && qdbus org.veexi.CliprovePopup /Popup >/dev/null 2>&1; then
        qdbus org.veexi.CliprovePopup /Popup org.veexi.CliprovePopup.show >/dev/null 2>&1 || true
        break
    fi
    sleep 0.15
done

wait "$popup_pid"
"""
    apprun = appdir / "AppRun"
    apprun.write_text(runner, encoding="utf-8")
    apprun.chmod(0o755)

    launcher = bindir / "cliprove"
    launcher.write_text(
        '#!/bin/sh\nexec "$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)/AppRun" "$@"\n',
        encoding="utf-8",
    )
    launcher.chmod(0o755)

    shutil.copy2(project / "packaging/cliprove.desktop", appdir / "cliprove.desktop")
    shutil.copy2(project / "packaging/cliprove.png", appdir / "cliprove.png")
    os.symlink("cliprove.png", appdir / ".DirIcon")

    print(f"Staged thin Plasma AppDir: {appdir}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Usage: stage_appdir.py CLIPROVE_POPUP PASTE_DAEMON APPDIR")
    stage(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve())
