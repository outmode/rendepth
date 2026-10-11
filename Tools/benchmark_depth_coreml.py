#!/usr/bin/env python3
"""Time a compiled native Core ML depth model and optionally compare ONNX output."""

import argparse
from pathlib import Path
import time

import coremltools as ct
import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path, help="Compiled .mlmodelc directory")
    parser.add_argument("--size", type=int, required=True)
    parser.add_argument("--onnx", type=Path, help="Reference ONNX model")
    args = parser.parse_args()
    if not args.model.is_dir() or args.model.suffix != ".mlmodelc":
        parser.error("model must be a compiled .mlmodelc directory")

    started = time.monotonic()
    model = ct.models.CompiledMLModel(str(args.model),
                                     compute_units=ct.ComputeUnit.CPU_AND_GPU)
    print(f"Core ML load: {time.monotonic() - started:.3f}s", flush=True)

    image = np.random.default_rng(42).normal(
        0, 1, (1, 3, args.size, args.size)).astype(np.float32)
    for run in range(2):
        started = time.monotonic()
        outputs = model.predict({"input": image})
        print(f"Core ML prediction {run + 1}: "
              f"{time.monotonic() - started:.3f}s", flush=True)
    name, output = next(iter(outputs.items()))
    print(f"Output: {name} {output.shape}", flush=True)

    if args.onnx:
        import onnxruntime as ort
        session = ort.InferenceSession(str(args.onnx),
                                       providers=["CPUExecutionProvider"])
        expected = session.run(None, {session.get_inputs()[0].name: image})[0]
        if expected.ndim == 4 and expected.shape[1] == 1:
            expected = expected[:, 0]
        if expected.shape != output.shape:
            raise ValueError(f"output shapes differ: {expected.shape} vs {output.shape}")
        difference = np.abs(expected - output)
        print(f"ONNX comparison: mean absolute error {difference.mean():.5f}; "
              f"max absolute error {difference.max():.5f}; "
              f"relative mean error "
              f"{difference.mean() / (np.abs(expected).mean() + 1e-6):.5%}",
              flush=True)


if __name__ == "__main__":
    main()
