#!/usr/bin/env python3
"""Package compiled Core ML models as individual ZIPs with SHA-256 sidecars.

Each archive contains one top-level .mlmodelc directory with the name expected
by Rendepth. The sidecar format matches Packaging/Models/*.zip.sha256.
"""

import argparse
import hashlib
from pathlib import Path
import shutil
import zipfile


MODELS = (
    "DA2-SMALL-280",
    "DA2-SMALL-336",
    "DA2-SMALL-392",
    "DA2-SMALL-560",
    "DA2-BASE-644",
    "DA2-LARGE-714",
    "RFDN_x4",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def archive_model(model: str, source_dir: Path, output_dir: Path) -> None:
    compiled = source_dir / f"{model}.mlmodelc"
    if not compiled.is_dir() or not (compiled / "model.mil").is_file():
        raise ValueError(f"Compiled Core ML model is missing: {compiled}")
    files = sorted(path for path in compiled.rglob("*") if path.is_file())
    if not files or any(path.is_symlink() for path in compiled.rglob("*")):
        raise ValueError(f"Model is empty or contains symlinks: {compiled}")

    archive = output_dir / f"{model}-coreml.zip"
    sidecar = output_dir / f"{archive.name}.sha256"
    if archive.exists() or sidecar.exists():
        raise FileExistsError(f"Refusing to replace existing archive or checksum: {archive}")
    temporary = output_dir / f".{archive.name}.partial"
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=True) as output:
            for source in files:
                member = f"{compiled.name}/{source.relative_to(compiled).as_posix()}"
                info = zipfile.ZipInfo(member, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                with source.open("rb") as input_file, output.open(info, "w",
                                                                     force_zip64=True) as output_file:
                    shutil.copyfileobj(input_file, output_file, 1024 * 1024)
        with zipfile.ZipFile(temporary) as check:
            expected = {f"{compiled.name}/{p.relative_to(compiled).as_posix()}" for p in files}
            if set(check.namelist()) != expected or check.testzip() is not None:
                raise ValueError(f"Archive verification failed: {temporary}")
        temporary.rename(archive)
        digest = sha256_file(archive)
        sidecar.write_text(f"{digest}  {archive.name}\n", encoding="ascii")
        print(f"{archive.name}: {archive.stat().st_size:,} bytes, SHA-256 {digest}",
              flush=True)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--models-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for model in MODELS:
        archive_model(model, args.models_dir, args.output_dir)


if __name__ == "__main__":
    main()
