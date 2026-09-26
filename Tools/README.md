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
- **`build_linux_cuda_pack.py`**: Assembles a separate Linux CUDA runtime pack from ORT, CUDA, and cuDNN SDKs. See [Linux packaging](../Packaging/Linux/README.md#build-a-linux-cuda-pack) for inputs and validation.
- **`test_build_linux_cuda_pack.py`**: Checks pack assembly with small ELF fixtures (`gcc`, `readelf`, and `patchelf`).
- **`build_linux_rocm_pack_portable.py`**: Assembles a ROCm pack from explicit ORT/ROCm SDK, data, and notice directories, with an older target OS ABI check. See [Linux packaging](../Packaging/Linux/README.md#build-packs-for-an-older-linux-baseline-on-fedora).
- **`check_linux_gpu_pack_abi.py`**: Checks a CUDA or ROCm pack's platform symbols against target OS libc, libstdc++, and libgcc libraries.
- **`test_build_linux_rocm_pack_portable.py`**: Checks ROCm dependency and ABI rejection with small ELF fixtures; full assembly tests require `patchelf`.
- **`test_linux_inference.py`**: Checks runtime loading and CPU fallback; `--cuda-pack` additionally requires CUDA depth and super-resolution inference on NVIDIA hardware.

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
