"""Per-user Windows rendezvous between the Firefox host and Rendepth."""

import ctypes
import json
import msvcrt
import os
from pathlib import Path
import subprocess
import time
import uuid


def runtime_directory():
    base = os.environ.get("LOCALAPPDATA")
    if not base:
        raise ValueError("LOCALAPPDATA is unavailable")
    directory = Path(base) / "Rendepth" / "browser-bridge"
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def process_alive(pid, ready_time_ns=None):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = (ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong)
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.GetExitCodeProcess.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong))
    kernel.GetProcessTimes.argtypes = (ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_void_p, ctypes.c_void_p)
    kernel.CloseHandle.argtypes = (ctypes.c_void_p,)
    handle = kernel.OpenProcess(0x1000, 0, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    code = ctypes.c_ulong()
    alive = bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
    if alive and ready_time_ns is not None:
        created = (ctypes.c_ulong * 2)()
        exited = (ctypes.c_ulong * 2)()
        kernel_time = (ctypes.c_ulong * 2)()
        user_time = (ctypes.c_ulong * 2)()
        if not kernel.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(exited),
                                      ctypes.byref(kernel_time), ctypes.byref(user_time)):
            alive = False
        else:
            # FILETIME is 100 ns since 1601; filesystem timestamps use Unix ns.
            started_ns = ((created[1] << 32 | created[0]) - 116444736000000000) * 100
            alive = started_ns <= ready_time_ns
    kernel.CloseHandle(handle)
    return alive


def input_ready(stream):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.PeekNamedPipe.argtypes = (ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong,
                                     ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p)
    handle = msvcrt.get_osfhandle(stream.fileno())
    available = ctypes.c_ulong()
    if kernel.PeekNamedPipe(handle, None, 0, None, ctypes.byref(available), None):
        return available.value > 0
    if ctypes.get_last_error() in (109, 233):  # Broken or disconnected pipe.
        return True
    raise OSError(ctypes.get_last_error(), "Could not read browser native messages")


def connect_existing(registry, request):
    endpoints = []
    for ready in registry.glob("instance-*/ready"):
        try:
            endpoints.append((ready.stat().st_mtime_ns, ready))
        except FileNotFoundError:
            pass
    for _, ready in sorted(endpoints, reverse=True):
        try:
            pid = int(ready.read_text())
        except (OSError, ValueError):
            continue
        try:
            ready_time_ns = ready.stat().st_mtime_ns
        except FileNotFoundError:
            continue
        if not process_alive(pid, ready_time_ns):
            continue
        nonce = uuid.uuid4().hex
        outgoing = ready.parent / f"request-{nonce}.json"
        pending = ready.parent / f"request-{nonce}.pending"
        reply = ready.parent / f"reply-{nonce}.json"
        try:
            pending.write_bytes(request)
            pending.replace(outgoing)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if reply.exists():
                    response = json.loads(reply.read_bytes())
                    if not isinstance(response, dict):
                        raise ValueError("Invalid Rendepth bridge response")
                    if response.get("error"):
                        raise ValueError(response["error"])
                    if type(response.get("pid")) is not int or response["pid"] != pid:
                        raise ValueError("Invalid Rendepth process ID")
                    return pid
                if not ready.exists() or not process_alive(pid):
                    break
                time.sleep(0.05)
            else:
                raise ValueError("The open Rendepth window is not responding. Try again when it is ready.")
        except FileNotFoundError:
            continue  # The instance closed while this request was prepared.
        finally:
            for path in (pending, outgoing, reply):
                try:
                    path.unlink(missing_ok=True)
                except PermissionError:
                    pass  # The app can be closing a file while it exits.
    return None


def attach(executable, directory, format_name, swap, image=False):
    registry = runtime_directory()
    request = json.dumps({"directory": str(directory), "format": format_name,
                          "swap": swap, "image": image}).encode()
    pid = connect_existing(registry, request)
    if pid is not None:
        return pid, None
    lock_path = registry / "launch.lock"
    with lock_path.open("a+b") as launch_lock:
        launch_lock.seek(0, os.SEEK_END)
        if launch_lock.tell() == 0:
            launch_lock.write(b"0")
            launch_lock.flush()
        launch_lock.seek(0)
        msvcrt.locking(launch_lock.fileno(), msvcrt.LK_LOCK, 1)
        try:
            pid = connect_existing(registry, request)
            if pid is not None:
                return pid, None
            process = subprocess.Popen([str(executable)], stdin=subprocess.DEVNULL,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                       creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise ValueError(f"Rendepth exited with code {process.returncode}. Check the app build and runtime libraries.")
                pid = connect_existing(registry, request)
                if pid is not None:
                    return pid, process
                time.sleep(0.1)
            raise ValueError("Rendepth did not open its browser connection. Rebuild and restart the app.")
        finally:
            launch_lock.seek(0)
            msvcrt.locking(launch_lock.fileno(), msvcrt.LK_UNLCK, 1)
