"""Protocol and lifecycle tests; no browser or desktop session required."""
import io
import json
import os
import socket
import select
import threading
import time
from unittest.mock import patch
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import host


def packet(value):
    data = json.dumps(value).encode()
    return struct.pack("=I", len(data)) + data


class Fragmented(io.BytesIO):
    def read(self, count=-1):
        return super().read(min(count, 2))


class ProtocolTest(unittest.TestCase):
    def test_fragmented_message(self):
        self.assertEqual(host.read_message(Fragmented(packet({"action": "start"}))), {"action": "start"})

    def test_truncated_and_oversized(self):
        for value in (b"\x01", packet({"x": 1})[:-1]):
            with self.assertRaises(EOFError):
                host.read_message(io.BytesIO(value))
        with self.assertRaises(ValueError):
            host.read_message(io.BytesIO(struct.pack("=I", host.MAX_MESSAGE + 1)))

    def test_reject_invalid_offer(self):
        for value in (None, "", "not SDP", "v=0\r\nm=audio 9 RTP/SAVPF 111\r\n",
                      "v=0\r\nm=video " + "x" * host.MAX_SDP,
                      "v=0\r\nm=video 9 RTP/SAVPF 96\x00"):
            with self.assertRaises(ValueError):
                host.validate_offer({"transport": "webrtc-vp8", "sdp": value})
        with self.assertRaisesRegex(ValueError, "Reload"):
            host.validate_offer({"jpeg": "old extension"})

    def test_receiver_error_is_reported_and_session_removed(self):
        offer = {"action": "start", "format": "sbs-full", "swap": False,
                 "transport": "webrtc-vp8", "sdp": "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\n"}
        session_paths = []
        def attach(executable, directory, format_name, swap):
            session_paths.append(directory)
            (directory / "error").write_text("VP8 decoder failed")
            return os.getpid(), None
        with patch.object(host, "attach", side_effect=attach), \
             patch.object(host.sys, "stdin", unittest.mock.Mock(buffer=io.BytesIO(packet(offer)))), \
             patch.object(host.select, "select", return_value=([True], [], [])):
            with self.assertRaisesRegex(ValueError, "VP8 decoder failed"):
                host.run(Path("/bin/false"))
        self.assertEqual(len(session_paths), 1)
        self.assertFalse(session_paths[0].exists())

    def test_media_messages_are_not_accepted(self):
        with patch.object(host.sys, "stdin", unittest.mock.Mock(buffer=io.BytesIO(packet({"action": "frame"})))), \
             patch.object(host.select, "select", return_value=([True], [], [])):
            with self.assertRaisesRegex(ValueError, "Unexpected capture message"):
                host.run(Path("/bin/false"))

    def test_reuses_open_window_and_keeps_it_open_on_disconnect(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            registry = directory / "rendepth-browser"
            registry.mkdir(mode=0o700)
            endpoint = registry / "existing.sock"
            requests = []
            done = threading.Event()
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as server:
                server.bind(str(endpoint))
                server.settimeout(0.1)
                def receive():
                    while not done.is_set():
                        try:
                            data, sender = server.recvfrom(4096)
                        except socket.timeout:
                            continue
                        requests.append(json.loads(data))
                        (Path(requests[-1]["directory"]) / "answer.sdp").write_text("test answer")
                        server.sendto(json.dumps({"pid": os.getpid()}).encode(), sender)
                worker = threading.Thread(target=receive)
                worker.start()
                try:
                    # The executable would fail immediately if the host tried to launch it.
                    for format_name, swap in (("2d", False), ("sbs-full", True), ("sbs-half", False)):
                        environment = dict(os.environ, XDG_RUNTIME_DIR=str(directory))
                        process = subprocess.Popen([sys.executable, "-u", str(Path(host.__file__)),
                                                    "--rendepth", "/bin/false"], env=environment,
                                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE)
                        try:
                            offer = "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                            process.stdin.write(packet({"action": "start", "format": format_name,
                                                       "swap": swap, "transport": "webrtc-vp8", "sdp": offer}))
                            process.stdin.flush()
                            self.assertEqual(host.read_message(process.stdout), {"action": "answer", "sdp": "test answer"})
                            request = requests[-1]
                            capture = Path(request["directory"])
                            self.assertEqual(request["format"], format_name)
                            self.assertEqual(request["swap"], swap)
                            self.assertEqual((capture / "offer.sdp").read_bytes(), offer.encode())
                            self.assertFalse((capture / "frame.jpg").exists())
                            heartbeat = capture / "heartbeat"
                            self.assertTrue(heartbeat.is_file())
                            if format_name == "2d":
                                # Even a paused video sends no further messages;
                                # the host must still advertise that it is alive.
                                initial = heartbeat.stat().st_mtime_ns
                                deadline = time.monotonic() + 3
                                while heartbeat.stat().st_mtime_ns == initial and time.monotonic() < deadline:
                                    time.sleep(0.05)
                                self.assertNotEqual(heartbeat.stat().st_mtime_ns, initial)
                                # Navigation preserves the directory and process. A
                                # replacement offer has its own answer, so an old
                                # answer cannot reconnect the new document by mistake.
                                process.stdin.write(packet({"action": "navigate"}))
                                process.stdin.flush()
                                deadline = time.monotonic() + 3
                                while (capture / "state").read_text() != "waiting" and time.monotonic() < deadline:
                                    time.sleep(0.01)
                                self.assertEqual((capture / "state").read_text(), "waiting")
                                replacement = offer + "a=x-test:new-page\r\n"
                                process.stdin.write(packet({"action": "start", "format": format_name,
                                    "swap": swap, "transport": "webrtc-vp8", "sdp": replacement}))
                                process.stdin.flush()
                                deadline = time.monotonic() + 3
                                while not (capture / "offer-1.sdp").exists() and time.monotonic() < deadline:
                                    time.sleep(0.01)
                                self.assertEqual((capture / "offer-1.sdp").read_bytes(), replacement.encode())
                                self.assertFalse(select.select([process.stdout], [], [], 0.2)[0],
                                                 "Reused the old document's answer")
                                (capture / "answer-1.sdp").write_text("replacement answer")
                                self.assertEqual(host.read_message(process.stdout),
                                                 {"action": "answer", "sdp": "replacement answer"})
                                self.assertEqual(len(requests), 1, "Navigation reattached the viewer")
                            process.stdin.close()
                            process.wait(timeout=5)
                            self.assertEqual(process.returncode, 0)
                            self.assertFalse(capture.exists())
                            self.assertTrue(worker.is_alive())
                        finally:
                            if process.poll() is None:
                                process.kill()
                                process.wait()
                            process.stdin.close()
                            process.stdout.close()
                    self.assertEqual(len(requests), 3)
                finally:
                    done.set()
                    worker.join(timeout=2)

    def test_launches_only_when_no_receiver_exists(self):
        with tempfile.TemporaryDirectory(prefix="rendepth-firefox-") as temporary:
            directory = Path(temporary)
            process = unittest.mock.Mock()
            process.poll.return_value = None
            with patch.object(host, "runtime_directory", return_value=directory), \
                 patch.object(host, "connect_existing", side_effect=[None, None, 123]) as connect, \
                 patch.object(host.subprocess, "Popen", return_value=process) as launch:
                self.assertEqual(host.attach(Path("/example/Rendepth"), directory, "sbs-half", False), (123, process))
                launch.assert_called_once()
                self.assertTrue(launch.call_args.kwargs["start_new_session"])
                self.assertEqual(connect.call_count, 3)

    def test_rechecks_receiver_before_launching(self):
        with tempfile.TemporaryDirectory(prefix="rendepth-firefox-") as temporary:
            directory = Path(temporary)
            with patch.object(host, "runtime_directory", return_value=directory), \
                 patch.object(host, "connect_existing", side_effect=[None, 321]), \
                 patch.object(host.subprocess, "Popen") as launch:
                self.assertEqual(host.attach(Path("/example/Rendepth"), directory, "sbs-full", True), (321, None))
                launch.assert_not_called()

    def test_busy_receiver_does_not_launch_another_window(self):
        with tempfile.TemporaryDirectory(prefix="rendepth-firefox-") as temporary:
            directory = Path(temporary)
            with patch.object(host, "runtime_directory", return_value=directory), \
                 patch.object(host, "connect_existing", side_effect=ValueError("busy")), \
                 patch.object(host.subprocess, "Popen") as launch:
                with self.assertRaisesRegex(ValueError, "busy"):
                    host.attach(Path("/example/Rendepth"), directory, "sbs-half", False)
                launch.assert_not_called()

    def test_skips_stale_socket(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as stale:
                stale.bind(str(directory / "stale.sock"))
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
                client.bind(str(directory / "reply"))
                client.settimeout(0.1)
                self.assertIsNone(host.connect_existing(client, directory, b"{}"))


if __name__ == "__main__":
    unittest.main()
