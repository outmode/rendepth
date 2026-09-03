#!/usr/bin/env python3
"""Create exact-output x2/x3 ONNX variants from the x4v3 export.

The upstream realesr-general-x4v3 network is trained at x4 but officially
supports smaller outscales.  These wrappers keep that resize inside ONNX so
the application receives exact 2x or 3x dimensions without a second app-level
scaling pass.
"""

import argparse
from pathlib import Path

import onnx
from onnx import TensorProto, helper


def make_variant(source: Path, destination: Path, scale: int) -> None:

    model = onnx.load(str(source))
    graph = model.graph
    if len(graph.output) != 1:
        raise RuntimeError("expected one Real-ESRGAN output")

    original = graph.output[0]
    original_name = original.name
    output_name = f"output_x{scale}"
    scales_name = f"output_x{scale}_scales"
    scales = [1.0, 1.0, scale / 4.0, scale / 4.0]
    graph.initializer.append(helper.make_tensor(
        scales_name, TensorProto.FLOAT, [4], scales))
    graph.node.append(helper.make_node(
        "Resize", [original_name, "", scales_name], [output_name],
        name=f"ResizeToX{scale}", mode="linear",
        coordinate_transformation_mode="half_pixel"))
    graph.output[0].CopyFrom(helper.make_tensor_value_info(
        output_name, TensorProto.FLOAT, [1, 3, "scaled_height", "scaled_width"]))
    onnx.checker.check_model(model)
    onnx.save(model, str(destination))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    for scale in (2, 3):
        make_variant(args.source, args.output_directory / f"realesr-general-x{scale}.onnx", scale)


if __name__ == "__main__":
    main()
