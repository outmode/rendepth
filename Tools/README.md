# Rendepth Tools

Internal utilities for model export, conversion, and super-resolution optimization.

## Scripts

- **`export_realesrnet_onnx.py`**: Exports PyTorch Real-ESRNet checkpoints to dynamic NCHW ONNX models for real-time inference in Rendepth.
- **`make_esrgan_scale_models.py`**: Creates x2 and x3 ONNX model variants with internal resizing from x4 Real-ESRGAN checkpoints.
- **`export_lightweight_sr.py`**: Exports lightweight super-resolution architectures (such as RFDN and ECBSR) to dynamic ONNX models with normalized float inputs `[0.0, 1.0]`.
