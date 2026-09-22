#!/usr/bin/env python3
"""Exercise real Firefox image decoding/PNG transfer in an isolated profile.

The native port is mocked; host and Unix bridge tests cover the receiving side.
No changes are made to the user's profile or native-host registration.
"""
import socket
import subprocess
import tempfile
from pathlib import Path

from test_stream import Firefox, wait_for, ROOT


def run():
    with tempfile.TemporaryDirectory(prefix="rendepth-image-test-") as temporary:
        root = Path(temporary)
        profile = root / "profile"
        profile.mkdir()
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        (profile / "user.js").write_text(
            f'user_pref("marionette.port", {port});\n'
            'user_pref("browser.shell.checkDefaultBrowser", false);\n')
        connection = None
        with (root / "firefox.log").open("w") as log:
            process = subprocess.Popen(["firefox", "--headless", "--no-remote", "--profile", str(profile),
                                        "--marionette", "about:blank"], stdout=log, stderr=log)
            try:
                def connect():
                    try:
                        return socket.create_connection(("127.0.0.1", port), timeout=1)
                    except OSError:
                        return None
                connection = wait_for(connect)
                connection.settimeout(30)
                firefox = Firefox(connection)
                firefox.script("""
window.messages = [];
window.browser = {runtime: {connectNative() {
  let receive;
  return {onMessage: {addListener(fn) {receive = fn;}},
    onDisconnect: {addListener() {}}, disconnect() {},
    postMessage(message) {
      messages.push(message);
      queueMicrotask(() => receive({action: message.action === 'image-end' ? 'image-opened' : 'image-ready'}));
    }};
}}};
""" + (ROOT / "extension/images.js").read_text() + "\nwindow.testOpen = openWebImage;")
                firefox.script("""
window.result = null;
(async () => {
  const canvas = document.createElement('canvas');
  canvas.width = 3840; canvas.height = 1080;
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 1920, 1080);
  ctx.fillStyle = '#0000ff'; ctx.fillRect(1920, 0, 1920, 1080);
  const srcUrl = canvas.toDataURL('image/png');
  await testOpen({srcUrl}, {id: 1}, 'sbs-full', Promise.resolve(true), new AbortController().signal);
  const chunks = messages.filter(m => m.action === 'image-chunk').map(m =>
    Uint8Array.from(atob(m.data), c => c.charCodeAt(0)));
  const bitmap = await createImageBitmap(new Blob(chunks, {type:'image/png'}));
  ctx.clearRect(0, 0, canvas.width, canvas.height);ctx.drawImage(bitmap, 0, 0);
  const left = Array.from(ctx.getImageData(0, 0, 1, 1).data);
  const right = Array.from(ctx.getImageData(3839, 0, 1, 1).data);
  window.result = {width: bitmap.width, height: bitmap.height, left, right,
    format: messages[0].format, end: messages.at(-1).action};
  bitmap.close();
})().catch(error => window.result = {error: String(error)});
""")
                result = wait_for(lambda: firefox.script("return window.result;"))
                assert result == {"width": 3840, "height": 1080, "left": [255, 0, 0, 255],
                                  "right": [0, 0, 255, 255], "format": "sbs-full", "end": "image-end"}, result
                print("Firefox image round trip: 3840x1080, full SBS dimensions and exact red/blue eye pixels passed.")
            finally:
                if connection:
                    connection.close()
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    run()
