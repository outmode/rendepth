"""Native-host manifest/launcher tests without changing user registration."""
import json
from pathlib import Path
import tempfile
import unittest

import install


class InstallerTest(unittest.TestCase):
    def test_manifest_scope_and_launcher(self):
        with tempfile.TemporaryDirectory(prefix='rendepth chrome ') as temporary:
            path = install.install(Path('/bin/true'), Path(temporary), install.extension_id())
            manifest = json.loads(path.read_text())
            self.assertEqual(manifest['allowed_origins'], [f'chrome-extension://{install.extension_id()}/'])
            self.assertNotIn('allowed_extensions', manifest)
            launcher = Path(manifest['path'])
            self.assertEqual(launcher.stat().st_mode & 0o777, 0o700)
            self.assertIn('Firefox/native/host.py', launcher.read_text())
            self.assertIn('"$@"', launcher.read_text())

    def test_reject_invalid_id(self):
        for identity in ('', '*', 'chrome-extension://anything/', 'z' * 32):
            with self.assertRaises(ValueError):
                install.install(Path('/bin/true'), Path('/tmp/unused-rendepth-test'), identity)


if __name__ == '__main__':
    unittest.main()
