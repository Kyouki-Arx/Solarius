#!/usr/bin/env python3
"""Install Godot export templates into the per-OS location Godot looks in.

Usage: install_templates.py <extracted-templates-dir> <version>

The .tpz archive unpacks to a `templates/` directory, so the first argument
should be the parent of that directory.
"""
import os
import pathlib
import shutil
import sys


def destination_root() -> pathlib.Path:
    if sys.platform == "win32":
        return pathlib.Path(os.environ["APPDATA"]) / "Godot" / "export_templates"
    if sys.platform == "darwin":
        return pathlib.Path.home() / "Library/Application Support/Godot/export_templates"
    return pathlib.Path.home() / ".local/share/godot/export_templates"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: install_templates.py <unpacked-root> <version>", file=sys.stderr)
        return 2

    src = pathlib.Path(sys.argv[1]) / "templates"
    version = sys.argv[2]

    if not src.is_dir():
        print(f"extracted templates not found at {src}", file=sys.stderr)
        return 1

    target = destination_root() / f"{version}.stable"
    target.parent.mkdir(parents=True, exist_ok=True)

    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(src, target)

    print(f"installed export templates to {target}")
    for probe in (
        "windows_release_x86_64.exe",
        "windows_release_x86_32.exe",
        "linux_release.x86_64",
        "macos.zip",
    ):
        print(f"  {probe}: {(target / probe).exists()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
