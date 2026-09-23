#!/usr/bin/env python3
"""Run isolated Windows runtime tests; never change application preferences."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("cpu_library", type=Path)
    parser.add_argument("--depth-model", type=Path)
    parser.add_argument("--sr-model", type=Path)
    parser.add_argument("--depth-size", type=int, default=280)
    parser.add_argument("--cuda-pack", type=Path)
    parser.add_argument("--directml-pack", type=Path)
    args = parser.parse_args()
    if args.sr_model and not args.depth_model:
        parser.error("Supply a depth model with the super-resolution model")
    if (args.cuda_pack or args.directml_pack) and not args.depth_model:
        parser.error("Real GPU pack validation requires a depth model")
    probe = args.probe.resolve(strict=True)
    cpu = args.cpu_library.resolve(strict=True)
    # Keep fixtures in the workspace next to the probe's build outputs.
    with tempfile.TemporaryDirectory(prefix="windows pack tests ", dir=probe.parent) as temporary:
        root = Path(temporary).resolve()
        assert root.parent == probe.parent
        app, packs = root / "application with spaces", root / "packs with spaces"
        cpu_dir = app / "Runtimes/cpu/bin"
        cpu_dir.mkdir(parents=True)
        shutil.copy2(cpu, cpu_dir / "onnxruntime.dll")

        def run(name, backend, expected, status, installed, models=False, pack_root=None, device=None):
            command = [str(probe), str(app), str(pack_root or packs), backend, expected, status, installed]
            if models:
                command += [str(args.depth_model.resolve()),
                            str(args.sr_model.resolve()) if args.sr_model else "-", str(args.depth_size)]
            environment = os.environ.copy()
            environment.pop("RENDEPTH_DIRECTML_DEVICE", None)
            if device is not None:
                environment["RENDEPTH_DIRECTML_DEVICE"] = str(device)
            result = subprocess.run(command, capture_output=True, text=True, timeout=300, cwd=root, env=environment)
            if result.returncode:
                raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
            print(f"PASS: {name}\n{result.stdout.strip()}", flush=True)

        run("CPU with no GPU packs", "cpu", "cpu", "CPU runtime", "installed", bool(args.depth_model))
        for backend in ("cuda", "directml"):
            run(f"missing {backend}", backend, "cpu", "CPU fallback", "missing")
            folder = packs / backend / "bin"
            folder.mkdir(parents=True)
            (folder / "onnxruntime.dll").write_bytes(b"not a DLL")
            run(f"incomplete {backend}", backend, "cpu", "CPU fallback", "missing")
            required = ["DirectML.dll"] if backend == "directml" else [
                "onnxruntime_providers_cuda.dll", "onnxruntime_providers_shared.dll"]
            for name in required:
                (folder / name).write_bytes(b"not a DLL")
            run(f"broken {backend}", backend, "cpu", "CPU fallback", "installed")
            shutil.copy2(cpu, folder / "onnxruntime.dll")
            run(f"wrong provider in {backend} pack", backend, "cpu", "CPU fallback", "installed")

        for backend, pack in (("cuda", args.cuda_pack), ("directml", args.directml_pack)):
            if pack:
                pack = pack.resolve(strict=True)
                if pack.name != backend:
                    parser.error(f"Pack directory must be named {backend}")
                name = f"real {backend} depth" + (" and SR" if args.sr_model else "")
                run(name, backend, backend,
                    "CUDA runtime" if backend == "cuda" else "DirectML runtime",
                    "installed", True, pack.parent)
                if backend == "directml":
                    run("DirectML session failure retries CPU", backend, "cpu", "CPU fallback",
                        "installed", True, pack.parent, device=999)
        (cpu_dir / "onnxruntime.dll").unlink()
        run("missing CPU", "cpu", "unavailable", "Inference unavailable", "missing")
        (cpu_dir / "onnxruntime.dll").write_bytes(b"not a DLL")
        run("broken CPU", "cpu", "unavailable", "Inference unavailable", "installed")


if __name__ == "__main__":
    main()
