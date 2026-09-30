#!/usr/bin/env python3
"""Package an exported build directory into a release archive.

Usage: package_build.py <runner-os> <artifact-name> <build-dir> <dist-dir>

runner-os is the GitHub Actions value ("Windows", "Linux", "macOS"). Linux gets
a .tar.gz rooted at Solarius/, everything else a .zip with the files at the top
level. Uses the standard library rather than zip(1)/tar(1) because Git for
Windows ships no zip binary at all.
"""
import pathlib
import sys
import tarfile
import zipfile


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: package_build.py <runner-os> <artifact> <build-dir> <dist-dir>", file=sys.stderr)
        return 2

    runner_os, artifact, build_dir, dist_dir = sys.argv[1:5]
    src = pathlib.Path(build_dir)
    dest = pathlib.Path(dist_dir)

    files = sorted(p for p in src.iterdir() if p.is_file())
    if not files:
        print(f"nothing to package in {src}", file=sys.stderr)
        return 1

    dest.mkdir(parents=True, exist_ok=True)

    if runner_os == "Linux":
        out = dest / f"{artifact}.tar.gz"
        with tarfile.open(out, "w:gz") as tar:
            for f in files:
                tar.add(f, arcname=f"Solarius/{f.name}")
    else:
        out = dest / f"{artifact}.zip"
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for f in files:
                z.write(f, arcname=f.name)

    print("wrote", out, out.stat().st_size, "bytes")
    for f in files:
        print(f"  {f.name} {f.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
