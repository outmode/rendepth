#!/usr/bin/env python3
"""Build the upload ZIPs for Chrome Web Store and Firefox Add-ons."""

import argparse
import hashlib
import json
from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[2]
FIXED_TIME = (2020, 1, 1, 0, 0, 0)


def package(source: Path, destination: Path, *, chrome: bool) -> None:
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    if chrome:
        # The Web Store assigns the real item ID at draft upload. The key in
        # the checkout only preserves the ID of local unpacked installations.
        manifest.pop("key", None)
    files = sorted(path for path in source.rglob("*") if path.is_file())
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        for path in files:
            name = path.relative_to(source).as_posix()
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            data = (json.dumps(manifest, indent=2).encode("utf-8") + b"\n"
                    if name == "manifest.json" and chrome else path.read_bytes())
            archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED,
                             compresslevel=9)
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None:
            raise ValueError(f"Archive verification failed: {destination}")
        packaged_manifest = json.loads(archive.read("manifest.json"))
        if packaged_manifest["version"] != manifest["version"]:
            raise ValueError(f"Wrong manifest version: {destination}")
        if chrome and "key" in packaged_manifest:
            raise ValueError("Chrome upload unexpectedly contains the local key")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    chrome = ROOT / "Browser/Chrome/extension"
    firefox = ROOT / "Browser/Firefox/extension"
    chrome_version = json.loads((chrome / "manifest.json").read_text())["version"]
    firefox_version = json.loads((firefox / "manifest.json").read_text())["version"]
    if chrome_version != firefox_version:
        parser.error("Chrome and Firefox add-on versions differ")
    output = args.output_dir or ROOT / "Distribution" / chrome_version / "Browser"
    for browser, source, is_chrome in (("chrome", chrome, True),
                                       ("firefox", firefox, False)):
        path = output / f"Rendepth-Companion-{chrome_version}-{browser}-upload.zip"
        package(source, path, chrome=is_chrome)
        print(f"{path}  SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
