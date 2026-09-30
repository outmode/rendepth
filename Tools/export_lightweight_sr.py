#!/usr/bin/env python3
"""Export lightweight SR checkpoints to dynamic NCHW ONNX."""

import argparse
import sys
from pathlib import Path

import torch


class NormalizedRgbWrapper(torch.nn.Module):
    """Adapt models trained with byte-range RGB tensors to Rendepth's API."""

    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, image):
        return self.model(image * 255.0) / 255.0


def export_model(kind: str, source: Path, destination: Path, scale: int,
                 source_root: Path, options) -> None:
    if kind == "rfdn":
        if scale != 4:
            raise ValueError(
                "The available RFDN_AIM checkpoint is x4; separate x2/x3 "
                "checkpoints are required for those scales.")
        sys.path.insert(0, str(source_root / "RFDN"))
        from RFDN import RFDN
        model = RFDN(upscale=4)
        state = torch.load(source, map_location="cpu")
    elif kind == "ecbsr":
        sys.path.insert(0, str(source_root / "ECBSR"))
        from models.ecbsr import ECBSR
        model = ECBSR(module_nums=options.ecbsr_modules,
                      channel_nums=options.ecbsr_channels,
                      with_idt=bool(options.ecbsr_with_idt),
                      act_type=options.ecbsr_activation,
                      scale=scale, colors=options.ecbsr_colors)
        state = torch.load(source, map_location="cpu")
        if isinstance(state, dict) and "params_ema" in state:
            state = state["params_ema"]
        state = {key.removeprefix("module."): value for key, value in state.items()}
    else:
        raise ValueError(kind)
    model.load_state_dict(state, strict=True)
    model.eval()
    if kind == "rfdn":
        # RFDN_AIM is trained and evaluated on byte-range tensors, unlike
        # Rendepth's runtime interface, which uses [0, 1] floats.
        model = NormalizedRgbWrapper(model)
    torch.onnx.export(model, torch.zeros(1, 3, 64, 64), destination,
                      opset_version=17, input_names=["input"], output_names=["output"],
                      dynamic_axes={"input": {2: "height", 3: "width"},
                                    "output": {2: "output_height", 3: "output_width"}},
                      dynamo=False)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("rfdn", "ecbsr"))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scale", type=int, default=2)
    parser.add_argument("--source-root", type=Path, default=Path("/tmp/rendepth-sr"),
                        help="directory containing RFDN/ or ECBSR/")
    parser.add_argument("--ecbsr-modules", type=int, default=4)
    parser.add_argument("--ecbsr-channels", type=int, default=8)
    parser.add_argument("--ecbsr-with-idt", type=int, choices=(0, 1), default=0)
    parser.add_argument("--ecbsr-activation", default="prelu")
    parser.add_argument("--ecbsr-colors", type=int, default=1,
                        help="ECBSR channels; official checkpoints use 1 (Y)")
    args = parser.parse_args()
    export_model(args.kind, args.source, args.output, args.scale, args.source_root, args)


if __name__ == "__main__":
    main()
