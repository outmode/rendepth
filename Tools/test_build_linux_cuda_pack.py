#!/usr/bin/env python3
"""Pack assembly tests using tiny ELF fixtures; no NVIDIA hardware required."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import build_linux_cuda_pack as builder


@unittest.skipUnless(shutil.which("gcc") and shutil.which("readelf"), "requires gcc and readelf")
class CudaPackTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cuda pack test ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ort = self.root / "ort/lib"
        self.vendor = self.root / "vendor"
        self.notices = self.root / "notices"
        for directory in (self.ort, self.vendor, self.notices):
            directory.mkdir(parents=True)
        for name in ("LICENSE", "ThirdPartyNotices.txt"):
            (self.ort.parent / name).write_text("ORT fixture notice")
        (self.notices / "LICENSE").write_text("NVIDIA fixture notice")
        self.output = self.root / "pack"
        self.library(self.ort / "libonnxruntime.so.1", "libonnxruntime.so.1")
        self.library(self.ort / "libonnxruntime_providers_shared.so")
        self.library(self.vendor / "libcudnn.so.9.1", "libcudnn.so.9")
        self.library(self.ort / "libonnxruntime_providers_cuda.so",
                     dependency=self.vendor / "libcudnn.so.9.1")

    def library(self, path, soname=None, dependency=None):
        command = ["gcc", "-shared", "-fPIC", "-x", "c", "-", "-o", str(path)]
        if soname:
            command.append(f"-Wl,-soname,{soname}")
        if dependency:
            command += ["-Wl,--no-as-needed", "-x", "none", str(dependency)]
        subprocess.run(command, input="int fixture(void) { return 1; }", text=True, check=True,
                       capture_output=True)

    def build(self):
        return builder.build_pack(self.ort, [self.vendor], [self.notices], self.output)

    def test_missing_dependency_is_not_published(self):
        (self.vendor / "libcudnn.so.9.1").unlink()
        self.library(self.vendor / "libcudnn.so.8", "libcudnn.so.8")
        with patch.object(builder.shutil, "which", return_value="tool"):
            with self.assertRaisesRegex(ValueError, "Missing dependencies.*libcudnn.so.9"):
                self.build()
        self.assertFalse(self.output.exists())

    def test_existing_pack_is_preserved(self):
        self.output.mkdir()
        marker = self.output / "keep"
        marker.write_text("existing pack")
        with self.assertRaisesRegex(ValueError, "Refusing to replace"):
            self.build()
        self.assertEqual(marker.read_text(), "existing pack")

    def test_conflicting_soname_is_rejected(self):
        self.library(self.vendor / "libcudnn.so.9.2", "libcudnn.so.9")
        with patch.object(builder.shutil, "which", return_value="tool"):
            with self.assertRaisesRegex(ValueError, "Conflicting SONAME"):
                self.build()
        self.assertFalse(self.output.exists())

    @unittest.skipUnless(shutil.which("patchelf"), "requires patchelf")
    def test_cudnn_system_zlib_dependency(self):
        zlib = self.root / "libz.so.1"
        self.library(zlib, "libz.so.1")
        self.library(self.vendor / "libcudnn.so.9.1", "libcudnn.so.9", dependency=zlib)
        self.build()
        manifest = json.loads((self.output / "pack.json").read_text())
        self.assertIn("libz.so.1", manifest["system_dependencies"])
        self.assertFalse((self.output / "lib/libz.so.1").exists())

    @unittest.skipUnless(shutil.which("patchelf"), "requires patchelf")
    def test_relocatable_pack_and_manifest(self):
        original_hash = builder.sha256(self.ort / "libonnxruntime.so.1")
        self.build()
        self.assertEqual(original_hash, builder.sha256(self.ort / "libonnxruntime.so.1"))
        self.assertTrue((self.output / "lib/libcudnn.so.9").is_file())
        manifest = json.loads((self.output / "pack.json").read_text())
        for name, digest in manifest["files"].items():
            self.assertEqual(builder.sha256(self.output / name), digest)
        moved = self.root / "relocated pack"
        self.output.rename(moved)
        provider = moved / "lib/libonnxruntime_providers_cuda.so"
        subprocess.run(["python3", "-c", "import ctypes,sys; ctypes.CDLL(sys.argv[1])", str(provider)],
                       check=True, capture_output=True)
        for library in (moved / "lib").iterdir():
            result = subprocess.run(["patchelf", "--print-rpath", str(library)],
                                    check=True, text=True, capture_output=True)
            self.assertEqual(result.stdout.strip(), "$ORIGIN")


if __name__ == "__main__":
    unittest.main()
