import json
from pathlib import Path
import struct
import tempfile
import unittest

from build_windows_runtime_pack import build_pack


class PackBuilderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ort = self.root / "ort"
        self.native = self.ort / "runtimes/win-x64/native"
        self.native.mkdir(parents=True)
        (self.ort / "LICENSE").write_text("ORT license")
        self.dependencies = self.root / "gpu"
        self.dependencies.mkdir()
        (self.dependencies / "LICENSE.txt").write_text("GPU license")
        self.dll(self.native / "onnxruntime.dll")
        self.dll(self.dependencies / "DirectML.dll")
        self.output = self.root / "output/directml"

    @staticmethod
    def dll(path, machine=0x8664):
        data = bytearray(88)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 60, 64)
        data[64:68] = b"PE\0\0"
        struct.pack_into("<H", data, 68, machine)
        struct.pack_into("<H", data, 86, 0x2000)
        path.write_bytes(data)

    def build(self):
        return build_pack("directml", self.ort, [self.dependencies], [self.dependencies], self.output)

    def test_complete_pack_and_manifest(self):
        self.build()
        manifest = json.loads((self.output / "pack.json").read_text())
        self.assertEqual(manifest["provider"], "directml")
        self.assertEqual(set(manifest["files_sha256"]), {"onnxruntime.dll", "DirectML.dll"})
        self.assertEqual((self.output / "licenses/1/LICENSE.txt").read_text(), "GPU license")

    def test_reject_x86(self):
        self.dll(self.dependencies / "DirectML.dll", 0x14c)
        with self.assertRaisesRegex(ValueError, "x64"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_reject_missing_dependency(self):
        (self.dependencies / "DirectML.dll").unlink()
        with self.assertRaisesRegex(ValueError, "Missing required DLL"):
            self.build()

    def test_does_not_replace_existing_pack(self):
        self.build()
        before = (self.output / "pack.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "Refusing to replace"):
            self.build()
        self.assertEqual((self.output / "pack.json").read_bytes(), before)

    def test_reject_foreign_core_in_dependencies(self):
        self.dll(self.dependencies / "onnxruntime.dll")
        with self.assertRaisesRegex(ValueError, "ORT DLL in dependency"):
            self.build()

    def test_require_dependency_notices(self):
        with self.assertRaisesRegex(ValueError, "notice-dir"):
            build_pack("directml", self.ort, [self.dependencies], [], self.output)

    def test_cuda_requires_runtime_dependencies(self):
        self.dll(self.native / "onnxruntime_providers_cuda.dll")
        self.dll(self.native / "onnxruntime_providers_shared.dll")
        with self.assertRaisesRegex(ValueError, "Missing CUDA dependency"):
            build_pack("cuda", self.ort, [], [], self.output)


if __name__ == "__main__":
    unittest.main()
