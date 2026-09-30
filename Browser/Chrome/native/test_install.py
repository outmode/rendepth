"""Native-host manifest/launcher tests without changing user registration."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import install


class InstallerTest(unittest.TestCase):
    @unittest.skipIf(os.name == 'nt', 'POSIX launcher test')
    def test_manifest_scope_and_launcher(self):
        with tempfile.TemporaryDirectory(prefix='rendepth chrome ') as temporary:
            path = install.install(Path(sys.executable), Path(temporary), install.extension_id())
            manifest = json.loads(path.read_text())
            self.assertEqual(manifest['allowed_origins'], [f'chrome-extension://{install.extension_id()}/'])
            self.assertNotIn('allowed_extensions', manifest)
            launcher = Path(manifest['path'])
            self.assertEqual(launcher.stat().st_mode & 0o777, 0o700)
            self.assertIn('Firefox/native/host.py', launcher.read_text())
            self.assertIn('"$@"', launcher.read_text())

    @unittest.skipUnless(os.name == 'nt', 'Windows launcher test')
    def test_windows_launcher_and_manifest(self):
        repository = Path(__file__).resolve().parents[3]
        app = repository / 'Debug/Rendepth.exe'
        launcher = repository / 'Debug/ChromeNativeHost.exe'
        if not app.is_file() or not launcher.is_file():
            self.skipTest('Build Rendepth and ChromeNativeHost first')
        with tempfile.TemporaryDirectory(prefix='rendepth chrome ') as temporary:
            path = install.install(app, Path(temporary), install.extension_id(), launcher)
            manifest = json.loads(path.read_text())
            self.assertEqual(manifest['allowed_origins'],
                             [f'chrome-extension://{install.extension_id()}/'])
            self.assertNotIn('allowed_extensions', manifest)
            self.assertEqual(Path(manifest['path']).parent, Path(temporary))
            payload = json.dumps({'action': 'invalid'}).encode()
            process = subprocess.run([manifest['path'],
                                      f'chrome-extension://{install.extension_id()}/'],
                                     input=struct.pack('=I', len(payload)) + payload,
                                     capture_output=True, timeout=10)
            self.assertEqual(process.returncode, 0, process.stderr)
            size = struct.unpack('=I', process.stdout[:4])[0]
            self.assertEqual(json.loads(process.stdout[4:4 + size]),
                             {'error': 'Unexpected capture message'})

    def test_reject_invalid_id(self):
        for identity in ('', '*', 'chrome-extension://anything/', 'z' * 32):
            with self.assertRaises(ValueError):
                install.install(Path('/bin/true'), Path('/tmp/unused-rendepth-test'), identity)


if __name__ == '__main__':
    unittest.main()
