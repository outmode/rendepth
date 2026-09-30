"""Check path detection against both text and native-binary string encodings."""
from pathlib import Path
import tempfile
import unittest
from audit_release_paths import audit


class ReleasePathAuditTest(unittest.TestCase):
    def test_private_paths_and_local_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for index, value in enumerate((b"/home/developer/sdk/header.h", b"/mnt/build/project.cpp",
                                          "C:\\Users\\developer\\build.pdb".encode("utf-16-le"),
                                          b"/private/sdk/library")):
                (root / f"binary{index}").write_bytes(value)
            (root / "Settings.json").write_text("{}")
            self.assertEqual(len(audit(root, ["/private/sdk"])), 5)

    def test_portable_files_and_links(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "library.so.1").write_bytes(b"$ORIGIN/../Library\0./Source/Main.cpp\0/usr/lib64\0")
            (root / "library.so").symlink_to("library.so.1")
            self.assertEqual(audit(root), [])
            (root / "outside").symlink_to("../private-library.so")
            self.assertEqual(len(audit(root)), 1)


if __name__ == "__main__":
    unittest.main()
