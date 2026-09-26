#!/usr/bin/env python3
"""Assemble a relocatable ROCm pack from explicit SDK inputs, on any Linux host.

Use matching ORT and ROCm binaries built for the oldest intended target OS.
The target runtime libraries must come from that OS, not from the build host.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from build_linux_cuda_pack import elf_info, sha256


# These come from the destination OS or its GPU driver. All other NEEDED
# libraries, including the ROCm and ORT dependency graph, are packaged.
SYSTEM_LIBRARIES = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
    "libstdc++.so.6", "libgcc_s.so.1", "libresolv.so.2", "libutil.so.1",
    "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1",
    "libdrm.so.2", "libdrm_amdgpu.so.1", "libudev.so.1", "libnuma.so.1",
}
PLATFORM_VERSION = re.compile(r"(?:GLIBC|GLIBCXX|CXXABI|GCC)_[0-9][0-9.]*")
TARGET_SONAMES = {"libc.so.6", "libstdc++.so.6", "libgcc_s.so.1"}


def clear_executable_stack(path):
    """Clear PF_X on PT_GNU_STACK; no ROCm runtime object needs stack code."""
    with path.open("r+b") as stream:
        header = stream.read(64)
        if header[:4] != b"\x7fELF" or header[5] not in (1, 2):
            raise ValueError(f"Invalid ELF header: {path}")
        order = "<" if header[5] == 1 else ">"
        if header[4] == 2:
            phoff = struct.unpack_from(order + "Q", header, 32)[0]
            entry_size, count = struct.unpack_from(order + "HH", header, 54)
            flags_offset = 4
        elif header[4] == 1:
            phoff = struct.unpack_from(order + "I", header, 28)[0]
            entry_size, count = struct.unpack_from(order + "HH", header, 42)
            flags_offset = 24
        else:
            raise ValueError(f"Unsupported ELF class: {path}")
        if entry_size < flags_offset + 4 or phoff + entry_size * count > path.stat().st_size:
            raise ValueError(f"Invalid ELF program headers: {path}")
        for index in range(count):
            position = phoff + index * entry_size
            stream.seek(position)
            segment = stream.read(4)
            if len(segment) != 4:
                raise ValueError(f"Truncated ELF program header: {path}")
            if struct.unpack(order + "I", segment)[0] != 0x6474e551:
                continue
            stream.seek(position + flags_offset)
            flags = struct.unpack(order + "I", stream.read(4))[0]
            if flags & 1:
                stream.seek(position + flags_offset)
                stream.write(struct.pack(order + "I", flags & ~1))


def version_names(path, section):
    result = subprocess.run(["readelf", "--version-info", str(path)], check=True,
                            capture_output=True, text=True, env={**os.environ, "LC_ALL": "C"})
    text = result.stdout
    marker = "Version needs section" if section == "needs" else "Version definition section"
    if marker not in text:
        return set()
    text = text.split(marker, 1)[1]
    if section == "defines" and "Version needs section" in text:
        text = text.split("Version needs section", 1)[0]
    return {name for name in re.findall(r"Name: ([A-Za-z0-9_.]+)", text)
            if PLATFORM_VERSION.fullmatch(name)}


def check_target_abi(sources, target_libraries):
    provided = set()
    for path in target_libraries:
        provided.update(version_names(path, "defines"))
    required = set().union(*(version_names(path, "needs") for path in sources))
    missing = required - provided
    if missing:
        raise ValueError("Target OS lacks required ABI symbols: " + ", ".join(sorted(missing)))
    return sorted(required)


def validate_target_libraries(target_libraries, architecture):
    sonames = set()
    for path in target_libraries:
        arch, soname, _ = elf_info(path)
        if arch != architecture:
            raise ValueError(f"Target OS library has a different ELF architecture: {path}")
        sonames.add(soname)
    if sonames != TARGET_SONAMES:
        raise ValueError("Supply exactly libc.so.6, libstdc++.so.6, and "
                         "libgcc_s.so.1 from the oldest target OS")


def build_pack(ort_directory, library_directories, data_directories, ort_notices,
               notices, target_libraries, output):
    output = output.expanduser().absolute()
    if output.exists() or output.is_symlink():
        raise ValueError(f"Refusing to replace an existing pack: {output}")
    for tool in ("readelf", "patchelf"):
        if not shutil.which(tool):
            raise ValueError(f"Required tool is missing: {tool}")
    ort = ort_directory.expanduser().resolve(strict=True)
    runtime = [directory.expanduser().resolve(strict=True) for directory in library_directories]
    for directory in (ort, *runtime):
        if not directory.is_dir():
            raise ValueError(f"Library input is not a directory: {directory}")
    for label, directory in data_directories.items():
        if not directory.expanduser().is_dir() or not any(
                path.is_file() for path in directory.expanduser().rglob("*")):
            raise ValueError(f"Missing or empty {label} data directory: {directory}")
    for label in ("rocblas", "hipblaslt"):
        if not (data_directories[label].expanduser() / "library").is_dir():
            raise ValueError(f"{label} data must contain a library/ directory")
    ort_notices = ort_notices.expanduser().resolve(strict=True)
    for name in ("LICENSE", "ThirdPartyNotices.txt"):
        if not (ort_notices / name).is_file():
            raise ValueError(f"Missing ORT notice: {ort_notices / name}")
    for directory in notices:
        if not directory.expanduser().is_dir() or not any(
                path.is_file() for path in directory.expanduser().rglob("*")):
            raise ValueError(f"Missing or empty redistribution notices: {directory}")
    target_libraries = [path.expanduser().resolve(strict=True) for path in target_libraries]

    libraries = {}
    dependencies = {}
    architecture = None

    def locate(name):
        candidates = {candidate.resolve() for directory in (ort, *runtime)
                      if (candidate := directory / name).is_file()}
        if len(candidates) != 1:
            raise ValueError(f"Expected one input for {name}; found {sorted(map(str, candidates))}")
        return candidates.pop()

    def add(name, source):
        nonlocal architecture
        if Path(name).name != name or name in (".", ".."):
            raise ValueError(f"Invalid library name: {name}")
        source = source.resolve(strict=True)
        if name in libraries:
            if libraries[name] != source:
                raise ValueError(f"Conflicting library: {name}")
            return
        libraries[name] = source
        if source in dependencies:
            return
        arch, soname, needed = elf_info(source)
        if architecture is None:
            architecture = arch
        if arch != architecture:
            raise ValueError(f"Mixed ELF architectures: {source}")
        dependencies[source] = needed
        if soname:
            add(soname, source)
        for dependency in needed:
            if dependency not in SYSTEM_LIBRARIES:
                add(dependency, locate(dependency))

    cores = {path.resolve() for path in ort.glob("libonnxruntime.so*") if path.is_file()}
    if len(cores) != 1:
        raise ValueError(f"Expected exactly one ORT core in {ort}; found {len(cores)}")
    add("libonnxruntime.so.1", cores.pop())
    for name in ("libonnxruntime_providers_shared.so", "libonnxruntime_providers_rocm.so",
                 "libhiprtc.so", "libhiprtc-builtins.so", "libhipblaslt.so"):
        add(name, locate(name))
    if not any(name.startswith("libMIOpen.so") for name in libraries):
        raise ValueError("Missing MIOpen runtime dependency")
    validate_target_libraries(target_libraries, architecture)
    platform_versions = check_target_abi(dependencies, target_libraries)

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".rocm-pack-", dir=output.parent) as temporary:
        pack = Path(temporary) / "rocm"
        lib = pack / "lib"
        lib.mkdir(parents=True)
        copied = {}
        for name, source in sorted(libraries.items()):
            if source in copied:
                (lib / name).symlink_to(copied[source])
                continue
            destination = lib / name
            shutil.copyfile(source, destination)
            subprocess.run(["patchelf", "--force-rpath", "--set-rpath", "$ORIGIN",
                            str(destination)], check=True)
            clear_executable_stack(destination)
            copied[source] = name
        for label, source in data_directories.items():
            destination = {"rocblas": pack / "lib/rocblas",
                           "hipblaslt": pack / "lib/hipblaslt",
                           "miopen": pack / "share/miopen/db"}[label]
            shutil.copytree(source.expanduser(), destination)
        licenses = pack / "licenses"
        (licenses / "onnxruntime").mkdir(parents=True)
        for name in ("LICENSE", "ThirdPartyNotices.txt"):
            shutil.copyfile(ort_notices / name, licenses / "onnxruntime" / name)
        for index, directory in enumerate(notices, 1):
            shutil.copytree(directory.expanduser(), licenses / f"rocm-{index}")
        manifest = {
            "format": 1, "backend": "rocm", "architecture": list(architecture),
            "platform_symbol_versions": platform_versions,
            "system_dependencies": sorted({name for needed in dependencies.values()
                                           for name in needed if name in SYSTEM_LIBRARIES}),
            "environment": {"MIOPEN_SYSTEM_DB_PATH": "<pack>/share/miopen/db"},
            "files": {str(path.relative_to(pack)): sha256(path)
                      for path in sorted(pack.rglob("*")) if path.is_file()},
        }
        (pack / "pack.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if output.exists() or output.is_symlink():
            raise ValueError(f"Refusing to replace an existing pack: {output}")
        os.rename(pack, output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ort-directory", type=Path, required=True)
    parser.add_argument("--library-directory", type=Path, action="append", required=True)
    parser.add_argument("--rocblas-data", type=Path, required=True,
                        help="rocBLAS data directory containing library/")
    parser.add_argument("--hipblaslt-data", type=Path, required=True,
                        help="hipBLASLt data directory containing library/")
    parser.add_argument("--miopen-db", type=Path, required=True)
    parser.add_argument("--ort-notices-directory", type=Path, required=True)
    parser.add_argument("--notices-directory", type=Path, action="append", required=True)
    parser.add_argument("--target-library", type=Path, action="append", required=True,
                        help="libc, libstdc++, and libgcc from the oldest target OS; repeat")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        output = build_pack(args.ort_directory, args.library_directory,
                            {"rocblas": args.rocblas_data, "hipblaslt": args.hipblaslt_data,
                             "miopen": args.miopen_db}, args.ort_notices_directory,
                            args.notices_directory, args.target_library, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Portable ROCm pack failed: {error}\n")
    print(f"Built ROCm pack: {output}\nValidate inference on each supported OS and GPU.")


if __name__ == "__main__":
    main()
