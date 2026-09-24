#!/usr/bin/env python3
"""Register the Firefox bridge for the current user."""
import argparse
import json
import os
from pathlib import Path
import shlex
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--rendepth", type=Path, required=True)
parser.add_argument("--directory", type=Path, default=(
    Path(os.environ["LOCALAPPDATA"]) / "Rendepth" / "Firefox" if os.name == "nt" else
    Path.home() / ".mozilla/native-messaging-hosts"))
parser.add_argument("--launcher", type=Path, help="Windows FirefoxNativeHost.exe built from this checkout")
args = parser.parse_args()
executable = args.rendepth.resolve(strict=True)
host = Path(__file__).with_name("host.py").resolve()
args.directory.mkdir(parents=True, exist_ok=True)
if os.name == "nt":
    if args.launcher is None:
        parser.error("Windows registration requires --launcher FirefoxNativeHost.exe")
    launcher = args.launcher.resolve(strict=True)
    if launcher.suffix.lower() != ".exe":
        parser.error("--launcher must point to an executable")
    (args.directory / "rendepth-firefox-host.conf").write_text(
        "\n".join((sys.executable, str(host), str(executable))) + "\n", encoding="utf-8")
else:
    launcher = (args.directory / "rendepth-firefox-host").resolve()
    launcher.write_text("#!/bin/sh\nexec " + " ".join(shlex.quote(value) for value in
                        (sys.executable, "-u", str(host), "--rendepth", str(executable))) + ' "$@"\n')
    launcher.chmod(0o700)
manifest = args.directory / "com.outmode.rendepth.json"
manifest.write_text(json.dumps({"name": "com.outmode.rendepth",
    "description": "Rendepth Firefox companion bridge", "path": str(launcher), "type": "stdio",
    "allowed_extensions": ["firefox@rendepth.outmode"]}, indent=2) + "\n")
if os.name == "nt":
    import winreg
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER,
                          r"Software\Mozilla\NativeMessagingHosts\com.outmode.rendepth") as key:
        winreg.SetValueEx(key, "", 0, winreg.REG_SZ, str(manifest.resolve()))
print(f"Registered {manifest}")
