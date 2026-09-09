#!/usr/bin/env python3
"""Build a Linux ROCm pack from matching Fedora RPM-installed runtimes."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from build_linux_cuda_pack import elf_info, sha256

# The kernel/graphics driver and standard platform ABI remain external.
SYSTEM_LIBRARIES = {
    'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0', 'librt.so.1',
    'libstdc++.so.6', 'libgcc_s.so.1', 'libresolv.so.2', 'libutil.so.1',
    'ld-linux-x86-64.so.2', 'ld-linux-aarch64.so.1',
    'libdrm.so.2', 'libdrm_amdgpu.so.1', 'libudev.so.1', 'libnuma.so.1',
}


def rpm(*args):
    return subprocess.check_output(['rpm', *args], text=True).strip()


def build_pack(ort_directory, library_directory, output):
    output = output.expanduser().absolute()
    if output.exists() or output.is_symlink():
        raise ValueError(f'Refusing to replace an existing pack: {output}')
    for tool in ('readelf', 'patchelf', 'rpm'):
        if not shutil.which(tool):
            raise ValueError(f'Required tool is missing: {tool}')
    ort = ort_directory.resolve(strict=True)
    runtime = library_directory.resolve(strict=True)
    libraries = {}
    dependencies = {}
    architecture = None

    def add(name, source):
        nonlocal architecture
        if Path(name).name != name or name in ('.', '..'):
            raise ValueError(f'Invalid library name: {name}')
        source = source.resolve(strict=True)
        if name in libraries and libraries[name] != source:
            raise ValueError(f'Conflicting library: {name}')
        libraries[name] = source
        if source in dependencies:
            return
        arch, soname, needed = elf_info(source)
        if architecture is None:
            architecture = arch
        if arch != architecture:
            raise ValueError(f'Mixed ELF architectures: {source}')
        dependencies[source] = needed
        if soname:
            add(soname, source)
        for dependency in needed:
            if dependency in SYSTEM_LIBRARIES:
                continue
            candidates = [directory / dependency for directory in (ort, runtime)]
            match = next((p for p in candidates if p.is_file()), None)
            if match is None:
                raise ValueError(f'Missing dependency {dependency} required by {source}')
            add(dependency, match)

    for name in ('libonnxruntime.so.1', 'libonnxruntime_providers_shared.so',
                 'libonnxruntime_providers_rocm.so'):
        pattern = 'libonnxruntime.so*' if name == 'libonnxruntime.so.1' else name + '*'
        candidates = {p.resolve() for p in ort.glob(pattern) if p.is_file()}
        if len(candidates) != 1:
            raise ValueError(f'Expected one matching ORT library for {name}: {candidates}')
        add(name, candidates.pop())
    # HIPRTC builtins and hipBLASLt can be loaded dynamically rather than NEEDED.
    for name in ('libhiprtc.so', 'libhiprtc-builtins.so', 'libhipblaslt.so'):
        add(name, runtime / name)

    data = {runtime / 'rocblas': 'lib/rocblas',
            runtime / 'hipblaslt': 'lib/hipblaslt',
            Path('/usr/share/miopen/db'): 'share/miopen/db'}
    for source in data:
        if not source.is_dir():
            raise ValueError(f'Missing GPU data directory: {source}')
    packages = {rpm('-qf', '--qf', '%{NAME}', str(p)) for p in dependencies}
    for source in data:
        packages.update(rpm('-qf', '--qf', '%{NAME}', str(p))
                        for p in source.iterdir() if p.is_file())
    # Kernel data may be in a subdirectory or a separate RPM.
    for source in data:
        sample = next(p for p in source.rglob('*') if p.is_file())
        packages.add(rpm('-qf', '--qf', '%{NAME}', str(sample)))
    versions = {p: rpm('-q', '--qf', '%{VERSION}-%{RELEASE}.%{ARCH}\n', p).splitlines()
                for p in sorted(packages)}
    notices = {}
    for package in packages:
        files = rpm('-ql', package).splitlines()
        notices[package] = [Path(p) for p in files if Path(p).is_file() and
                           ('/licenses/' in p or 'thirdpartynotice' in p.lower())]
        if not notices[package]:
            source_rpm = rpm('-q', '--qf', '%{SOURCERPM}\n', package).splitlines()[0]
            source_name = source_rpm.rsplit('-', 2)[0]
            license_dir = Path('/usr/share/licenses') / source_name
            notices[package] = [p for p in license_dir.rglob('*') if p.is_file()]
        if not notices[package]:
            raise ValueError(f'No installed license notices for RPM {package}')

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.rocm-pack-', dir=output.parent) as temporary:
        pack = Path(temporary) / 'rocm'
        lib = pack / 'lib'
        lib.mkdir(parents=True)
        copied = {}
        for name, source in sorted(libraries.items()):
            if source in copied:
                (lib / name).symlink_to(copied[source])
                continue
            destination = lib / name
            print(f'Copying {source.name}', flush=True)
            shutil.copyfile(source, destination)
            subprocess.run(['patchelf', '--force-rpath', '--set-rpath', '$ORIGIN',
                            str(destination)], check=True)
            copied[source] = name
        for source, destination in data.items():
            print(f'Copying GPU data: {source}', flush=True)
            shutil.copytree(source, pack / destination)
        for package, paths in notices.items():
            for source in paths:
                destination = pack / 'licenses' / package / source.relative_to('/usr/share')
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
        # Validate the copied ELF graph and its relocation paths.
        for source, name in copied.items():
            _, _, needed = elf_info(lib / name)
            missing = set(needed) - libraries.keys() - SYSTEM_LIBRARIES
            if missing:
                raise ValueError(f'Unresolved packaged dependencies: {name}: {missing}')
            rpath = subprocess.check_output(['patchelf', '--print-rpath', str(lib / name)], text=True).strip()
            if rpath != '$ORIGIN':
                raise ValueError(f'Invalid packaged RPATH: {name}: {rpath}')
        requirements = set()
        for name in copied.values():
            text = subprocess.check_output(['readelf', '--version-info', str(lib / name)], text=True)
            requirements.update(re.findall(r'Name: ((?:GLIBC|GLIBCXX|CXXABI)_[\d.]+)', text))
        (pack / 'README.txt').write_text(
            'Rendepth Linux ROCm runtime pack (Fedora RPM build)\n\n'
            'Extract the rocm directory into ~/.Rendepth/Runtimes while Rendepth is closed.\n'
            'Select AMD ROCm in Settings and restart.\n'
            'This Fedora build requires the platform ABI versions listed in pack.json.\n'
            'It is not a validated Ubuntu-compatible build. AMD GPU hardware, the OS\n'
            'amdgpu/KFD driver, libdrm, libudev, and libnuma are required separately.\n\n'
            'The Fedora MIOpen build has a fixed system database path. To use the\n'
            'included database, launch with (adjust for another install location):\n'
            'MIOPEN_SYSTEM_DB_PATH="$HOME/.Rendepth/Runtimes/rocm/share/miopen/db" rendepth\n'
            'No LD_LIBRARY_PATH override is required. rocBLAS and hipBLASLt kernel\n'
            'data are installed next to their libraries.\n\n'
            'GPU inference and clean-system portability require hardware validation.\n')
        manifest = {
            'format': 1, 'backend': 'rocm', 'architecture': list(architecture),
            'source_packages': versions,
            'platform_symbol_versions': sorted(requirements),
            'system_dependencies': sorted({name for needed in dependencies.values()
                                           for name in needed if name in SYSTEM_LIBRARIES}),
            'environment': {'MIOPEN_SYSTEM_DB_PATH': '<pack>/share/miopen/db'},
            'files': {str(p.relative_to(pack)): sha256(p)
                      for p in sorted(pack.rglob('*')) if p.is_file()},
        }
        (pack / 'pack.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if output.exists() or output.is_symlink():
            raise ValueError(f'Refusing to replace an existing pack: {output}')
        os.rename(pack, output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ort-directory', type=Path, default=Path('/usr/lib64/rocm/lib'))
    parser.add_argument('--library-directory', type=Path, default=Path('/usr/lib64'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        output = build_pack(args.ort_directory, args.library_directory, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'ROCm pack failed: {error}\n')
    print(f'Built ROCm pack: {output}')


if __name__ == '__main__':
    main()
