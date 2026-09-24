"""Send an SBS photo through the registered Windows host into the real app."""
import base64
import json
import struct
import subprocess
import winreg
import zlib
from pathlib import Path


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def packet(value):
    encoded = json.dumps(value).encode()
    return struct.pack("=I", len(encoded)) + encoded


def run():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                        r"Software\Mozilla\NativeMessagingHosts\com.outmode.rendepth") as key:
        manifest = Path(winreg.QueryValueEx(key, "")[0])
    host = Path(json.loads(manifest.read_text())["path"])
    pixels = b"\0" + b"\xff\0\0" + b"\0\0\xff"
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))
    messages = (
        {"action": "image-begin", "size": len(png), "format": "sbs-full", "swap": False},
        {"action": "image-chunk", "data": base64.b64encode(png).decode()},
        {"action": "image-end"},
    )
    result = subprocess.run([str(host), str(manifest), "firefox@rendepth.outmode"],
                            input=b"".join(map(packet, messages)), capture_output=True, timeout=45)
    replies = []
    offset = 0
    while offset + 4 <= len(result.stdout):
        size = struct.unpack_from("=I", result.stdout, offset)[0]
        offset += 4
        replies.append(json.loads(result.stdout[offset:offset + size]))
        offset += size
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert replies == [{"action": "image-ready"}, {"action": "image-ready"},
                       {"action": "image-opened"}], replies
    print("Registered Firefox host opened a two-eye photo in Rendepth.")


if __name__ == "__main__":
    run()
