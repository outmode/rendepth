"""Verify notice preservation and substitution of the SDK selected by packaging."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from update_legal_notices import ROOT, generate, RUNTIME_MARKER


class LegalNoticesTest(unittest.TestCase):
    def test_current_documents_and_original_licenses(self):
        documents = generate()
        for name, text in documents.items():
            self.assertEqual((ROOT / "Legal" / name).read_bytes(), text.encode("utf-8"))
        app = documents["RENDEPTH_APP_LICENSE"]
        self.assertIn((ROOT / "LICENSE").read_text(), app)
        self.assertIn((ROOT / "ThirdParty/FFmpeg/COPYING.GPLv3").read_text(), app)
        notices = documents["THIRD_PARTY_LICENSING"]
        self.assertEqual(notices.count(RUNTIME_MARKER), 1)
        self.assertIn("https://github.com/outmode/rendepth/releases", app)
        self.assertIn("Source repository: https://github.com/libsdl-org/SDL", notices)
        self.assertIn("https://github.com/microsoft/onnxruntime/tree/", notices)
        self.assertNotIn("https://github.com/outmode/rapidjson-private", notices)
        for path in ["ThirdParty/SDL/LICENSE.txt", "ThirdParty/SDL_image/external/aom/PATENTS",
                     "ThirdParty/SDL_image/external/libavif/LICENSE", "ThirdParty/SDL_image/external/libjxl/LICENSE",
                     "ThirdParty/SyLC/edge264/LICENSE_BSD.txt", "Browser/Chrome/extension/fonts/OFL.txt"]:
            self.assertTrue((ROOT / path).read_bytes().decode("utf-8-sig").rstrip() in notices, path)

    def test_packaging_uses_selected_sdk_and_rejects_missing_notices(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            sdk = base / "private-sdk"
            sdk.mkdir()
            (sdk / "LICENSE").write_text("SDK license sentinel\n")
            (sdk / "ThirdPartyNotices.txt").write_text("SDK third-party sentinel\n")
            script = base / "check.cmake"
            script.write_text(f'''set(CMAKE_SOURCE_DIR "{ROOT.as_posix()}")
set(CMAKE_BINARY_DIR "{base.as_posix()}/build")
set(RENDEPTH_ENABLE_ONNX_RUNTIME ON)
set(RENDEPTH_CPU_RUNTIME_DIR "{sdk.as_posix()}")
set(RENDEPTH_CPU_RUNTIME_NOTICES_DIR "{sdk.as_posix()}")
set(RENDEPTH_ONNXRUNTIME_DIR "{sdk.as_posix()}")
include("{ROOT.as_posix()}/Packaging/Legal.cmake")
''')
            result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            output = (base / "build/generated/Legal/THIRD_PARTY_LICENSING").read_text()
            runtime = output.split(RUNTIME_MARKER)[1]
            self.assertIn("SDK license sentinel", runtime)
            self.assertIn("SDK third-party sentinel", runtime)
            self.assertNotIn("Tools/Legal/inputs/onnxruntime", runtime)
            self.assertNotIn(str(sdk), output)
            (sdk / "ThirdPartyNotices.txt").unlink()
            result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("ThirdPartyNotices.txt", result.stderr)


if __name__ == "__main__":
    unittest.main()
