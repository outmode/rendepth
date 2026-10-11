#!/usr/bin/env python3
"""Convert the official RFDN_AIM x4 checkpoint to flexible-shape Core ML.

Pass the directory containing RFDN.py and block.py as --source-root.
Rendepth feeds normalized RGB tensors and expects normalized RGB output.
"""

import argparse
from pathlib import Path
import sys

import coremltools as ct
import numpy as np
import torch
import torch.nn.functional as F

from export_lightweight_sr import NormalizedRgbWrapper


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not (args.source_root / "RFDN.py").is_file():
        parser.error("RFDN.py was not found in --source-root")
    if not args.checkpoint.is_file():
        parser.error("checkpoint was not found")
    if args.output.suffix != ".mlpackage" or args.output.exists():
        parser.error("--output must be a new .mlpackage path")

    sys.path.insert(0, str(args.source_root))
    import block
    from RFDN import RFDN

    def esa_forward(self, x):
        c1_ = self.conv1(x)
        c1 = self.conv2(c1_)
        v_max = F.max_pool2d(c1, kernel_size=7, stride=3)
        v_range = self.relu(self.conv_max(v_max))
        c3 = self.relu(self.conv3(v_range))
        c3 = self.conv3_(c3)
        # grid_sample expresses the original align_corners=False bilinear
        # resize in a form that Core ML can use with flexible spatial shapes.
        y_indices = torch.arange(x.shape[2], device=x.device, dtype=x.dtype)
        x_indices = torch.arange(x.shape[3], device=x.device, dtype=x.dtype)
        y = (y_indices + .5) * (2.0 / torch.ones_like(y_indices).sum()) - 1.0
        xx = (x_indices + .5) * (2.0 / torch.ones_like(x_indices).sum()) - 1.0
        yy, xx = torch.meshgrid(y, xx, indexing="ij")
        grid = torch.stack((xx, yy), dim=-1).unsqueeze(0)
        c3 = F.grid_sample(c3, grid, mode="bilinear", padding_mode="border",
                           align_corners=False)
        cf = self.conv_f(c1_)
        m = self.sigmoid(self.conv4(c3 + cf))
        return x * m

    block.ESA.forward = esa_forward

    model = RFDN(upscale=4)
    model.load_state_dict(torch.load(args.checkpoint, map_location="cpu",
                                     weights_only=True), strict=True)
    model = NormalizedRgbWrapper(model.eval()).eval()
    example = torch.zeros(1, 3, 64, 64)
    with torch.inference_mode():
        traced = torch.jit.trace(model, example, check_trace=False)
    height_range = ct.RangeDim(lower_bound=16, upper_bound=720, default=64)
    width_range = ct.RangeDim(lower_bound=16, upper_bound=720, default=64)
    converted = ct.convert(
        traced,
        convert_to="mlprogram",
        minimum_deployment_target=ct.target.macOS15,
        compute_precision=ct.precision.FLOAT16,
        inputs=[ct.TensorType(name="input", shape=(1, 3, height_range, width_range),
                              dtype=np.float32)],
        skip_model_load=True,
    )
    converted.save(str(args.output))
    print(f"Saved {args.output}", flush=True)


if __name__ == "__main__":
    main()
