"""Windows host rendezvous and executable native-messaging launcher checks."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

if os.name == "nt":
    import winbridge


@unittest.skipUnless(os.name == "nt", "Windows only")
class WindowsBridgeTest(unittest.TestCase):
    def test_reused_process_id_does_not_claim_stale_endpoint(self):
        with tempfile.TemporaryDirectory() as temporary:
            registry = Path(temporary)
            endpoint = registry / f"instance-{os.getpid()}-stale"
            endpoint.mkdir()
            ready = endpoint / "ready"
            ready.write_text(str(os.getpid()))
            os.utime(ready, ns=(0, 0))
            self.assertIsNone(winbridge.connect_existing(registry, b'{}'))

    def test_existing_instance_and_busy_reply(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, LOCALAPPDATA=temporary):
            registry = winbridge.runtime_directory()
            endpoint = registry / f"instance-{os.getpid()}-test"
            endpoint.mkdir()
            (endpoint / "ready").write_text(str(os.getpid()))
            requests = []
            stop = threading.Event()
            busy = False

            def serve():
                while not stop.is_set():
                    for request in endpoint.glob("request-*.json"):
                        requests.append(json.loads(request.read_bytes()))
                        response = {"error": "busy"} if busy else {"pid": os.getpid()}
                        reply = endpoint / request.name.replace("request-", "reply-")
                        pending = reply.with_suffix(".pending")
                        pending.write_text(json.dumps(response))
                        pending.replace(reply)
                        request.unlink(missing_ok=True)
                    time.sleep(0.01)

            worker = threading.Thread(target=serve)
            worker.start()
            try:
                with tempfile.TemporaryDirectory(prefix="rendepth-firefox-") as capture:
                    pid, process = winbridge.attach(Path("missing.exe"), Path(capture), "sbs-full", True, True)
                    self.assertEqual(pid, os.getpid())
                    self.assertIsNone(process)
                    self.assertEqual(requests[-1]["directory"], capture)
                    self.assertEqual(requests[-1]["format"], "sbs-full")
                    self.assertTrue(requests[-1]["swap"])
                    self.assertTrue(requests[-1]["image"])
                    busy = True
                    with self.assertRaisesRegex(ValueError, "busy"):
                        winbridge.attach(Path("missing.exe"), Path(capture), "2d", False)
            finally:
                stop.set()
                worker.join(timeout=2)

    def test_launcher_reads_native_message(self):
        repository = Path(__file__).resolve().parents[3]
        launcher = repository / "Debug" / "FirefoxNativeHost.exe"
        executable = repository / "Debug" / "Rendepth.exe"
        if not launcher.is_file() or not executable.is_file():
            self.skipTest("Build FirefoxNativeHost and Rendepth first")
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            manifest = directory / "com.outmode.rendepth.json"
            manifest.write_text("{}")
            (directory / "rendepth-firefox-host.conf").write_text(
                "\n".join((sys.executable, str(Path(__file__).with_name("host.py")), str(executable))) + "\n",
                encoding="utf-8")
            payload = json.dumps({"action": "invalid"}).encode()
            process = subprocess.run([str(launcher), str(manifest), "firefox@rendepth.outmode"],
                                     input=struct.pack("=I", len(payload)) + payload,
                                     capture_output=True, timeout=10)
            self.assertEqual(process.returncode, 0, process.stderr)
            size = struct.unpack("=I", process.stdout[:4])[0]
            self.assertEqual(json.loads(process.stdout[4:4 + size]),
                             {"error": "Unexpected capture message"})


if __name__ == "__main__":
    unittest.main()
