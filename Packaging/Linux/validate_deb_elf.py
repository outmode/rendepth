#!/usr/bin/env python3
"""Reject a DEB whose ELF files cannot run on the Ubuntu 24.04 baseline."""

import re
import subprocess
import sys
from pathlib import Path


BASELINE = {"GLIBC": (2, 39), "GLIBCXX": (3, 4, 32), "CXXABI": (1, 3, 14)}
VERSION = re.compile(r"Name: (GLIBCXX|GLIBC|CXXABI)_([0-9.]+)")
NEEDED = re.compile(r"Shared library: \[([^]]+)\]")
RPATH = re.compile(r"(?:Library runpath|Library rpath): \[([^]]+)\]")


def readelf(path: Path, flag: str) -> str:
    result = subprocess.run(
        ["readelf", flag, str(path)], capture_output=True, text=True, check=False
    )
    if result.returncode:
        raise RuntimeError(f"readelf failed for {path}: {result.stderr.strip()}")
    return result.stdout


def check(app_root: Path) -> list[str]:
    binary = app_root / "Binary/Rendepth"
    if not binary.is_file():
        return [f"missing staged executable: {binary}"]

    errors = []
    elf_files = []
    for directory in ("Binary", "Library", "Runtimes"):
        for path in sorted((app_root / directory).rglob("*")):
            if path.is_symlink() or not path.is_file():
                continue
            with path.open("rb") as source:
                if source.read(4) == b"\x7fELF":
                    elf_files.append(path)

    for path in elf_files:
        versions = readelf(path, "--version-info")
        for name, version in VERSION.findall(versions):
            if name in BASELINE and tuple(map(int, version.split("."))) > BASELINE[name]:
                errors.append(f"{path.relative_to(app_root)} needs {name}_{version}; "
                              f"Ubuntu 24.04 baseline is {name}_{'.'.join(map(str, BASELINE[name]))}")
        dynamic = readelf(path, "-d")
        for runpath in RPATH.findall(dynamic):
            for entry in runpath.split(":"):
                if entry and not entry.startswith(("$ORIGIN", "${ORIGIN}")):
                    errors.append(f"{path.relative_to(app_root)} has non-relative RPATH: {entry}")
        for needed in NEEDED.findall(dynamic):
            if "/" in needed:
                errors.append(f"{path.relative_to(app_root)} needs a library by path: {needed}")

    needed = set(NEEDED.findall(readelf(binary, "-d")))
    for soname in ("libbluray.so.2", "libdvdread.so.8"):
        if soname not in needed:
            errors.append(f"Binary/Rendepth must link {soname} for the Ubuntu 24.04 DEB")

    for path in elf_files:
        loader = subprocess.run(["ldd", str(path)], capture_output=True, text=True, check=False)
        output = loader.stdout + loader.stderr
        if loader.returncode or "not found" in output:
            problems = [line.strip() for line in output.splitlines()
                        if "not found" in line or "version `" in line]
            errors.append(f"{path.relative_to(app_root)} has unresolved dependencies: " +
                          "; ".join(problems or [output.strip()]))
    return errors


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate_deb_elf.py <staged-app-root>", file=sys.stderr)
        return 2
    try:
        errors = check(Path(sys.argv[1]))
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 2
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("DEB ELF dependencies and Ubuntu 24.04 ABI baseline verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
