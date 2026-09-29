#!/usr/bin/env python3
"""Stage a relocatable Rendepth.app and both browser registrations in a pkg."""

import argparse
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
PLUGINS = (
    "app", "coreelements", "dtls", "nice", "rtp", "rtpmanager",
    "srtp", "typefindfunctions", "vpx", "webrtc",
)


def run(*command):
    subprocess.run([str(part) for part in command], check=True)


def write_manifest(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(contents, indent=2) + "\n")


def stage(bundle, native_host, destination, chrome_id, plugin_directory, plugin_scanner, ca_bundle):
    staged_app = destination / "Applications/Rendepth.app"
    shutil.copytree(bundle, staged_app, symlinks=True)
    if not ca_bundle.is_file() or b"-----BEGIN CERTIFICATE-----" not in ca_bundle.read_bytes():
        raise ValueError(f"A PEM CA certificate bundle is required: {ca_bundle}")
    shutil.copyfile(ca_bundle, staged_app / "Contents/Resources/cacert.pem")
    helpers = staged_app / "Contents/Helpers"
    helpers.mkdir(exist_ok=True)
    host_path = helpers / "RendepthNativeHost"
    shutil.copy2(native_host, host_path)
    if not host_path.is_file() or not host_path.stat().st_mode & 0o111:
        raise ValueError("The frozen native host executable is missing")
    plugins = staged_app / "Contents/PlugIns"
    plugins.mkdir(parents=True, exist_ok=True)
    for name in PLUGINS:
        source = plugin_directory / f"libgst{name}.dylib"
        if not source.is_file():
            raise ValueError(f"Required GStreamer plugin is missing: {source}")
        shutil.copy2(source, plugins / source.name)
    if not plugin_scanner.is_file():
        raise ValueError(f"GStreamer plugin scanner is missing: {plugin_scanner}")
    shutil.copy2(plugin_scanner, staged_app / "Contents/MacOS/gst-plugin-scanner")
    run("cmake", f"-DRENDEPTH_BUNDLE={staged_app}", "-P",
        ROOT / "Packaging/Mac/FixupBundle.cmake")

    installed_host = "/Applications/Rendepth.app/Contents/Helpers/RendepthNativeHost"
    common = {"name": "com.outmode.rendepth", "path": installed_host, "type": "stdio"}
    write_manifest(destination / "Library/Application Support/Mozilla/NativeMessagingHosts/com.outmode.rendepth.json",
        {**common, "description": "Rendepth Firefox companion bridge",
         "allowed_extensions": ["firefox@rendepth.outmode"]})
    write_manifest(destination / "Library/Google/Chrome/NativeMessagingHosts/com.outmode.rendepth.json",
        {**common, "description": "Rendepth Chrome companion bridge",
         "allowed_origins": [f"chrome-extension://{chrome_id}/"]})
    return staged_app


def sign_app(bundle, identity):
    # Sign every nested Mach-O file before sealing the application bundle.
    for path in sorted(bundle.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        if not path.is_file() or path.is_symlink():
            continue
        if path == bundle / "Contents/MacOS/Rendepth":
            continue
        result = subprocess.run(["file", "-b", str(path)], capture_output=True, text=True, check=True)
        if "Mach-O" in result.stdout:
            run("codesign", "--force", "--options", "runtime", "--timestamp",
                "--sign", identity, path)
    run("codesign", "--force", "--options", "runtime", "--timestamp",
        "--sign", identity, bundle)
    run("codesign", "--verify", "--deep", "--strict", "--verbose=2", bundle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, default=ROOT / "Binary/Rendepth.app")
    parser.add_argument("--native-host", type=Path, required=True,
                        help="PyInstaller onefile output for Packaging/Mac/native_host.py")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--signed-bundle-output", type=Path,
                        help="Keep a copy of the fully staged and signed app")
    parser.add_argument("--chrome-extension-id", required=True,
                        help="Chrome Web Store item ID from the developer dashboard")
    parser.add_argument("--plugin-directory", type=Path,
                        default=Path("/opt/homebrew/lib/gstreamer-1.0"))
    parser.add_argument("--plugin-scanner", type=Path,
        default=Path("/opt/homebrew/opt/gstreamer/libexec/gstreamer-1.0/gst-plugin-scanner"))
    parser.add_argument("--ca-bundle", type=Path,
        default=Path("/opt/homebrew/etc/ca-certificates/cert.pem"),
        help="PEM CA bundle to embed for model downloads and licensing HTTPS")
    parser.add_argument("--application-identity", help="Developer ID Application identity")
    parser.add_argument("--installer-identity", help="Developer ID Installer identity")
    args = parser.parse_args()
    if not re.fullmatch("[a-p]{32}", args.chrome_extension_id):
        parser.error("Chrome extension ID must contain 32 letters from a to p")
    if args.installer_identity and not args.application_identity:
        parser.error("An Installer identity also requires an Application identity")
    with (args.bundle / "Contents/Info.plist").open("rb") as stream:
        version = plistlib.load(stream)["CFBundleShortVersionString"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rendepth-pkg-") as temporary:
        temporary_root = Path(temporary)
        destination = temporary_root / "payload"
        destination.mkdir()
        bundle = stage(args.bundle, args.native_host, destination,
                       args.chrome_extension_id, args.plugin_directory, args.plugin_scanner,
                       args.ca_bundle)
        if args.application_identity:
            sign_app(bundle, args.application_identity)
        if args.signed_bundle_output:
            if args.signed_bundle_output.exists():
                raise FileExistsError(args.signed_bundle_output)
            args.signed_bundle_output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(bundle, args.signed_bundle_output, symlinks=True)
        component_plist = temporary_root / "components.plist"
        with component_plist.open("wb") as stream:
            plistlib.dump([{
                "RootRelativeBundlePath": "Applications/Rendepth.app",
                "BundleIsRelocatable": False,
                "BundleIsVersionChecked": False,
                "BundleHasStrictIdentifier": False,
                "BundleOverwriteAction": "upgrade",
            }], stream)
        component = temporary_root / "Rendepth-component.pkg"
        run("pkgbuild", "--root", destination, "--install-location", "/",
            "--component-plist", component_plist,
            "--identifier", "com.outmode.rendepth", "--version", version,
            component)
        distribution = temporary_root / "Distribution.xml"
        run("productbuild", "--synthesize", "--package", component, distribution)
        tree = ET.parse(distribution)
        title = ET.Element("title")
        title.text = f"Rendepth {version}"
        tree.getroot().insert(0, title)
        resources = temporary_root / "Resources"
        resources.mkdir()
        license_name = "RENDEPTH_APP_LICENSE.txt"
        shutil.copyfile(ROOT / "Legal/RENDEPTH_APP_LICENSE", resources / license_name)
        tree.getroot().insert(1, ET.Element("license", {
            "file": license_name, "mime-type": "text/plain"}))
        tree.write(distribution, encoding="utf-8", xml_declaration=True)
        command = ["productbuild", "--distribution", distribution,
                   "--package-path", temporary_root, "--resources", resources]
        if args.installer_identity:
            command += ["--sign", args.installer_identity]
        command.append(args.output)
        run(*command)
    print(f"Created {args.output}")
    if not args.installer_identity:
        print("TEST PACKAGE ONLY: the installer package is unsigned")


if __name__ == "__main__":
    main()
