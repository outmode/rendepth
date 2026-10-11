#!/usr/bin/env python3
"""Convert a fixed-size Depth Anything V2 checkpoint to a Core ML ML program.

Requires PyTorch, torchvision, coremltools, and the Depth-Anything-V2 source tree.
The resulting .mlpackage is a distribution asset; compile it on macOS with
`xcrun coremlcompiler compile model.mlpackage output-directory` for local tests.
"""

import argparse
from pathlib import Path
import sys
import time
import types

import coremltools as ct
import numpy as np
import torch


MODEL_CONFIGS = {
    "vits": (64, [48, 96, 192, 384]),
    "vitb": (128, [96, 192, 384, 768]),
    "vitl": (256, [256, 512, 1024, 1024]),
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Directory containing depth_anything_v2/dpt.py")
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--encoder", choices=MODEL_CONFIGS, required=True)
    parser.add_argument("--size", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.size <= 0 or args.size % 14:
        parser.error("--size must be a positive multiple of 14")
    if not args.checkpoint.is_file():
        parser.error(f"checkpoint does not exist: {args.checkpoint}")
    if not (args.source_root / "depth_anything_v2" / "dpt.py").is_file():
        parser.error(f"Depth Anything V2 source not found in {args.source_root}")
    if args.output.suffix != ".mlpackage" or args.output.exists():
        parser.error("--output must be a new .mlpackage path")

    sys.path.insert(0, str(args.source_root))
    from depth_anything_v2.dpt import DepthAnythingV2

    features, channels = MODEL_CONFIGS[args.encoder]
    started = time.monotonic()
    model = DepthAnythingV2(encoder=args.encoder, features=features,
                            out_channels=channels).eval()
    model.load_state_dict(torch.load(args.checkpoint, map_location="cpu",
                                     weights_only=True))
    print(f"Checkpoint loaded in {time.monotonic() - started:.2f}s", flush=True)

    # Each Rendepth export has one fixed input size. Compute the bicubic
    # positional embedding once in PyTorch, then embed that exact tensor.
    # coremltools 9 does not convert the dynamic bicubic operation directly.
    patches = (args.size // 14) ** 2
    tokens = torch.empty((1, patches + 1, model.pretrained.embed_dim))
    with torch.inference_mode():
        positions = model.pretrained.interpolate_pos_encoding(
            tokens, args.size, args.size).clone()
    model.pretrained.register_buffer("fixed_pos_embed", positions)

    def fixed_positions(self, _tokens, width, height):
        assert width == args.size and height == args.size
        return self.fixed_pos_embed

    model.pretrained.interpolate_pos_encoding = types.MethodType(
        fixed_positions, model.pretrained)

    example = torch.zeros((1, 3, args.size, args.size), dtype=torch.float32)
    started = time.monotonic()
    with torch.inference_mode():
        traced = torch.jit.trace(model, example, check_trace=False)
    print(f"PyTorch traced in {time.monotonic() - started:.2f}s", flush=True)

    started = time.monotonic()
    converted = ct.convert(
        traced,
        convert_to="mlprogram",
        minimum_deployment_target=ct.target.macOS15,
        compute_precision=ct.precision.FLOAT16,
        inputs=[ct.TensorType(name="input", shape=example.shape, dtype=np.float32)],
        skip_model_load=True,
    )
    converted.save(str(args.output))
    print(f"Core ML package saved in {time.monotonic() - started:.2f}s: "
          f"{args.output}", flush=True)


if __name__ == "__main__":
    main()
