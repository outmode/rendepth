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
