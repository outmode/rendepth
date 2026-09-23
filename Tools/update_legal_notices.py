#!/usr/bin/env python3
"""Rebuild the application license and consolidated dependency notices, without network access."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ROOT / "Tools/Legal"
RUNTIME_MARKER = "=== CONFIGURED ONNX RUNTIME NOTICES ==="


def section(title, path, entry=None):
    """Preserve upstream text and identify its checked-in input without local absolute paths."""
    data = (ROOT / path).read_bytes()
    text = data.decode("utf-8-sig")
    entry = entry or {}
    if entry.get("license_tail"):
        start = text.index(entry["license_tail"])
        text = text[start:text.index("*/", start)].rstrip()
    elif entry.get("copyright_comment"):
        text = next(comment for comment in re.findall(r"/\*.*?\*/", text, re.DOTALL) if "Copyright" in comment)
    elif entry.get("extract_header"):
        start = text.index("/*")
        text = text[start:text.index("*/", start) + 2]
    return (f"\n--- {title} ---\nSource: {path}\n"
            f"Source SHA-256: {hashlib.sha256(data).hexdigest()}\n\n" + text.rstrip() + "\n")


def generate():
    """Assemble deterministically from complete upstream files and labeled embedded-header notices."""
    app = (INPUTS / "app-license-intro.txt").read_text()
    app += "\nORIGINAL RENDEPTH SOURCE CODE: MIT LICENSE\n\n" + (ROOT / "LICENSE").read_text()
    app += "\nCOMBINED APPLICATION: GNU GENERAL PUBLIC LICENSE VERSION 3\n\n"
    app += (ROOT / "ThirdParty/FFmpeg/COPYING.GPLv3").read_text()
    entries = json.loads((INPUTS / "manifest.json").read_text())
    third = (INPUTS / "third-party-intro.txt").read_text()
    third += "\nCONTENTS\n" + "".join(f"{i}. {e['name']}\n" for i, e in enumerate(entries, 1))
    third += f"{len(entries) + 1}. ONNX Runtime CPU SDK\n"
    for i, entry in enumerate(entries, 1):
        third += f"\n{'=' * 72}\n{i}. {entry['name']}\n{entry['note']}\n"
        for url in entry.get("source_urls", []):
            third += f"Source repository: {url}\n"
        if entry.get("source_revision"):
            third += f"Reference revision: {entry['source_revision']}\nReference source: {entry['revision_url']}\n"
        for path in entry.get("artifacts", []):
            third += f"Component file: {path}\nSHA-256: {hashlib.sha256((ROOT / path).read_bytes()).hexdigest()}\n"
        for path in entry["files"]:
            third += section(Path(path).name, path, entry)
    third += "\n" + RUNTIME_MARKER + "\n"
    third += "Source repository: https://github.com/microsoft/onnxruntime\n"
    revision = (INPUTS / "inputs/onnxruntime/GIT_COMMIT_ID").read_text().strip()
    third += f"Reference source: https://github.com/microsoft/onnxruntime/tree/{revision}\n"
    for filename in ("VERSION_NUMBER", "GIT_COMMIT_ID", "LICENSE", "ThirdPartyNotices.txt"):
        third += section(filename, "Tools/Legal/inputs/onnxruntime/" + filename)
    return {"RENDEPTH_APP_LICENSE": app, "THIRD_PARTY_LICENSING": third}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if the committed documents need regeneration")
    args = parser.parse_args()
    stale = []
    for name, text in generate().items():
        path = ROOT / "Legal" / name
        if args.check:
            if not path.exists() or path.read_bytes() != text.encode("utf-8"):
                stale.append(name)
        else:
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(text.encode("utf-8"))
    if stale:
        parser.exit(1, "Regenerate licensing documents: " + ", ".join(stale) + "\n")
    print("Licensing documents are current." if args.check else "Updated Legal licensing documents.")


if __name__ == "__main__":
    main()
