#!/usr/bin/env python3
"""Firefox native host for image transfer and SBS WebRTC capture.

Only the registered extension can start this host. No TCP listener, URLs,
commands, or caller-chosen filesystem paths are accepted from the browser.
"""
import argparse
import base64
import binascii
from contextlib import contextmanager
import errno
import json
import os
from pathlib import Path
import select
import shutil
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time

if os.name == "nt":
    import winbridge
else:
    import fcntl

MAX_MESSAGE = 128 * 1024
MAX_SDP = 64 * 1024
MAX_IMAGE = 32 * 1024 * 1024
MAX_IMAGE_PIXELS = 32 * 1024 * 1024


class ImageTransfer:
    """Bounded PNG transfer; neither URLs nor file paths cross this protocol."""
    def __init__(self, message):
        self.size = message.get("size")
        self.format = message.get("format")
        self.swap = message.get("swap", False)
        if (type(self.size) is not int or not 24 <= self.size <= MAX_IMAGE or
                self.format not in ("2d", "sbs-half", "sbs-full") or type(self.swap) is not bool):
            raise ValueError("Invalid image size or format")
        self.data = bytearray()

    def append(self, message):
        encoded = message.get("data")
        if not isinstance(encoded, str) or not 0 < len(encoded) <= 65536:
            raise ValueError("Invalid image chunk")
        try:
            chunk = base64.b64decode(encoded, validate=True)
        except (ValueError, binascii.Error) as error:
            raise ValueError("Invalid image encoding") from error
        if len(self.data) + len(chunk) > self.size:
            raise ValueError("Image exceeds declared size")
        self.data.extend(chunk)

    def save(self, directory):
        if (len(self.data) != self.size or self.data[:16] !=
                b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR"):
            raise ValueError("Incomplete or invalid PNG image")
        width, height = struct.unpack(">II", self.data[16:24])
        if (not width or not height or width > 16384 or height > 16384 or
                width * height > MAX_IMAGE_PIXELS or
                (self.format in ("sbs-half", "sbs-full") and (width < 2 or width % 2))):
            raise ValueError("Unsupported image dimensions")
        (directory / "image.png").write_bytes(self.data)


def read_exact(stream, size):
    result = bytearray()
    while len(result) < size:
        chunk = stream.read(size - len(result))
        if not chunk:
            raise EOFError("Truncated native message")
        result.extend(chunk)
    return bytes(result)


def read_message(stream):
    first = stream.read(1)
    if not first:
        return None
    length = struct.unpack("=I", first + read_exact(stream, 3))[0]
    if not 0 < length <= MAX_MESSAGE:
        raise ValueError("Native message exceeds the signalling limit")
    message = json.loads(read_exact(stream, length))
    if not isinstance(message, dict):
        raise ValueError("Expected a message object")
    return message


def send(message):
    data = json.dumps(message).encode("utf-8")
    sys.stdout.buffer.write(struct.pack("=I", len(data)) + data)
    sys.stdout.buffer.flush()


def wait_for_image_disconnect(input_stream):
    # Keep the native port open until Firefox has received image-opened and
    # closed it. Exiting immediately can deliver a disconnect before the final
    # response on Windows. Bound the wait if the browser never closes the port.
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        ready = (winbridge.input_ready(input_stream) if os.name == "nt" else
                 select.select([input_stream], [], [], 0.05)[0])
        if ready:
            if read_message(input_stream) is None:
                return
            raise ValueError("Unexpected image message after completion")
        if os.name == "nt":
            time.sleep(0.05)


def validate_offer(message):
    if message.get("transport") != "webrtc-vp8":
        raise ValueError("Reload the Rendepth extension: this host requires WebRTC video")
    sdp = message.get("sdp")
    if not isinstance(sdp, str) or not 0 < len(sdp.encode("utf-8")) <= MAX_SDP or "\x00" in sdp:
        raise ValueError("Invalid video session description")
    if not sdp.startswith("v=0") or "m=video " not in sdp or "m=audio " in sdp or "m=application " in sdp:
        raise ValueError("Expected a video-only WebRTC offer")
    return sdp


def runtime_directory():
    if os.name == "nt":
        return winbridge.runtime_directory()
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    directory = Path(runtime) / "rendepth-browser" if runtime else Path(f"/tmp/rendepth-browser-{os.getuid()}")
    directory.mkdir(mode=0o700, exist_ok=True)
    info = directory.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise ValueError("Rendepth needs a private browser bridge directory")
    return directory


def connect_existing(client, registry, request):
    # Prefer the most recently opened instance when several windows are open.
    endpoints = []
    for path in registry.glob("*.sock"):
        try:
            endpoints.append((path.stat().st_mtime_ns, path))
        except FileNotFoundError:
            pass
    for _, endpoint in sorted(endpoints, reverse=True):
        try:
            client.sendto(request, str(endpoint))
        except OSError as error:
            if error.errno in (errno.ENOENT, errno.ECONNREFUSED):
                continue  # A crashed/closed instance may leave a stale socket.
            raise
        try:
            reply, sender = client.recvfrom(4096)
        except socket.timeout as error:
            raise ValueError("The open Rendepth window is not responding. Try again when it is ready.") from error
        if sender != str(endpoint):
            raise ValueError("Unexpected Rendepth bridge response")
        response = json.loads(reply)
        if not isinstance(response, dict):
            raise ValueError("Invalid Rendepth bridge response")
        if response.get("error"):
            raise ValueError(response["error"])
        pid = response.get("pid")
        if type(pid) is not int or pid <= 0:
            raise ValueError("Invalid Rendepth process ID")
        return pid
    return None


def attach(executable, directory, format_name, swap, image=False):
    if os.name == "nt":
        return winbridge.attach(executable, directory, format_name, swap, image)
    registry = runtime_directory()
    request = json.dumps({"directory": str(directory), "format": format_name,
                          "swap": swap, "image": image}).encode()
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
        client.bind(str(directory / "reply.sock"))
        client.settimeout(15)
        pid = connect_existing(client, registry, request)
        if pid is not None:
            return pid, None
        # Serialize fallback launches, then recheck in case another host started it.
        with (registry / "launch.lock").open("a") as launch_lock:
            fcntl.flock(launch_lock, fcntl.LOCK_EX)
            pid = connect_existing(client, registry, request)
            if pid is not None:
                return pid, None
            process = subprocess.Popen([str(executable)], stdin=subprocess.DEVNULL,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                       start_new_session=True)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise ValueError(f"Rendepth exited with code {process.returncode}. Check the app build and runtime libraries.")
                pid = connect_existing(client, registry, request)
                if pid is not None:
                    return pid, process
                time.sleep(0.1)
            raise ValueError("Rendepth did not open its browser connection. Rebuild and restart the app.")


def process_alive(pid):
    if os.name == "nt":
        return winbridge.process_alive(pid)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def publish(path, text):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text)
    deadline = time.monotonic() + (5 if os.name == "nt" else 0)
    replace_deadline = time.monotonic() + 0.25
    while True:
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if os.name != "nt" or time.monotonic() >= deadline:
                raise
            if path.name == "state" and time.monotonic() >= replace_deadline:
                try:
                    # Older Windows viewers keep state open during the whole
                    # frame loop, leaving no reliable gap for a rename. A
                    # single unbuffered write lets their next read see the
                    # complete short state value; an empty read is ignored.
                    with path.open("r+b", buffering=0) as state:
                        state.write(text.encode("ascii"))
                        state.truncate()
                    temporary.unlink()
                    return
                except PermissionError:
                    pass
            time.sleep(0.01)


@contextmanager
def session_directory():
    directory = Path(tempfile.mkdtemp(prefix="rendepth-firefox-")).resolve()
    if directory.parent != Path(tempfile.gettempdir()).resolve() or not directory.name.startswith("rendepth-firefox-"):
        raise ValueError("Invalid browser session directory")
    try:
        yield directory
    finally:
        # The viewer detects a stopped host asynchronously. Windows cannot
        # remove a file while the viewer still has it open, so wait for it to
        # release the directory instead of reporting a spurious WinError 32.
        deadline = time.monotonic() + (8 if os.name == "nt" else 0)
        while True:
            try:
                shutil.rmtree(directory)
                break
            except FileNotFoundError:
                break
            except PermissionError:
                if os.name != "nt" or time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)


def run(executable):
    # select() watches the FD, so do not hide later control messages in a buffered read.
    input_stream = getattr(sys.stdin.buffer, "raw", sys.stdin.buffer)
    process = None
    receiver = None
    started = False
    answered = False
    answer_deadline = None
    settings = None
    generation = 0
    navigating = False
    request_id = None
    image_transfer = None
    image_deadline = None
    with session_directory() as directory:
        # A killed native host cannot run TemporaryDirectory cleanup. Let the
        # viewer distinguish that from a healthy but paused browser video.
        heartbeat = directory / "heartbeat"
        heartbeat.touch()
        next_heartbeat = time.monotonic() + 1
        while True:
            if image_deadline is not None and time.monotonic() > image_deadline:
                raise ValueError("Image transfer timed out")
            if time.monotonic() >= next_heartbeat:
                heartbeat.touch()
                next_heartbeat = time.monotonic() + 1
            if receiver is not None:
                if process is not None:
                    process.poll()
                error_path = directory / "error"
                if error_path.exists():
                    raise ValueError(error_path.read_text()[:4096])
                if (directory / "closed").exists() or not process_alive(receiver):
                    return
                if not answered:
                    answer_path = directory / ("answer.sdp" if generation == 0 else f"answer-{generation}.sdp")
                    if answer_path.exists():
                        if answer_path.stat().st_size > MAX_SDP:
                            raise ValueError("Rendepth returned an oversized session description")
                        reply = {"action": "answer", "sdp": answer_path.read_text()}
                        if request_id is not None:
                            reply["requestId"] = request_id
                        send(reply)
                        answered = True
                    elif time.monotonic() > answer_deadline:
                        raise ValueError("Timed out preparing Rendepth WebRTC. Check its GStreamer plugins.")
            if not (winbridge.input_ready(input_stream) if os.name == "nt" else
                    select.select([input_stream], [], [], 0.05)[0]):
                if os.name == "nt":
                    time.sleep(0.05)
                continue
            message = read_message(input_stream)
            if message is None:
                return
            action = message.get("action")
            if action == "image-begin" and not started and image_transfer is None:
                image_transfer = ImageTransfer(message)
                image_deadline = time.monotonic() + 60
                send({"action": "image-ready"})
                continue
            if image_transfer is not None:
                if action == "image-chunk":
                    image_transfer.append(message)
                    send({"action": "image-ready"})
                    continue
                if action == "image-end":
                    image_transfer.save(directory)
                    attach(executable, directory, image_transfer.format, image_transfer.swap, image=True)
                    send({"action": "image-opened"})
                    wait_for_image_disconnect(input_stream)
                    return
                raise ValueError("Unexpected image message")
            if message.get("action") == "navigate" and started:
                publish(directory / "state", "waiting")
                navigating = True
                answered = True  # Cancel any pending answer/deadline from the old page.
                continue
            if message.get("action") == "start":
                format_name = message.get("format")
                swap = message.get("swap")
                if format_name not in ("2d", "sbs-half", "sbs-full") or type(swap) is not bool:
                    raise ValueError("Choose SBS Half or SBS Full and an eye order")
                sdp = validate_offer(message)
                request_id = message.get("requestId")
                if request_id is not None and (not isinstance(request_id, str) or not 0 < len(request_id) <= 64):
                    raise ValueError("Invalid capture request ID")
                if started:
                    if settings != (format_name, swap) or not navigating:
                        raise ValueError("Unexpected capture replacement")
                    generation += 1
                    publish(directory / f"offer-{generation}.sdp", sdp)
                    publish(directory / "state", str(generation))
                    navigating = False
                else:
                    (directory / "offer.sdp").write_text(sdp)
                    publish(directory / "state", "0")
                    settings = (format_name, swap)
                    started = True
                    receiver, process = attach(executable, directory, format_name, swap)
                answered = False
                answer_deadline = time.monotonic() + 15
            else:
                raise ValueError("Unexpected capture message")
    # Removing the directory stops the receiver without terminating the app.


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rendepth", required=True, type=Path)
    # Firefox appends its native manifest path and extension ID.
    args, _ = parser.parse_known_args()
    try:
        if not args.rendepth.is_file() or not os.access(args.rendepth, os.X_OK):
            raise ValueError("The configured Rendepth executable is missing or not executable")
        run(args.rendepth.resolve())
    except (ValueError, OSError, EOFError) as error:
        send({"error": str(error)})
