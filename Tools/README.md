# Rendepth Tools

Internal utilities for model export, conversion, and super-resolution optimization.

Keep standalone development utilities, ad hoc tests, and reusable test fixtures under
`Tools/`, rather than in the project root. Remove one-off repair scripts once their
changes are incorporated; use a system temporary directory for disposable experiments.

## Scripts

- **`update_legal_notices.py`**: Regenerates the application license and consolidated dependency notices in `Legal/`; use `--check` to detect stale output. See [licensing maintenance](Legal/README.md).
- **`test_update_legal_notices.py`**: Checks notice preservation and packaging with the selected ONNX Runtime SDK.

- **`build_windows_runtime_pack.py`**: Builds Windows x64 CUDA/DirectML packs from local SDKs with DLL architecture checks, notices, and a hash inventory. See [Windows packaging](../Packaging/Windows/README.md).
- **`test_build_windows_runtime_pack.py`**: Tests pack validation and refusal to overwrite an existing pack.
- **`test_windows_inference.py`**: Tests isolated CPU/GPU loading, DLL origin, fallback, and real depth/SR inference on Windows.

- **`export_realesrnet_onnx.py`**: Exports PyTorch Real-ESRNet checkpoints to dynamic NCHW ONNX models for real-time inference in Rendepth.
- **`make_esrgan_scale_models.py`**: Creates x2 and x3 ONNX model variants with internal resizing from x4 Real-ESRGAN checkpoints.
- **`export_lightweight_sr.py`**: Exports lightweight super-resolution architectures (such as RFDN and ECBSR) to dynamic ONNX models with normalized float inputs `[0.0, 1.0]`.
- **`convert_depth_coreml.py`**: Converts a fixed-size Depth Anything V2 PyTorch checkpoint to a native Core ML `.mlpackage`. Requires `torch`, `torchvision`, `coremltools`, and the Depth Anything V2 source tree. Compile the package to `.mlmodelc` for local use in the macOS app.
- **`convert_sr_coreml.py`**: Converts the official RFDN_AIM x4 checkpoint to a flexible-shape Core ML `.mlpackage` for normalized RGB inputs from 16 to 720 pixels per side. Requires `torch`, `coremltools`, and the [RFDN source](https://github.com/njulj/RFDN).
- **`package_coreml_models.py`**: Creates one `-coreml.zip` per compiled `.mlmodelc` model, plus a matching SHA-256 sidecar, for server upload. The archives contain the exact directories the macOS app loads.
- **`benchmark_depth_coreml.py`**: Measures native `.mlmodelc` load and prediction times, with an optional comparison to the corresponding ONNX model. Requires `coremltools`, and `onnxruntime` for the comparison.
- **`build_linux_cuda_pack.py`**: Assembles a separate Linux CUDA runtime pack from ORT, CUDA, and cuDNN SDKs. See [Linux packaging](../Packaging/Linux/README.md#build-a-linux-cuda-pack) for inputs and validation.
- **`test_build_linux_cuda_pack.py`**: Checks pack assembly with small ELF fixtures (`gcc`, `readelf`, and `patchelf`).
- **`build_linux_rocm_pack_portable.py`**: Assembles a ROCm pack from explicit ORT/ROCm SDK, data, and notice directories, with an older target OS ABI check. See [Linux packaging](../Packaging/Linux/README.md#build-packs-for-an-older-linux-baseline-on-fedora).
- **`check_linux_gpu_pack_abi.py`**: Checks a CUDA or ROCm pack's platform symbols against target OS libc, libstdc++, and libgcc libraries.
- **`test_build_linux_rocm_pack_portable.py`**: Checks ROCm dependency and ABI rejection with small ELF fixtures; full assembly tests require `patchelf`.
- **`test_linux_inference.py`**: Checks runtime loading and CPU fallback; `--cuda-pack` additionally requires CUDA depth and super-resolution inference on NVIDIA hardware.

For a local native Core ML trial, convert the checkpoint at the same input size
as the ONNX export, compile the package, and compare both models:

```sh
python3 Tools/convert_depth_coreml.py \
  --source-root /path/to/Depth-Anything-V2 \
  --checkpoint /path/to/depth_anything_v2_vitl.pth \
  --encoder vitl --size 714 --output /tmp/DA2-LARGE-714.mlpackage
xcrun coremlcompiler compile /tmp/DA2-LARGE-714.mlpackage /tmp
python3 Tools/benchmark_depth_coreml.py /tmp/DA2-LARGE-714.mlmodelc \
  --size 714 --onnx /path/to/DA2-LARGE-714.onnx
```

To test in the macOS app, copy the compiled `DA2-LARGE-714.mlmodelc` directory
beside `DA2-LARGE-714.onnx` in the configured Models folder, then choose
**GPU Support > Choose AI Engine > Core ML** and restart. The same convention
applies to `DA2-SMALL-560`, `DA2-BASE-644`, and the video sizes
`DA2-SMALL-280`, `DA2-SMALL-336`, and `DA2-SMALL-392`. To convert super
resolution, use the official RFDN source and checkpoint:

```sh
python3 Tools/convert_sr_coreml.py \
  --source-root /path/to/RFDN \
  --checkpoint /path/to/RFDN/trained_model/RFDN_AIM.pth \
  --output /tmp/RFDN_x4.mlpackage
xcrun coremlcompiler compile /tmp/RFDN_x4.mlpackage /tmp
```

Copy `RFDN_x4.mlmodelc` beside `RFDN_x4.onnx` for local use. Keep the
`.mlpackage` files for future distribution; this trial does not add native
models to downloads.

After compiling all seven models, stage upload files with:

```sh
python3 Tools/package_coreml_models.py \
  --models-dir "$HOME/.Rendepth/Models" \
  --output-dir Distribution/CoreMLModels
cd Distribution/CoreMLModels && shasum -a 256 -c *.zip.sha256
```

Upload each `-coreml.zip` with its `.zip.sha256` sidecar. The names are distinct
from the existing ONNX ZIPs. The macOS app downloads and verifies these
archives when Core ML is selected, then reuses the installed `.mlmodelc`
directories on subsequent runs.

## Release path audit

Before publishing, scan the staged install or unpacked package for embedded developer
paths, external symlinks, and accidentally bundled settings or bytecode:

```sh
cmake --install <build-directory> --prefix <staging-directory> --strip
python3 Tools/audit_release_paths.py <staging-directory>
```

Use `--forbid-root <private-sdk-directory>` for additional machine-specific SDK roots.
The scanner checks UTF-8 and UTF-16 strings in binaries as well as text files. Standard
runtime paths such as `/usr/lib` are allowed. Debug/build directories are local inputs,
not release artifacts; do not distribute them by copying the whole working tree.
