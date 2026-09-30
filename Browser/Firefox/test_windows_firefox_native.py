"""Check that Firefox can launch the registered host and open an image in Rendepth.

Uses an isolated Firefox profile and appends a diagnostic message sender to the
production extension's background script. It does not exercise the context menu.
"""
import base64
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import zipfile
import zlib

from test_stream import Firefox, ROOT, wait_for


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def run():
    firefox_exe = Path(os.environ.get("PROGRAMFILES", r"C:\Program Files")) / "Mozilla Firefox/firefox.exe"
    runtime = Path(os.environ["APPDATA"]) / "Outmode/Rendepth/Runtime"
    before = set(runtime.glob("Rendepth-*/WebPhoto-*/Photo_sbs.png"))
    pixels = b"\0" + b"\xff\0\0" + b"\0\0\xff"
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))
    diagnostic = """
const testPort = browser.runtime.connectNative('com.outmode.rendepth');
let testStep = 0;
testPort.onMessage.addListener(reply => {
  if (reply.error) { browser.browserAction.setBadgeText({text:'!'}); return; }
  if (reply.action !== 'image-ready') return;
  if (testStep++ === 0)
    testPort.postMessage({action:'image-chunk', data:TEST_PNG});
  else testPort.postMessage({action:'image-end'});
});
testPort.postMessage({action:'image-begin', size:TEST_SIZE, format:'sbs-full', swap:false});
""".replace("TEST_PNG", json.dumps(base64.b64encode(png).decode())).replace("TEST_SIZE", str(len(png)))
    with tempfile.TemporaryDirectory(prefix="rendepth-firefox-native-") as temporary:
        root = Path(temporary)
        profile = root / "profile"
        profile.mkdir()
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        (profile / "user.js").write_text(
            f'user_pref("marionette.port", {port});\n'
            'user_pref("browser.shell.checkDefaultBrowser", false);\n')
        addon = root / "rendepth-native-test.xpi"
        with zipfile.ZipFile(addon, "w") as archive:
            for file in (ROOT / "extension").rglob("*"):
                if file.is_file() and file.name != "background.js":
                    archive.write(file, str(file.relative_to(ROOT / "extension")))
            archive.writestr("background.js", (ROOT / "extension/background.js").read_text(encoding="utf-8") + diagnostic)
        connection = None
        with (root / "firefox.log").open("w") as log:
            browser = subprocess.Popen([str(firefox_exe), "--headless", "--no-remote", "--profile",
                                        str(profile), "--marionette", "about:blank"], stdout=log, stderr=log)
            try:
                def connect():
                    try:
                        return socket.create_connection(("127.0.0.1", port), timeout=1)
                    except OSError:
                        return None
                connection = wait_for(connect)
                connection.settimeout(30)
                firefox = Firefox(connection)
                firefox.call("Addon:Install", path=str(addon), temporary=True)
                def received():
                    matches = set(runtime.glob("Rendepth-*/WebPhoto-*/Photo_sbs.png")) - before
                    return next((path for path in matches if path.read_bytes() == png), None)
                image = wait_for(received, seconds=30)
                print(f"Firefox native messaging opened the exact SBS image in Rendepth: {image.name}")
            finally:
                if connection:
                    connection.close()
                browser.terminate()
                try:
                    browser.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    browser.kill()
                    browser.wait()
                time.sleep(1)


if __name__ == "__main__":
    run()
