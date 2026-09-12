#!/usr/bin/env python3
"""Link a host ROCm ORT installation into Rendepth's development pack layout."""
import argparse
from pathlib import Path
import tempfile
import os

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--ort-directory", type=Path, default=Path("/usr/lib64/rocm/lib"))
parser.add_argument("--pack-root", type=Path, default=Path.home() / ".Rendepth/Runtimes")
args = parser.parse_args()
source = args.ort_directory.resolve(strict=True)

cores = {path.resolve(strict=True) for path in source.glob("libonnxruntime.so*") if path.is_file()}
if len(cores) != 1:
    parser.error(f"Expected exactly one ORT core version in {source}; found {len(cores)}")
core = cores.pop()
version = core.name.removeprefix("libonnxruntime.so.")
shared = source / f"libonnxruntime_providers_shared.so.{version}"
if not shared.is_file():
    shared = source / "libonnxruntime_providers_shared.so"
libraries = {
    "libonnxruntime.so.1": core,
    "libonnxruntime_providers_shared.so": shared,
    "libonnxruntime_providers_rocm.so": source / "libonnxruntime_providers_rocm.so",
}
for name, path in libraries.items():
    if not path.is_file():
        parser.error(f"Required library is missing: {path}")

root = args.pack_root.expanduser().absolute()
destination = root / "rocm"
if destination.exists() or destination.is_symlink():
    parser.error(f"Refusing to replace an existing pack: {destination}")
root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix=".rocm-link-", dir=root) as temporary:
    pack = Path(temporary) / "rocm"
    lib = pack / "lib"
    lib.mkdir(parents=True)
    for name, path in libraries.items():
        # Keep the provider alongside the core from the same installation.
        (lib / name).symlink_to(path.absolute())
    (pack / "LOCAL-DEVELOPMENT.txt").write_text(
        f"Linked from {source}. This pack depends on the host ROCm installation.\n"
        "Not suitable for redistribution. Restart Rendepth after installation.\n"
    )
    # Publish the complete layout together. Never replace files in a live pack.
    os.rename(pack, destination)
print(f"Linked local ROCm runtime at {destination}")
print("Restart Rendepth with AMD ROCm selected in Settings.")
