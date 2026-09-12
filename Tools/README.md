# Rendepth Tools

Internal utilities for model export, conversion, and super-resolution optimization.

## Scripts

- **`build_windows_runtime_pack.py`**: Builds Windows x64 CUDA/DirectML packs from local SDKs with DLL architecture checks, notices, and a hash inventory. See [Windows packaging](../Packaging/Windows/README.md).
- **`test_build_windows_runtime_pack.py`**: Tests pack validation and refusal to overwrite an existing pack.
- **`test_windows_inference.py`**: Tests isolated CPU/GPU loading, DLL origin, fallback, and real depth/SR inference on Windows.

- **`export_realesrnet_onnx.py`**: Exports PyTorch Real-ESRNet checkpoints to dynamic NCHW ONNX models for real-time inference in Rendepth.
- **`make_esrgan_scale_models.py`**: Creates x2 and x3 ONNX model variants with internal resizing from x4 Real-ESRGAN checkpoints.
- **`export_lightweight_sr.py`**: Exports lightweight super-resolution architectures (such as RFDN and ECBSR) to dynamic ONNX models with normalized float inputs `[0.0, 1.0]`.
- **`build_linux_cuda_pack.py`**: Assembles a separate Linux CUDA runtime pack from ORT, CUDA, and cuDNN SDKs. See [Linux packaging](../Packaging/Linux/README.md#build-a-linux-cuda-pack) for inputs and validation.
- **`test_build_linux_cuda_pack.py`**: Checks pack assembly with small ELF fixtures (`gcc`, `readelf`, and `patchelf`).
- **`test_linux_inference.py`**: Checks runtime loading and CPU fallback; `--cuda-pack` additionally requires CUDA depth and super-resolution inference on NVIDIA hardware.
