#!/usr/bin/env python3
"""Check an unpacked release for developer paths and accidentally bundled local files."""
import argparse
import os
from pathlib import Path


def audit(root, extra_roots=()):
    """Return relative filenames and issue types without printing private embedded data."""
    prefixes = ["/home/", "/Users/", "/mnt/", "/media/", str(Path.home()),
                str(Path(__file__).resolve().parents[1]), *extra_roots]
    prefixes += [f"{drive}:{slash}Users{slash}" for drive in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                 for slash in ("/", "\\")]
    needles = {prefix.encode(encoding) for prefix in prefixes if len(prefix) > 1
               for encoding in ("utf-8", "utf-16-le", "utf-16-be")}
    issues = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if path.is_symlink():
            target = os.readlink(path)
            if os.path.isabs(target) or not path.resolve().is_relative_to(root.resolve()):
                issues.append((relative, "link points outside release"))
            continue
        if not path.is_file():
            continue
        if path.name in {"Settings.json", "CMakeCache.txt", "build_log.txt"} or path.suffix in {".pyc", ".pyo"}:
            issues.append((relative, "local settings or generated development file"))
        data = path.read_bytes()
        if any(needle in data for needle in needles):
            issues.append((relative, "embedded developer path"))
    return issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Staged install or unpacked release")
    parser.add_argument("--forbid-root", action="append", default=[], help="Additional private build/SDK root")
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error("directory must exist")
    issues = audit(args.directory, args.forbid_root)
    for path, reason in issues:
        print(f"{path}: {reason}")
    print(f"Release path audit: {len(issues)} issue(s)")
    return bool(issues)


if __name__ == "__main__":
    raise SystemExit(main())
