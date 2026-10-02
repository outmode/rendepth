"""Check the staged macOS native host layout and browser registrations."""

import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import BuildInstaller


class StageTest(unittest.TestCase):
    def test_postinstall_accepts_staged_browser_registrations(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            host = (root / "Applications/Rendepth.app/Contents/Helpers/"
                    "RendepthNativeHost.app/Contents/MacOS/RendepthNativeHost")
            host.parent.mkdir(parents=True)
            host.write_bytes(b"host")
            host.chmod(0o755)
            manifest_paths = (
                root / "Library/Application Support/Mozilla/NativeMessagingHosts/com.outmode.rendepth.json",
                root / "Library/Google/Chrome/NativeMessagingHosts/com.outmode.rendepth.json",
            )
            for manifest in manifest_paths:
                manifest.parent.mkdir(parents=True)
                manifest.write_text(json.dumps({"path": "/" + str(host.relative_to(root))}))
            script = Path(__file__).with_name("Scripts") / "postinstall"
            command = ["/bin/sh", str(script), "", "", str(root)]
            subprocess.run(command, check=True, capture_output=True)
            manifest_paths[1].unlink()
            failure = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(failure.returncode, 0)
            self.assertIn(str(manifest_paths[1]), failure.stderr)

    def test_stage_uses_signed_helper_app_layout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "Rendepth.app"
            (app / "Contents/MacOS").mkdir(parents=True)
            (app / "Contents/Resources").mkdir()
            (app / "Contents/MacOS/Rendepth").write_bytes(b"app")
            helper = root / "RendepthNativeHost.app"
            executable = helper / "Contents/MacOS/RendepthNativeHost"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"host")
            executable.chmod(0o755)
            (helper / "Contents/Frameworks/Python.framework").mkdir(parents=True)
            with (helper / "Contents/Info.plist").open("wb") as stream:
                plistlib.dump({"CFBundleIdentifier": "com.outmode.rendepth.nativehost"}, stream)
            plugins = root / "plugins"
            plugins.mkdir()
            for name in BuildInstaller.PLUGINS:
                (plugins / f"libgst{name}.dylib").write_bytes(b"plugin")
            scanner = root / "gst-plugin-scanner"
            scanner.write_bytes(b"scanner")
            ca_bundle = root / "cacert.pem"
            ca_bundle.write_bytes(b"-----BEGIN CERTIFICATE-----\ntest\n")
            destination = root / "payload"
            with patch.object(BuildInstaller, "run"):
                BuildInstaller.stage(app, helper, destination, "a" * 32,
                                     plugins, scanner, ca_bundle)
            host_path = (destination / "Applications/Rendepth.app/Contents/Helpers/"
                         "RendepthNativeHost.app/Contents/MacOS/RendepthNativeHost")
            self.assertTrue(host_path.is_file())
            with (host_path.parents[1] / "Info.plist").open("rb") as stream:
                self.assertTrue(plistlib.load(stream)["LSUIElement"])
            for browser in ("Mozilla", "Google/Chrome"):
                if browser == "Mozilla":
                    manifest = destination / "Library/Application Support/Mozilla/NativeMessagingHosts/com.outmode.rendepth.json"
                else:
                    manifest = destination / "Library/Google/Chrome/NativeMessagingHosts/com.outmode.rendepth.json"
                self.assertEqual(json.loads(manifest.read_text())["path"], "/" + str(host_path.relative_to(destination)))

    def test_stage_rejects_onefile_host(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "Rendepth.app"
            (app / "Contents/Resources").mkdir(parents=True)
            onefile = root / "RendepthNativeHost"
            onefile.write_bytes(b"host")
            ca_bundle = root / "ca"
            ca_bundle.write_bytes(b"-----BEGIN CERTIFICATE-----\ntest\n")
            with self.assertRaisesRegex(ValueError, "onedir native host"):
                BuildInstaller.stage(app, onefile, root / "payload", "a" * 32,
                                     root, root / "scanner", ca_bundle)

    def test_migration_removes_only_rendepth_user_manifests(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary) / "a home with spaces"
            firefox = home / "Library/Application Support/Mozilla/NativeMessagingHosts/com.outmode.rendepth.json"
            chrome = home / "Library/Application Support/Google/Chrome/NativeMessagingHosts/com.outmode.rendepth.json"
            for manifest in (firefox, chrome):
                manifest.parent.mkdir(parents=True)
                manifest.write_text(json.dumps({"name": "com.outmode.rendepth", "path": "/old/dev/host"}))
            chrome.write_text("stale or malformed registration")
            unrelated = firefox.parent / "other.example.json"
            unrelated.write_text(json.dumps({"name": "other.example"}))
            script = Path(__file__).with_name("Scripts") / "migrate-user-registrations.sh"
            subprocess.run(["/bin/sh", "-c", '. "$1"; migrate_registration_home "$2"',
                            "test", str(script), str(home)], check=True, capture_output=True)
            self.assertFalse(firefox.exists())
            self.assertFalse(chrome.exists())
            self.assertTrue(unrelated.exists())


if __name__ == "__main__":
    unittest.main()
