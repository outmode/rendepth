# Linux inference packaging

The Linux application no longer links ONNX Runtime, CUDA, or ROCm at startup.
It explicitly loads a runtime after reading Settings. CPU inference is the
default. Rendering and video decoding retain their existing dependencies.

## Build the CPU base

Supply a self-contained **CPU** ORT shared library, its distribution notices,
and an ORT header version supported by every intended runtime pack:

```sh
cmake -S . -B cmake-build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DRENDEPTH_ONNXRUNTIME_INCLUDE_DIR=/path/to/ort-headers/include \
  -DRENDEPTH_CPU_RUNTIME_LIBRARY=/path/to/cpu-sdk/lib/libonnxruntime.so.1 \
  -DRENDEPTH_CPU_RUNTIME_NOTICES_DIR=/path/to/cpu-sdk
cmake --build cmake-build-release --target Rendepth
cmake --install cmake-build-release --prefix /usr
```

The install command writes the system prefix; use `DESTDIR` for package staging.
The base installs exactly the specified ORT core as
`libexec/rendepth/Runtimes/cpu/lib/libonnxruntime.so.1`, plus its LICENSE and
ThirdPartyNotices.txt. Development builds copy the same core to
`Runtimes/cpu/lib/` beside the source tree's Binary and Library directories.
GPU libraries and developer-machine symlinks are not installed in the base.
Do not supply a distro ORT build with unbundled non-system dependencies as the
CPU input. Inspect its ELF dependencies before releasing it.

`RENDEPTH_LINUX_BACKEND` is obsolete. The Linux build accepts both GPU provider
APIs without linking their runtimes. Windows keeps its existing build behavior;
macOS uses its CPU ORT setup. Neither platform's new packaging is validated by
this Linux change.

## GPU pack contract (local installation)

Selecting AMD ROCm in Settings does not install a runtime pack or import the
previous system installation. If the log reports `GPU runtime pack is missing
or incomplete`, the required files have not been placed in the directory below.

For development on a machine that already has ROCm-enabled ORT installed:

```sh
python3 Tools/link_local_rocm_runtime.py --ort-directory /usr/lib64/rocm/lib
```

This creates a local pack using symlinks to the existing core and provider
libraries, including the unversioned shared-provider filename ORT needs. It
refuses to replace an existing pack. Restart Rendepth after running it. These
links depend on the host's ROCm packages and are **not a redistributable pack**;
the self-contained release requirements below still apply.

### Build a Linux CUDA pack

CUDA session support is compiled into the universal Linux executable for both
depth and super-resolution. No CUDA toolkit is needed to build that executable.
Prepare a CUDA-enabled **Linux ORT SDK** with an API compatible with the build's
headers, and the CUDA and cuDNN runtime versions required by that SDK. For
example, an ORT CUDA 12/cuDNN 9 distribution needs CUDA 12 and cuDNN 9 libraries;
cuDNN 8 is not interchangeable. Use the vendor's compatibility matrix for the
specific SDK version, GPU architecture, minimum driver, and Linux baseline.

Install `patchelf` and `readelf` (binutils) on the packaging machine, then run:

```sh
python3 Tools/build_linux_cuda_pack.py \
  --ort-directory /path/to/onnxruntime-linux-x64-gpu/lib \
  --library-directory /path/to/cuda/lib64 \
  --library-directory /path/to/cudnn/lib \
  --notices-directory /path/to/cuda-redistribution-notices \
  --notices-directory /path/to/cudnn-redistribution-notices \
  --output Distribution/cuda
```

Library directories are nonrecursive; repeat the option for each runtime
directory (including split NVIDIA Python-package `lib` directories if used).
The tool copies NVIDIA runtime libraries, including cuDNN engines and NVRTC
builtins loaded on demand. It adds SONAME aliases, sets `$ORIGIN` RPATHs on
copied libraries, rejects unresolved ELF dependencies and mixed architectures,
and includes ORT/NVIDIA notices plus a `pack.json` inventory with SHA-256 hashes.
It never modifies SDK files. NVIDIA driver libraries and toolkit stubs are not
bundled. An existing output directory is rejected.

The target system must provide zlib (`libz.so.1`, Ubuntu package `zlib1g`),
which newer cuDNN libraries require, along with the standard C/C++ runtimes
and NVIDIA driver. These external dependencies are listed in `pack.json`.

ELF validation does not prove API, GPU, driver, glibc, or dynamically loaded
library compatibility. The inventory is not a signed download manifest and the
loader does not consume it. Review redistributable inputs and validate the pack
on the supported Linux/NVIDIA systems before publishing it.

Test the assembled pack on NVIDIA hardware without changing installed packs or
Settings; any CPU fallback fails the CUDA test:

```sh
python3 Tools/test_linux_inference.py Binary/InferenceRuntimeTest \
  Runtimes/cpu/lib/libonnxruntime.so.1 \
  --depth-model /path/to/DA2-SMALL-280.onnx \
  --sr-model /path/to/RFDN_x4.onnx \
  --cuda-pack Distribution/cuda
```

For local installation, close Rendepth and use
`--output "$HOME/.Rendepth/Runtimes/cuda"` when building the pack. Select
**Nvidia CUDA** through Settings → GPU Support → View Status, then restart.
In-app downloads remain a separate step; no download endpoint is configured.

### Archive naming and checksum format

Use this naming convention for Linux CUDA packs:

```text
rendepth-cuda-linux-<arch>-ort<ort-version>-cuda<cuda-version>-cudnn<cudnn-version>.tar.gz
<archive-filename>.sha256
```

Use `x64` for x86-64 and `arm64` for AArch64. Version fields describe the
libraries packaged, not the maximum CUDA version reported by `nvidia-smi`.
The current artifact uses CUDA's toolkit release `12.8`; individual component
patch versions may differ. The filename is a label, not a complete dependency
lock: preserve the exact SDK/library inputs and notices when rebuilding.
The Fedora ROCm extension to this convention is documented below; other
platforms do not yet have a validated archive convention here.

The archive must contain one top-level `cuda/` directory, with `lib/`,
`licenses/`, and `pack.json` beneath it. Preserve relative library symlinks;
do not use tar's dereference option. After assembling `Distribution/cuda`,
create the archive and sidecar checksum from inside `Distribution`:

```sh
cd Distribution
archive=rendepth-cuda-linux-x64-ort1.22.0-cuda12.8-cudnn9.25.1.tar.gz
tar -czf "$archive" cuda
gzip -t "$archive"
sha256sum "$archive" > "$archive.sha256"
```

The sidecar is a single UTF-8/ASCII line: 64 lowercase hexadecimal SHA-256
characters, two spaces, the archive's basename (no directory), and a newline.
It hashes the final compressed archive bytes. Transfer both files together;
on another Linux computer, verify them from their containing directory:

```sh
sha256sum -c rendepth-cuda-linux-x64-ort1.22.0-cuda12.8-cudnn9.25.1.tar.gz.sha256
```

`pack.json` separately maps relative file paths to hashes of the unpacked file
contents (including library aliases resolved through symlinks). Its hashes
are not the archive checksum. Neither checksum format provides a signature.

Rebuilding is not currently byte-for-byte reproducible: tar records file
timestamps, ownership, and traversal order, and tool versions can change ELF
patching/compression output. The same naming convention and library versions
can therefore produce a different archive hash. To obtain exactly the recorded
hash on another computer, copy the existing archive and verify its sidecar;
for a new build, generate a new checksum rather than reusing the old one.

### Build a Linux ROCm pack (Fedora RPM inputs)

The ROCm builder assembles the matching ORT core and providers from installed
Fedora RPMs, recursively copies non-system ELF dependencies, adds SONAME aliases,
and sets `$ORIGIN` RPATHs. It includes HIPRTC builtins, hipBLASLt, rocBLAS and
hipBLASLt kernel data, MIOpen databases, RPM license notices, and a format-1
`pack.json` inventory. Source libraries and installed Rendepth packs are unchanged.
Install `patchelf`, `readelf`, and the matching ROCm/ORT RPMs first:

```sh
python3 Tools/build_linux_rocm_pack.py \
  --ort-directory /usr/lib64/rocm/lib \
  --library-directory /usr/lib64 \
  --output Distribution/rocm
```

This builder is specifically for Fedora's installed RPM layout, including its
MIOpen database and license directories. It refuses existing output, unresolved
ELF dependencies, conflicting libraries, mixed architectures, or missing notices.
It does not download SDKs or create a portable build from a newer distro ABI.

ROCm archives follow the same layout/checksum rules above, with a top-level
`rocm/` directory containing `lib/`, `share/`, `licenses/`, `README.txt`, and
`pack.json`. Include the distro baseline in this Fedora artifact's filename:

```sh
cd Distribution
archive=rendepth-rocm-linux-x64-ort1.22.2-rocm7.1.1-fedora44.tar.gz
tar -czf "$archive" rocm
gzip -t "$archive"
sha256sum "$archive" > "$archive.sha256"
sha256sum -c "$archive.sha256"
```

The checksum sidecar uses the same single-line `64-hex-digits  archive-basename`
format as CUDA. Preserve relative symlinks. The ROCm version label represents the
7.1.1 runtime; some component RPMs are versioned 7.1.0. Exact package versions,
platform symbol versions, external dependencies, and file hashes are in `pack.json`.

Close Rendepth, then extract the top-level `rocm/` directory into
`~/.Rendepth/Runtimes/` (do not overwrite a running or existing pack). This Fedora
MIOpen build uses an absolute system database path; point it at the included data:

```sh
MIOPEN_SYSTEM_DB_PATH="$HOME/.Rendepth/Runtimes/rocm/share/miopen/db" rendepth
```

Select **AMD ROCm** under GPU Support → View Status and restart with that environment.
No `LD_LIBRARY_PATH` override is needed. The OS supplies the AMD GPU driver/KFD,
libdrm, libnuma, and standard C/C++ runtimes. This artifact requires **glibc 2.43**,
**GLIBCXX_3.4.32**, and **CXXABI_1.3.15**. It is a Fedora 44 build, not a validated
Ubuntu-compatible pack. A broadly compatible Linux release needs libraries built
against the intended oldest supported distribution.

Test both GPU inference paths in isolated directories, without changing Settings:

```sh
python3 Tools/test_linux_inference.py Binary/InferenceRuntimeTest \
  Runtimes/cpu/lib/libonnxruntime.so.1 \
  --depth-model /path/to/DA2-SMALL-280.onnx \
  --sr-model /path/to/RFDN_x4.onnx \
  --rocm-pack Distribution/rocm
```

The test sets the included MIOpen database path and fails on CPU fallback.

### Fedora ROCm pack validation (2026-09-08)

Assembled `Distribution/rocm` from ORT 1.22.2 and the installed Fedora 44 ROCm
7.1.x packages. Validated ELF dependency closure and `$ORIGIN` RPATHs. The isolated
runtime suite passed CPU inference, missing/corrupt/incomplete-pack checks,
session failure recovery, and actual **ROCm depth and SR inference** on an
**AMD Radeon RX 7900 XTX (gfx1100)**. GPU testing required host access outside the
agent sandbox. A loader trace confirmed the exercised ORT/ROCm libraries loaded
from `Distribution/rocm/lib`. `InferenceRuntimeTest` has no ORT or ROCm startup
dependency.

The archive is `Distribution/rendepth-rocm-linux-x64-ort1.22.2-rocm7.1.1-fedora44.tar.gz`.
The archive is 1.86 GiB (about 6.1 GiB unpacked). Archive layout, gzip
integrity, all 3,531 inventory entries, and the sidecar checksum passed validation.
SHA-256: `e940a6c8c940e5bc3d337f99c5bbdcc2996ba021e4f42e508713595f39c8204a`.
Its adjacent `.sha256` file records the checksum of the compressed archive.
Clean-system portability and other GPU/driver combinations remain untested;
the successful host test does not prove that every MIOpen JIT/compiler path is
independent of the host's installed SDK. No pack was installed into Settings
and no download endpoint was added.

### Ubuntu CUDA pack validation (2026-09-08)

Assembled `Distribution/cuda` with the existing pack builder using:

- ORT GPU SDK 1.22.0 (`/home/psyko/SDK/onnxruntime-linux-x64-gpu-1.22.0/lib`).
- CUDA 12.8 libraries (`/usr/local/cuda-12.8/targets/x86_64-linux/lib`).
- cuDNN 9.25.1 libraries (`/usr/lib/x86_64-linux-gnu`).
- CUDA's `EULA.txt` in a separate notices directory, and
  `/usr/share/doc/libcudnn9-cuda-12` for cuDNN notices.

The archive is
`Distribution/rendepth-cuda-linux-x64-ort1.22.0-cuda12.8-cudnn9.25.1.tar.gz`;
it contains the `cuda/` directory, including licenses and the hashed inventory.
The archive is approximately 2.3 GiB (3.5 GiB unpacked); `gzip -t` passed.
SHA-256: `12e103c2d20933011eda811f86a0832b44ec0336e643493f1bc6197d5ecc5b5a`.
Driver libraries are excluded.

Built `InferenceRuntimeTest` with the ORT 1.22.0 headers and CPU SDK. All five
pack-builder tests passed. The isolated inference suite passed CPU inference,
missing/corrupt/incomplete runtime checks, session failure recovery, and actual
CUDA depth (`DA2-SMALL-280.onnx`) and SR (`RFDN_x4.onnx`) inference on this
Ubuntu NVIDIA machine with driver 580.178.04. The test executable has no ORT or
CUDA startup dependency. GPU access required running outside the agent sandbox.
This validates this host; clean-system portability and the supported driver/OS
matrix still need testing before release. No pack was installed into user
Settings, and no download endpoint was added.

The current loader expects these user-owned directories:

```text
~/.Rendepth/Runtimes/
  cuda/lib/
    libonnxruntime.so.1
    libonnxruntime_providers_shared.so
    libonnxruntime_providers_cuda.so
    ...required CUDA/cuDNN redistributable dependencies...
  rocm/lib/
    libonnxruntime.so.1
    libonnxruntime_providers_shared.so
    libonnxruntime_providers_rocm.so
    ...required ROCm redistributable dependencies...
```

Each pack must include a matching ORT **core and providers**, not just provider
libraries from an unrelated SDK. Include licenses and third-party notices in
each pack. Dependencies must resolve from the pack (using `$ORIGIN` paths on
the relevant ELF objects), or from documented base-system/driver dependencies.
Do not rely on `/usr/lib64/rocm`, a developer SDK path, or application-wide
`LD_LIBRARY_PATH` injection. Graphics drivers are installed by the user/OS.

The loader checks the ORT C API version before using any C++ wrappers. For
example, ORT 1.22 headers require API 22; newer compatible cores can provide
that API, but an older core cannot satisfy newer headers. Matching the C API
does not establish compatibility between different core/provider binaries.
Release pack versions and OS/GPU/driver support matrices still need to be pinned
and validated. No download URLs, manifests, or remote installers ship yet.

Install complete packs while the application is closed. Do not overwrite a
loaded core or provider. A future installer must verify downloads, stage them,
and activate them atomically with version/rollback metadata.

## Settings and fallback

Settings shows three compact text lines, for example:

```text
AI Engine (Restart Required): AMD ROCm
Installed Runtime Packs: ROCm
GPU Support: View Status   Open Pack Folder
```

The engine line shows the saved next-launch preference. The first two lines
are read-only information; View Status and Open Pack Folder are text actions.
View Status reports the active runtime and any fallback reason, and provides
CPU, Nvidia CUDA, and AMD ROCm controls for changing the next-launch engine.
The previous engine preference is preserved when upgrading.

When the selected GPU pack is missing or incomplete, the engine line retains
the preference and adds `(Unavailable)`, for example `Nvidia CUDA (Unavailable)`.
The same marker appears if that engine was attempted at startup but fell back
to CPU. View Status provides the failure details. A newly selected, installed
engine waiting for restart is not marked unavailable just because another
backend is currently running.

Installed means the core and provider files exist, not that the
GPU has passed inference validation. The runtime is loaded once per process;
backend changes and newly installed packs require restart.
Use the wheel/trackpad or Page Up, Page Down, Home, and End to scroll Settings
when the controls extend below the window.

Missing, incomplete, incompatible, or unloadable GPU packs select the bundled
CPU core. If a loaded GPU core cannot create an inference session, that request
is retried with its CPU execution provider and future default sessions use CPU.
Already-created sessions remain intact. Settings details and logs report the
fallback reason. A missing/broken CPU core reports inference as unavailable
without making ORT a process-startup dependency.

## Validation

```sh
cmake --build cmake-build-release --target InferenceRuntimeTest DepthTest SuperTest
python3 Tools/test_linux_inference.py Binary/InferenceRuntimeTest \
  Runtimes/cpu/lib/libonnxruntime.so.1 \
  --depth-model /path/to/DA2-SMALL-280.onnx \
  --sr-model /path/to/RFDN_x4.onnx
readelf -d Binary/Rendepth
```

The test script uses temporary application/pack directories, including spaces,
and fresh processes. It covers CPU loading, missing/incomplete/corrupt packs,
and missing/corrupt CPU cores. With models supplied, it also runs real depth
and super-resolution inference and GPU session failure recovery. It never
changes user settings or installed packs. The executable must have no ORT,
CUDA, or ROCm `NEEDED` entries. Test the staged base on a clean Linux system;
successful GPU inference still requires separate CUDA and ROCm hardware tests.
