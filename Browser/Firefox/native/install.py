#!/usr/bin/env python3
"""Register the Firefox bridge for the current Linux user."""
import argparse
import json
from pathlib import Path
import shlex
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--rendepth", type=Path, required=True)
parser.add_argument("--directory", type=Path, default=Path.home() / ".mozilla/native-messaging-hosts")
args = parser.parse_args()
executable = args.rendepth.resolve(strict=True)
host = Path(__file__).with_name("host.py").resolve()
args.directory.mkdir(parents=True, exist_ok=True)
launcher = (args.directory / "rendepth-firefox-host").resolve()
launcher.write_text("#!/bin/sh\nexec " + " ".join(shlex.quote(value) for value in
                    (sys.executable, "-u", str(host), "--rendepth", str(executable))) + ' "$@"\n')
launcher.chmod(0o700)
manifest = args.directory / "com.outmode.rendepth.json"
manifest.write_text(json.dumps({"name": "com.outmode.rendepth",
    "description": "Rendepth Firefox video bridge", "path": str(launcher), "type": "stdio",
    "allowed_extensions": ["firefox@rendepth.outmode"]}, indent=2) + "\n")
print(f"Registered {manifest}")
