#!/usr/bin/env python3
"""Assemble an offline Linux CUDA pack from matching ORT and NVIDIA SDKs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


# These remain OS/driver responsibilities. Everything else must be in the pack.
SYSTEM_LIBRARIES = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
    "libstdc++.so.6", "libgcc_s.so.1", "libresolv.so.2", "libutil.so.1",
    "libz.so.1",  # cuDNN 9.25+ uses the base-system zlib runtime.
    "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1",
    "libcuda.so.1", "libnvidia-ptxjitcompiler.so.1",
}
NVIDIA_LIBRARY = re.compile(
    r"lib(?:cudart|cublas(?:Lt)?|cudnn[^.]*|cufft|curand|cusolver|cusparse|"
    r"nvrtc(?:-builtins)?|nvJitLink|nvToolsExt)\.so(?:\..*)?$")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def elf_info(path):
    # Inspect ELF metadata without loading vendor code or using ldd.
    result = subprocess.run(["readelf", "-h", "-d", str(path)], check=True,
                            capture_output=True, text=True, env={**os.environ, "LC_ALL": "C"})
    text = result.stdout
    machine = re.search(r"^\s*Machine:\s*(.+)$", text, re.MULTILINE)
    elf_class = re.search(r"^\s*Class:\s*(.+)$", text, re.MULTILINE)
    soname = re.search(r"\(SONAME\).*\[(.+?)\]", text)
    if not machine or not elf_class:
        raise ValueError(f"Not an ELF library: {path}")
    return ((elf_class[1], machine[1]), soname[1] if soname else None,
            re.findall(r"\(NEEDED\).*\[(.+?)\]", text))


def build_pack(ort_directory, library_directories, notices, output):
    output = output.expanduser().absolute()
    if output.exists() or output.is_symlink():
        raise ValueError(f"Refusing to replace an existing pack: {output}")
    for tool in ("readelf", "patchelf"):
        if not shutil.which(tool):
            raise ValueError(f"Required tool is missing: {tool}")
    ort = ort_directory.expanduser().resolve(strict=True)
    cores = {p.resolve() for p in ort.glob("libonnxruntime.so*") if p.is_file()}
    if len(cores) != 1:
        raise ValueError(f"Expected exactly one ORT core in {ort}; found {len(cores)}")
    libraries = {"libonnxruntime.so.1": cores.pop()}
    for name in ("libonnxruntime_providers_shared.so", "libonnxruntime_providers_cuda.so"):
        libraries[name] = (ort / name).resolve(strict=True)
    for directory in library_directories:
        directory = directory.expanduser().resolve(strict=True)
        for path in sorted(directory.iterdir()):
            if NVIDIA_LIBRARY.fullmatch(path.name) and path.is_file():
                if path.name in libraries and libraries[path.name] != path.resolve():
                    raise ValueError(f"Conflicting library: {path.name}")
                libraries[path.name] = path.resolve()
    if not any(name.startswith("libcudnn.so.") for name in libraries):
        raise ValueError("Supply cuDNN runtime libraries with --library-directory")
    for notice in notices:
        if not notice.expanduser().is_dir() or not any(p.is_file() for p in notice.expanduser().rglob("*")):
            raise ValueError(f"Notices directory is missing or empty: {notice}")
    for name in ("LICENSE", "ThirdPartyNotices.txt"):
        if not (ort.parent / name).is_file():
            raise ValueError(f"Required ORT notice is missing: {ort.parent / name}")

    architecture = elf_info(libraries["libonnxruntime.so.1"])[0]
    dependencies = {}
    for source in set(libraries.values()):
        arch, soname, needed = elf_info(source)
        if arch != architecture:
            raise ValueError(f"ELF architecture differs from ORT: {source}")
        if soname:
            if Path(soname).name != soname or soname in (".", ".."):
                raise ValueError(f"Invalid SONAME: {soname}")
            if soname in libraries and libraries[soname] != source:
                raise ValueError(f"Conflicting SONAME: {soname}")
            libraries[soname] = source
        dependencies[source] = needed
    for source, needed in dependencies.items():
        missing = set(needed) - libraries.keys() - SYSTEM_LIBRARIES
        if missing:
            raise ValueError(f"Missing dependencies for {source.name}: {', '.join(sorted(missing))}")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".cuda-pack-", dir=output.parent) as temporary:
        pack = Path(temporary) / "cuda"
        lib = pack / "lib"
        lib.mkdir(parents=True)
        copied = {}
        for name, source in sorted(libraries.items()):
            if source in copied:
                (lib / name).symlink_to(copied[source])
                continue
            destination = lib / name
            shutil.copyfile(source, destination)
            # RPATH (rather than RUNPATH) also covers transitive vendor dlopen
            # dependencies, including cuDNN engines and NVRTC builtins.
            subprocess.run(["patchelf", "--force-rpath", "--set-rpath", "$ORIGIN",
                            str(destination)], check=True)
            copied[source] = name
        licenses = pack / "licenses"
        (licenses / "onnxruntime").mkdir(parents=True)
        for name in ("LICENSE", "ThirdPartyNotices.txt"):
            shutil.copyfile(ort.parent / name, licenses / "onnxruntime" / name)
        for index, notice in enumerate(notices):
            shutil.copytree(notice.expanduser(), licenses / f"nvidia-{index + 1}")
        manifest = {
            "format": 1, "backend": "cuda", "architecture": list(architecture),
            "system_dependencies": sorted({name for needed in dependencies.values()
                                           for name in needed if name in SYSTEM_LIBRARIES}),
            "files": {str(p.relative_to(pack)): sha256(p)
                      for p in sorted(pack.rglob("*")) if p.is_file()},
        }
        (pack / "pack.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if output.exists() or output.is_symlink():
            raise ValueError(f"Refusing to replace an existing pack: {output}")
        os.rename(pack, output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ort-directory", type=Path, required=True,
                        help="lib directory of a CUDA-enabled ORT SDK")
    parser.add_argument("--library-directory", type=Path, action="append", required=True,
                        help="CUDA/cuDNN runtime lib directory; repeat as needed (never driver stubs)")
    parser.add_argument("--notices-directory", type=Path, action="append", required=True,
                        help="directory containing NVIDIA redistribution notices; repeat as needed")
    parser.add_argument("--output", type=Path, required=True,
                        help="new pack directory, e.g. Distribution/cuda")
    args = parser.parse_args()
    try:
        output = build_pack(args.ort_directory, args.library_directory, args.notices_directory, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"CUDA pack failed: {error}\n")
    print(f"Built CUDA pack: {output}\nValidate on NVIDIA hardware before distribution.")


if __name__ == "__main__":
    main()
