#!/usr/bin/env python3
"""Exercise portable ROCm assembly with small ELF fixtures."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import build_linux_rocm_pack_portable as builder


@unittest.skipUnless(shutil.which("gcc") and shutil.which("readelf"), "requires gcc and readelf")
class PortableRocmPackTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="portable rocm pack test ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.ort = self.root / "ort/lib"
        self.runtime = self.root / "runtime/lib"
        self.notices = self.root / "notices"
        self.data = {name: self.root / name for name in ("rocblas", "hipblaslt", "miopen")}
        for directory in (self.ort, self.runtime, self.notices, *self.data.values()):
            directory.mkdir(parents=True)
        for name in ("rocblas", "hipblaslt"):
            (self.data[name] / "library").mkdir()
            (self.data[name] / "library/kernel.dat").write_text("fixture")
        (self.data["miopen"] / "db.txt").write_text("fixture")
        for name in ("LICENSE", "ThirdPartyNotices.txt"):
            (self.ort.parent / name).write_text("ORT fixture notice")
        (self.notices / "LICENSE").write_text("ROCm fixture notice")
        self.library(self.ort / "libonnxruntime.so.1", "libonnxruntime.so.1")
        self.library(self.ort / "libonnxruntime_providers_shared.so")
        self.library(self.runtime / "libMIOpen.so.1", "libMIOpen.so.1")
        self.library(self.ort / "libonnxruntime_providers_rocm.so",
                     dependency=self.runtime / "libMIOpen.so.1")
        for name in ("libhiprtc.so", "libhiprtc-builtins.so", "libhipblaslt.so"):
            self.library(self.runtime / name)
        self.output = self.root / "pack"

    def library(self, path, soname=None, dependency=None):
        command = ["gcc", "-shared", "-fPIC", "-x", "c", "-", "-o", str(path)]
        if soname:
            command.append(f"-Wl,-soname,{soname}")
        if dependency:
            command += ["-Wl,--no-as-needed", "-x", "none", str(dependency)]
        subprocess.run(command, input="int fixture(void) { return 1; }", text=True,
                       check=True, capture_output=True)

    def build(self):
        # The ABI checker is tested separately; these fixture ELFs stand in
        # only for the target library arguments during pack assembly.
        target = [self.ort / "libonnxruntime.so.1"] * 3
        with patch.object(builder, "validate_target_libraries"), \
                patch.object(builder, "check_target_abi", return_value=[]):
            return builder.build_pack(self.ort, [self.runtime], self.data,
                                      self.ort.parent, [self.notices], target, self.output)

    def test_missing_miopen_dependency_is_rejected(self):
        (self.runtime / "libMIOpen.so.1").unlink()
        with patch.object(builder.shutil, "which", return_value="tool"):
            with self.assertRaisesRegex(ValueError, "Expected one input for libMIOpen.so.1"):
                self.build()
        self.assertFalse(self.output.exists())

    def test_target_abi_gap_is_rejected(self):
        with patch.object(builder, "version_names", side_effect=[{"GLIBC_2.35"}] * 3 +
                          [{"GLIBC_2.43"}]):
            with self.assertRaisesRegex(ValueError, "GLIBC_2.43"):
                builder.check_target_abi([self.ort / "libonnxruntime.so.1"],
                                         [self.ort / "libonnxruntime.so.1"] * 3)

    @unittest.skipUnless(shutil.which("patchelf"), "requires patchelf")
    def test_relocatable_pack(self):
        # A vendor build may request an executable stack even though its
        # runtime code does not need one; the release pack must clear it.
        subprocess.run(["gcc", "-shared", "-fPIC", "-Wl,-z,execstack", "-x", "c", "-",
                        "-o", str(self.runtime / "libhipblaslt.so")],
                       input="int fixture(void) { return 1; }", text=True,
                       check=True, capture_output=True)
        self.build()
        manifest = json.loads((self.output / "pack.json").read_text())
        self.assertEqual(manifest["backend"], "rocm")
        for name, digest in manifest["files"].items():
            self.assertEqual(builder.sha256(self.output / name), digest)
        self.assertTrue((self.output / "lib/libMIOpen.so.1").is_file())
        self.assertTrue((self.output / "lib/rocblas/library/kernel.dat").is_file())
        self.assertTrue((self.output / "lib/hipblaslt/library/kernel.dat").is_file())
        self.assertTrue((self.output / "share/miopen/db/db.txt").is_file())
        stack = subprocess.check_output(["readelf", "-W", "-l",
                                         str(self.output / "lib/libhipblaslt.so")], text=True)
        self.assertRegex(stack, r"GNU_STACK\s+[^\n]+ RW\s")
        for path in (self.output / "lib").iterdir():
            if not path.is_file():
                continue
            self.assertEqual(subprocess.check_output(["patchelf", "--print-rpath", str(path)],
                                                     text=True).strip(), "$ORIGIN")


if __name__ == "__main__":
    unittest.main()
