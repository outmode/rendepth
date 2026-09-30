#!/usr/bin/env python3
"""Export the official RealESRNet_x4plus weights as dynamic NCHW ONNX."""

import argparse
from pathlib import Path

import torch
from basicsr.archs.rrdbnet_arch import RRDBNet


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("weights", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    model = RRDBNet(num_in_ch=3, num_out_ch=3, scale=4,
                    num_feat=64, num_block=23, num_grow_ch=32)
    checkpoint = torch.load(args.weights, map_location="cpu")
    model.load_state_dict(checkpoint["params_ema"], strict=True)
    model.eval()
    sample = torch.zeros(1, 3, 64, 64)
    torch.onnx.export(model, sample, args.output, opset_version=17,
                      input_names=["input"], output_names=["output"],
                      dynamic_axes={"input": {2: "height", 3: "width"},
                                    "output": {2: "output_height", 3: "output_width"}},
                      dynamo=False)


if __name__ == "__main__":
    main()
