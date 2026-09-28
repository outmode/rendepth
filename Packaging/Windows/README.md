# Windows inference packs

## Build an unsigned installer

Install Inno Setup 6 and PyInstaller on the build machine. Install the MSVC x64
GStreamer runtime and development files into `Runtimes/GStreamer` (or set
`RENDEPTH_GSTREAMER_ROOT` in the Release CMake profile). Configure an x64 Visual
Studio Release build as described below, then run from the repository root:

```powershell
& Packaging/Windows/Build-Installer.ps1 -ChromeExtensionId hffdjljngfgobaekdbgfgfodecmehgbh
```

To sign a release with a hardware code-signing token, use the certificate
thumbprint from the Windows Current User personal certificate store:

```powershell
& Packaging/Windows/Build-Installer.ps1 `
  -ChromeExtensionId hffdjljngfgobaekdbgfgfodecmehgbh `
  -Sign -SigningThumbprint 76D9F13FC52953F7365B8A29D61BA25E597C354A
```

The signed build uses Windows SDK SignTool and Sectigo's RFC 3161 timestamp
service. SafeNet prompts for the token PIN locally. The script signs and verifies
the staged app, MVC library, and browser native host before Inno Setup packages
them; Inno signs the setup and uninstaller. A signing or verification failure
leaves the previous installer in place. Pass `-SignToolPath` or `-TimestampUrl`
if this machine uses different tools or a different timestamp service.

Pass `-ChromeExtensionId` with the Item ID shown after the Chrome upload ZIP
is added to the Chrome Web Store dashboard. The local unpacked ID is not a
substitute. Pass `-BuildDir` for another configured CMake directory,
`-InnoCompiler` for an ISCC.exe outside the usual installation paths, or
`-Python` for a Python installation with PyInstaller. The script reconfigures with browser WebRTC
required, builds Rendepth with 28 parallel jobs, stages the CMake install tree,
checks the required CPU runtime, Visual C++ and GStreamer files, and writes
`Distribution/<version>/Windows/Rendepth-<version>-windows-x64-setup.exe`.
The installer includes the CPU runtime, program DLLs, GStreamer WebRTC runtime
and plugins, their vendor notices, a frozen browser native host, assets, shaders,
and licenses. It
registers the native host for Firefox and Chrome, updates any per-user
development association, and creates manifests that point to the installed
executable. End users do not need GStreamer or Python
installed separately. GPU runtime packs remain separate. The output is
unsigned unless `-Sign` is supplied. Signing only the finished setup executable
does not sign the application files inside it.

The script replaces an existing installer for the same version after the new
installer compiles successfully. It does not keep backup executables.

The installer registers Rendepth as an **Open with** choice for its supported
image, video, and audio formats. It does not change the user's default apps.
An old per-user `Rendepth.exe` open command can override the machine-wide
registration. If an older portable copy still opens, update the current
Windows account's
`HKCU\Software\Classes\Applications\Rendepth.exe\shell\open\command` value to
`"<install directory>\Binary\Rendepth.exe" "%1"`.

Windows x64 builds use a CPU runtime by default. GPU inference is selected in
**AI Engine (Restart Required)**. **Installed Runtime Packs** reports files on
disk; **GPU Support → Choose AI Engine** reports the selected runtime or CPU fallback
reason. **GPU Support → Open Pack Folder** opens the per-user installation folder.

| Engine | GPUs | Pack directory |
| --- | --- | --- |
| CPU | Any supported x64 CPU | Application `Runtimes/cpu/bin` |
| Nvidia CUDA | Compatible NVIDIA GPU and driver | User `Runtimes/cuda/bin` |
| DirectML | DirectX 12 NVIDIA, AMD, Intel Arc/integrated GPUs | User `Runtimes/directml/bin` |

DirectML requires Windows 10 version 1903 or later. It defaults to the first
hardware DirectX 12 adapter in DXGI high-performance order. On multi-GPU systems,
`RENDEPTH_DIRECTML_DEVICE` can select an explicit DXGI EnumAdapters index before
startup; invalid indices trigger CPU fallback. Status identifies the actual GPU.

The CPU runtime is loaded from an absolute application path. Each GPU pack
contains its own ORT core and dependencies. Exactly one core is retained for the
process lifetime, so pack/preference changes require restart. Missing, corrupt,
incompatible, and wrong-provider packs fall back to the bundled CPU core. A GPU
session creation failure retries a CPU session using the already loaded core;
the loader never replaces a live ORT core. Individual unsupported graph nodes
may also run on CPU. Failures during an already-running prediction are reported
by the existing inference error handling.

## Build the CPU base

Provide a CPU-only **Windows x64** ONNX Runtime SDK and headers whose API version
is supported by all intended packs. A runtime newer than the headers is allowed
if it supports the requested ORT API. GPU SDKs must not be used for the CPU base.
No CUDA toolkit, cuDNN, DirectML SDK, or ORT import library is needed to compile.

```powershell
cmake -S . -B build-windows -G "Visual Studio 18 2026" -A x64 `
  -DRENDEPTH_ONNXRUNTIME_DIR=C:/SDKs/onnxruntime-win-x64-1.22.1 `
  -DRENDEPTH_CPU_RUNTIME_DIR=C:/SDKs/onnxruntime-win-x64-1.22.1
cmake --build build-windows --config Release --target Rendepth --parallel 28
```

Retain the project's other machine-specific FFmpeg and NASM options. In CLion,
add `RENDEPTH_CPU_RUNTIME_DIR` to each profile's CMake options to survive a cache
reset. Builds copy the CPU DLL to `Runtimes/cpu/bin` beside `Binary`/`Debug`.
Install rules place it in `libexec/rendepth/Runtimes/cpu/bin` with ORT notices in
`share/licenses/rendepth/THIRD_PARTY_LICENSING`, alongside `RENDEPTH_APP_LICENSE`. GPU packs are never included in the base.
Old flat-layout ORT/CUDA DLLs beside the EXE are not part of the new base; use a
fresh package staging directory instead of distributing a historical build folder.

## Create packs from local SDKs

`Tools/build_windows_runtime_pack.py` accepts an extracted native ORT SDK or
NuGet package. It validates x64 DLL headers and required files, copies selected
runtime DLLs and notices, and writes a SHA-256 inventory. The inventory records
pack contents; it is not a signature. The script refuses to overwrite a pack.
Dependency compatibility is verified by the inference tests below, not merely
by the presence of the DLLs. Packs contain executable code and should come from
a trusted source.

For DirectML, use matching packages from NuGet. For example,
`Microsoft.ML.OnnxRuntime.DirectML` 1.22.1 declares a dependency on
`Microsoft.AI.DirectML` 1.15.4. Extract both `.nupkg` archives:

```powershell
python Tools/build_windows_runtime_pack.py --provider directml `
  --ort-dir C:/SDKs/ort-directml `
  --dependency-dir C:/SDKs/directml/bin/x64-win `
  --notice-dir C:/SDKs/directml `
  --output Distribution/windows-packs/directml
```

For CUDA, use a CUDA-enabled Windows x64 ORT SDK and the matching CUDA/cuDNN
runtime directories. These versions must follow the SDK's compatibility matrix;
do not mix CUDA major versions or cuDNN major versions.

```powershell
python Tools/build_windows_runtime_pack.py --provider cuda `
  --ort-dir C:/SDKs/onnxruntime-win-x64-gpu-1.22.1 `
  --dependency-dir C:/SDKs/cuda/bin `
  --dependency-dir C:/SDKs/cudnn/bin `
  --notice-dir C:/SDKs/cuda --notice-dir C:/SDKs/cudnn `
  --output Distribution/windows-packs/cuda
```

CUDA dependency directories are filtered to runtime DLL families (cuBLAS,
cuDNN, CUDA runtime, cuFFT, cuRAND, cuSOLVER, cuSPARSE, NVRTC, nvJitLink, zlib).
Compiler/profiler tools and the TensorRT provider are not copied. Include all
applicable vendor redistribution notices and use redistributable binaries.

## Install and select

The public download location is `https://rendepth.com/packs/`, shared with the
Linux packs. Windows archive filenames identify Windows, x64, the provider,
and runtime versions to avoid collisions with Linux archives. Each archive has
an adjacent SHA-256 sidecar. Rendepth also pins the release archive hashes in
`Source/RuntimePackDownloader.cpp`; changing an archive requires updating the
filename and pinned hash in a new app build.

When a missing engine is chosen in **GPU Support → Choose AI Engine** or the
AI Engine settings row, Rendepth offers to download and install its pack. The
download runs in the background and can be cancelled from **GPU Support**.
It verifies the published sidecar and the pinned archive hash, extracts into a
temporary directory, and moves the finished pack into place. Existing pack
directories are never overwritten; move an incomplete directory aside before
retrying. Restart Rendepth after installation.

For manual installation, copy the entire `cuda` or `directml` directory,
including `bin`, `licenses`, and `pack.json`, into
`%USERPROFILE%\.Rendepth\Runtimes`. The paths must be:

```text
%USERPROFILE%/.Rendepth/Runtimes/cuda/bin/onnxruntime.dll
%USERPROFILE%/.Rendepth/Runtimes/directml/bin/onnxruntime.dll
%USERPROFILE%/.Rendepth/Runtimes/directml/bin/DirectML.dll
```

Select the engine in Settings and restart Rendepth. Pack presence does not
guarantee a compatible GPU/driver.

## Validate

```powershell
python Tools/test_build_windows_runtime_pack.py
cmake --build build-windows --config Release --target RuntimePackDownloaderSmoke
# Downloads and installs the smaller public pack in a new test folder.
.\build-windows\tests\RuntimePackDownloaderSmoke.exe directml build-windows/runtime-pack-smoke
cmake --build build-windows --config Release --target InferenceRuntimeTest
python Tools/test_windows_inference.py Binary/InferenceRuntimeTest.exe `
  C:/SDKs/onnxruntime-win-x64-1.22.1/lib/onnxruntime.dll `
  --depth-model C:/Models/DA3-SMALL-560.onnx --depth-size 560 `
  --cuda-pack Distribution/windows-packs/cuda `
  --directml-pack Distribution/windows-packs/directml
```

Each runtime case uses a fresh process. Tests cover CPU inference, missing and
broken packs, wrong providers, paths with spaces, refusal to switch a live
core, DLL origin, real depth inference, optional SR inference, and GPU-session CPU fallback. Omit
unavailable GPU pack arguments on hardware without those backends. Pass the
input size expected by the depth model. Add `--sr-model C:/Models/RFDN_x4.onnx`
to also test super-resolution inference. These tests do not modify user settings.

Provider references:
- [DirectML requirements and session constraints](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)
- [CUDA runtime compatibility](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html)

DirectML remains supported; new Windows ML development is focused on WinML.
This pack implementation uses DirectML's documented ORT compatibility factory
export to avoid linking a second core and to retain offline installation.
