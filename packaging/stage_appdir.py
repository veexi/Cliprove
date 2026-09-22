#!/usr/bin/env python3
"""Stage a Qt AppDir without rewriting Fedora ELF files."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


PLUGIN_ROOT = Path("/usr/lib64/qt6/plugins")
PLUGINS = (
    "platforms/libqwayland.so",
    "platforms/libqxcb.so",
    "sqldrivers/libqsqlite.so",
    "imageformats/libqgif.so",
    "imageformats/libqjpeg.so",
    "imageformats/libqwebp.so",
    "platforminputcontexts/libcomposeplatforminputcontextplugin.so",
    "platformthemes/libqxdgdesktopportal.so",
    "wayland-graphics-integration-client/libqt-plugin-wayland-egl.so",
    "wayland-shell-integration/libxdg-shell.so",
    "xcbglintegrations/libqxcb-egl-integration.so",
    "xcbglintegrations/libqxcb-glx-integration.so",
)
SYSTEM_LIBRARIES = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
    "librt.so.1", "libresolv.so.2", "libutil.so.1",
    "libEGL.so.1", "libGLX.so.0", "libGLdispatch.so.0",
    "libOpenGL.so.0", "libGL.so.1",
}
DEPENDENCY = re.compile(r"^\s*(\S+)\s+=>\s+(/\S+)\s+\(")


def dependencies(path: Path) -> list[tuple[str, Path]]:
    result = subprocess.run(["ldd", str(path)], text=True, capture_output=True, check=True)
    found = []
    for line in result.stdout.splitlines():
        if "=> not found" in line:
            raise RuntimeError(f"Missing dependency for {path}: {line.strip()}")
        match = DEPENDENCY.match(line)
        if match:
            found.append((match.group(1), Path(match.group(2))))
    return found


def stage(executable: Path, appdir: Path) -> None:
    if appdir.exists():
        raise RuntimeError(f"AppDir already exists: {appdir}")
    project = Path(__file__).resolve().parent.parent
    binary_dir = appdir / "usr/bin"
    library_dir = appdir / "usr/lib"
    plugin_dir = appdir / "usr/plugins"
    desktop_dir = appdir / "usr/share/applications"
    icon_dir = appdir / "usr/share/icons/hicolor/256x256/apps"
    for directory in (binary_dir, library_dir, plugin_dir, desktop_dir, icon_dir):
        directory.mkdir(parents=True, exist_ok=True)

    shutil.copy2(executable, binary_dir / "cliptool")
    pending = [executable]
    for name in PLUGINS:
        source = PLUGIN_ROOT / name
        if not source.is_file():
            raise RuntimeError(f"Missing Qt plugin: {source}")
        destination = plugin_dir / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        pending.append(source)

    copied = set()
    while pending:
        source = pending.pop()
        for name, location in dependencies(source):
            if name in SYSTEM_LIBRARIES or name.startswith("ld-linux") or name in copied:
                continue
            shutil.copy2(location, library_dir / name)
            copied.add(name)
            pending.append(location)

    (binary_dir / "qt.conf").write_text(
        "[Paths]\nPrefix = ../\nPlugins = plugins\n", encoding="utf-8"
    )
    apprun = appdir / "AppRun"
    apprun.write_text(
        "#!/bin/sh\n"
        "set -eu\n"
        'appdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)\n'
        'export LD_LIBRARY_PATH="$appdir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
        'export QT_PLUGIN_PATH="$appdir/usr/plugins"\n'
        'export QT_QPA_PLATFORM_PLUGIN_PATH="$appdir/usr/plugins/platforms"\n'
        'exec "$appdir/usr/bin/cliptool" "$@"\n',
        encoding="utf-8",
    )
    apprun.chmod(0o755)
    shutil.copy2(project / "packaging/cliprove.desktop", desktop_dir / "cliprove.desktop")
    shutil.copy2(project / "packaging/cliprove.png", icon_dir / "cliprove.png")
    os.symlink("usr/share/applications/cliprove.desktop", appdir / "cliprove.desktop")
    os.symlink("usr/share/icons/hicolor/256x256/apps/cliprove.png", appdir / "cliprove.png")
    os.symlink("cliprove.png", appdir / ".DirIcon")
    print(f"Staged {len(copied)} shared libraries in {appdir}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: stage_appdir.py RELEASE_BINARY APPDIR")
    stage(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
