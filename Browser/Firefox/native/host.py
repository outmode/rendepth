#!/usr/bin/env python3
"""Firefox native signalling host for Linux SBS WebRTC capture.

Only the registered extension can start this host. No TCP listener, URLs,
commands, or caller-chosen filesystem paths are accepted from the browser.
"""
import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import select
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time

MAX_MESSAGE = 128 * 1024
MAX_SDP = 64 * 1024


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


def attach(executable, directory, format_name, swap):
    registry = runtime_directory()
    request = json.dumps({"directory": str(directory), "format": format_name, "swap": swap}).encode()
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
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def run(executable):
    process = None
    receiver = None
    started = False
    answered = False
    answer_deadline = None
    with tempfile.TemporaryDirectory(prefix="rendepth-firefox-") as temporary:
        directory = Path(temporary)
        # A killed native host cannot run TemporaryDirectory cleanup. Let the
        # viewer distinguish that from a healthy but paused browser video.
        heartbeat = directory / "heartbeat"
        heartbeat.touch()
        next_heartbeat = time.monotonic() + 1
        while True:
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
                    answer_path = directory / "answer.sdp"
                    if answer_path.exists():
                        if answer_path.stat().st_size > MAX_SDP:
                            raise ValueError("Rendepth returned an oversized session description")
                        send({"action": "answer", "sdp": answer_path.read_text()})
                        answered = True
                    elif time.monotonic() > answer_deadline:
                        raise ValueError("Timed out preparing Rendepth WebRTC. Check its GStreamer plugins.")
            if not select.select([sys.stdin.buffer], [], [], 0.05)[0]:
                continue
            message = read_message(sys.stdin.buffer)
            if message is None:
                return
            if message.get("action") == "start" and not started:
                format_name = message.get("format")
                swap = message.get("swap")
                if format_name not in ("2d", "sbs-half", "sbs-full") or type(swap) is not bool:
                    raise ValueError("Choose SBS Half or SBS Full and an eye order")
                sdp = validate_offer(message)
                (directory / "offer.sdp").write_text(sdp)
                started = True
                receiver, process = attach(executable, directory, format_name, swap)
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
