#!/usr/bin/env python3
"""Check a CUDA or ROCm pack against an older Linux target's core ABI."""
import argparse
from pathlib import Path
import subprocess

from build_linux_cuda_pack import elf_info
from build_linux_rocm_pack_portable import check_target_abi, validate_target_libraries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack", type=Path, help="assembled cuda/ or rocm/ directory")
    parser.add_argument("--target-library", type=Path, action="append", required=True,
                        help="libc, libstdc++, and libgcc from the oldest target OS; repeat")
    args = parser.parse_args()
    try:
        library_directory = (args.pack / "lib").resolve(strict=True)
        libraries = set()
        for path in library_directory.iterdir():
            if not path.is_file() or ".so" not in path.name:
                continue
            source = path.resolve()
            if not source.is_relative_to(library_directory):
                raise ValueError(f"Library alias escapes the pack: {path}")
            libraries.add(source)
        if not libraries:
            raise ValueError(f"No shared libraries in {library_directory}")
        architecture = {elf_info(path)[0] for path in libraries}
        if len(architecture) != 1:
            raise ValueError("Mixed ELF architectures in pack")
        target = [path.expanduser().resolve(strict=True) for path in args.target_library]
        validate_target_libraries(target, architecture.pop())
        required = check_target_abi(libraries, target)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Pack ABI check failed: {error}\n")
    print(f"PASS: {len(libraries)} libraries require {len(required)} platform ABI symbols "
          "provided by the target OS")


if __name__ == "__main__":
    main()
