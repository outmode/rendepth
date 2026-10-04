"""Check the packaged Chrome origin and narrow legacy registration cleanup."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "migrate_chrome_registration", ROOT / "migrate-chrome-registration.py")
MIGRATION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MIGRATION)


class ChromeRegistrationTest(unittest.TestCase):
    def test_system_manifest_uses_store_origin_and_packaged_host(self):
        manifest = json.loads((ROOT / "com.outmode.rendepth.json").read_text())
        self.assertEqual(manifest["allowed_origins"],
                         ["chrome-extension://hffdjljngfgobaekdbgfgfodecmehgbh/"])
        self.assertEqual(manifest["path"],
                         "/usr/libexec/rendepth/Browser/rendepth-chrome-host")

    def test_migration_removes_only_development_registration(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)
            manifest = home / MIGRATION.RELATIVE_MANIFEST
            manifest.parent.mkdir(parents=True)
            registration = {"name": "com.outmode.rendepth",
                            "allowed_origins": [MIGRATION.DEVELOPMENT_ORIGIN]}
            manifest.write_text(json.dumps(registration))
            self.assertTrue(MIGRATION.remove_development_registration(home))
            self.assertFalse(manifest.exists())

            registration["allowed_origins"] = [
                "chrome-extension://hffdjljngfgobaekdbgfgfodecmehgbh/"]
            manifest.write_text(json.dumps(registration))
            self.assertFalse(MIGRATION.remove_development_registration(home))
            self.assertTrue(manifest.exists())

            registration["allowed_origins"] = [MIGRATION.DEVELOPMENT_ORIGIN]
            registration["name"] = "other.host"
            manifest.write_text(json.dumps(registration))
            self.assertFalse(MIGRATION.remove_development_registration(home))
            self.assertTrue(manifest.exists())


if __name__ == "__main__":
    unittest.main()
