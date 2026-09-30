#!/usr/bin/env python3
"""Build an offline Windows x64 CUDA or DirectML runtime pack from local SDKs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile


def check_x64_dll(path):
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) < 64 or header[:2] != b"MZ":
            raise ValueError(f"Not a Windows DLL: {path}")
        stream.seek(struct.unpack_from("<I", header, 60)[0])
        pe = stream.read(24)
    if len(pe) < 24 or pe[:4] != b"PE\0\0" or struct.unpack_from("<H", pe, 4)[0] != 0x8664:
        raise ValueError(f"Expected a Windows x64 DLL: {path}")
    if not struct.unpack_from("<H", pe, 22)[0] & 0x2000:
        raise ValueError(f"Expected a DLL, not an executable: {path}")


def build_pack(backend, ort_dir, dependencies, notice_dirs, output):
    ort_dir, output = ort_dir.resolve(strict=True), output.resolve()
    if output.exists():
        raise ValueError(f"Refusing to replace an existing pack: {output}")
    candidates = [ort_dir / "lib", ort_dir / "runtimes/win-x64/native", ort_dir / "bin"]
    native = next((p for p in candidates if (p / "onnxruntime.dll").is_file()), None)
    if native is None:
        raise ValueError("ORT SDK must contain lib/onnxruntime.dll or runtimes/win-x64/native/onnxruntime.dll")
    ort_names = {"onnxruntime.dll", "onnxruntime_providers_shared.dll"}
    if backend == "cuda":
        ort_names.add("onnxruntime_providers_cuda.dll")
    files = {p.name.lower(): p for p in native.glob("*.dll") if p.name.lower() in ort_names}
    for directory in dependencies:
        for path in directory.resolve(strict=True).glob("*.dll"):
            name = path.name.lower()
            # A dependency directory cannot silently replace the selected ORT.
            if name.startswith("onnxruntime"):
                raise ValueError(f"ORT DLL in dependency directory: {path}")
            prefixes = ("cublas", "cudart", "cudnn", "cufft", "curand", "cusolver", "cusparse", "nvrtc", "nvjitlink", "zlib")
            if (backend == "directml" and name != "directml.dll") or (backend == "cuda" and not name.startswith(prefixes)):
                continue
            if name in files:
                raise ValueError(f"Duplicate DLL: {name}")
            files[name] = path
    required = ["onnxruntime.dll"]
    if backend == "directml":
        required += ["directml.dll"]
        if (native / "onnxruntime_providers_cuda.dll").exists():
            raise ValueError("DirectML pack must use a DirectML ORT SDK, not a CUDA SDK")
    else:
        required += ["onnxruntime_providers_cuda.dll", "onnxruntime_providers_shared.dll"]
        for pattern in ("cublas64_", "cublaslt64_", "cudart64_", "cudnn64_"):
            if not any(name.startswith(pattern) for name in files):
                raise ValueError(f"Missing CUDA dependency {pattern}*.dll; supply CUDA and cuDNN directories")
    for name in required:
        if name not in files:
            raise ValueError(f"Missing required DLL: {name}")
    for path in files.values():
        check_x64_dll(path)
    notices = []
    for directory in [ort_dir, *notice_dirs]:
        directory = directory.resolve(strict=True)
        found = [p for p in directory.iterdir() if p.is_file() and
                 any(token in p.name.lower() for token in ("license", "notice", "eula"))]
        if not found:
            raise ValueError(f"No license/notices found in {directory}")
        notices.append(found)
    if dependencies and not notice_dirs:
        raise ValueError("Supply --notice-dir for the redistributed GPU dependencies")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Stage only within the explicitly selected output parent, then rename.
    with tempfile.TemporaryDirectory(prefix=".rendepth-pack-", dir=output.parent) as temporary:
        stage = Path(temporary).resolve()
        assert stage.parent == output.parent
        pack = stage / output.name
        (pack / "bin").mkdir(parents=True)
        hashes = {}
        for path in files.values():
            target = pack / "bin" / path.name
            shutil.copy2(path, target)
            with target.open("rb") as stream:
                hashes[target.name] = hashlib.file_digest(stream, "sha256").hexdigest()
        for index, group in enumerate(notices):
            folder = pack / "licenses" / str(index)
            folder.mkdir(parents=True)
            for path in group:
                shutil.copy2(path, folder / path.name)
        (pack / "pack.json").write_text(json.dumps({
            "platform": "windows-x64", "provider": backend, "files_sha256": hashes,
        }, indent=2) + "\n", encoding="utf-8")
        pack.rename(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", choices=("cuda", "directml"), required=True)
    parser.add_argument("--ort-dir", type=Path, required=True)
    parser.add_argument("--dependency-dir", type=Path, action="append", default=[])
    parser.add_argument("--notice-dir", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True,
                        help="New directory, normally named cuda or directml")
    args = parser.parse_args()
    try:
        print(build_pack(args.provider, args.ort_dir, args.dependency_dir, args.notice_dir, args.output))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
