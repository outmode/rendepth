#!/usr/bin/env python3
"""Exercise isolated CPU/GPU runtime layouts without modifying user settings."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("probe", type=Path)
parser.add_argument("cpu_library", type=Path)
parser.add_argument("--depth-model", type=Path)
parser.add_argument("--sr-model", type=Path)
args = parser.parse_args()
if bool(args.depth_model) != bool(args.sr_model):
    parser.error("Supply both models to include real inference tests")
probe = args.probe.resolve(strict=True)
cpu = args.cpu_library.resolve(strict=True)

with tempfile.TemporaryDirectory(prefix="rendepth-runtime-test-") as temporary:
    root = Path(temporary)
    app = root / "application with spaces"
    packs = root / "packs with spaces"
    cpu_dir = app / "Runtimes/cpu/lib"
    cpu_dir.mkdir(parents=True)
    (cpu_dir / "libonnxruntime.so.1").symlink_to(cpu)

    def run(name, backend, expected, status, installed, models=False):
        command = [str(probe), str(app), str(packs), backend, expected, status, installed]
        if models:
            command += [str(args.depth_model.resolve()), str(args.sr_model.resolve())]
        result = subprocess.run(command, text=True, capture_output=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f"{name} failed:\n{result.stdout}\n{result.stderr}")
        print(f"PASS {name}: {result.stdout.strip()}")

    run("CPU base", "cpu", "cpu", "CPU runtime", "installed", bool(args.depth_model))
    for backend in ("cuda", "rocm"):
        run(f"missing {backend}", backend, "cpu", "CPU fallback", "missing")
        gpu_dir = packs / backend / "lib"
        gpu_dir.mkdir(parents=True)
        core = gpu_dir / "libonnxruntime.so.1"
        core.write_text("interrupted or corrupt runtime download")
        provider = gpu_dir / f"libonnxruntime_providers_{backend}.so"
        shared = gpu_dir / "libonnxruntime_providers_shared.so"
        provider.write_text("invalid provider fixture")
        shared.write_text("invalid provider fixture")
        run(f"broken {backend}", backend, "cpu", "CPU fallback", "installed")
        core.unlink()
        core.symlink_to(cpu)
        shared.unlink()
        # Presence of the core alone must not claim a pack is complete.
        run(f"incomplete {backend}", backend, "cpu", "CPU fallback", "missing")
        shared.write_text("invalid provider fixture")
        run(f"{backend} core exposes provider dependencies", backend, backend, "runtime", "installed")
        if args.depth_model:
            run(f"{backend} session failure retries CPU", backend, "cpu", "CPU fallback", "installed", True)

    (cpu_dir / "libonnxruntime.so.1").unlink()
    run("missing CPU", "cpu", "unavailable", "Inference unavailable", "missing")
    (cpu_dir / "libonnxruntime.so.1").write_text("broken CPU runtime")
    run("broken CPU", "cpu", "unavailable", "Inference unavailable", "installed")
