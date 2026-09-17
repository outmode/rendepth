#!/usr/bin/env python3
"""Real Firefox -> native WebRTC decode throughput/stereo test, in an isolated profile.

Build BrowserCaptureTest first. No changes to the user's Firefox profile or host
registration. Requires Firefox with Marionette and the receiver's GStreamer plugins.
"""
import argparse
import itertools
import json
import re
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent


class Firefox:
    def __init__(self, connection):
        self.connection = connection
        self.ids = itertools.count(1)
        self.read()
        self.call("WebDriver:NewSession", capabilities={})

    def read(self):
        size = b""
        while (byte := self.connection.recv(1)) != b":":
            if not byte:
                raise EOFError("Firefox closed Marionette")
            size += byte
        data = b""
        while len(data) < int(size):
            chunk = self.connection.recv(int(size) - len(data))
            if not chunk:
                raise EOFError("Firefox closed Marionette")
            data += chunk
        return json.loads(data)

    def call(self, name, **parameters):
        data = json.dumps([0, next(self.ids), name, parameters]).encode()
        self.connection.sendall(str(len(data)).encode() + b":" + data)
        response = self.read()
        if response[2]:
            raise RuntimeError(response[2])
        result = response[3]
        return result.get("value", result) if isinstance(result, dict) else result

    def script(self, script, *args):
        return self.call("WebDriver:ExecuteScript", script=script, args=list(args))


def wait_for(check, seconds=20):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(0.05)
    raise TimeoutError("Timed out waiting for WebRTC setup")


def run(fps, receiver, width=1920, height=1080, half=False, min_fps=None, mono=False):
    with tempfile.TemporaryDirectory(prefix="rendepth-firefox-test-") as temporary:
        directory = Path(temporary)
        profile = directory / "profile"
        profile.mkdir()
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        (profile / "user.js").write_text(
            f'user_pref("marionette.port", {port});\n'
            'user_pref("media.autoplay.default", 0);\n'
            'user_pref("browser.shell.checkDefaultBrowser", false);\n')
        # A file with exact frame timestamps avoids canvas/requestAnimationFrame
        # throttling in headless Firefox from becoming the benchmark's source cap.
        fixture = directory / "fixture.html"
        fixture.write_text((ROOT / "test-video.html").read_text().replace(
            "if (!video.srcObject)", "if (!video.srcObject && !video.src)"))
        video = directory / "source.mp4"
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                        f"testsrc2=size={width}x{height}:rate={fps}:duration=20",
                        # Small stereo markers leave most of the moving detail
                        # intact: full-frame overlays hid bitrate starvation.
                        "-vf", "drawbox=x=0:y=0:w=iw/2:h=32:color=red:t=fill,"
                               "drawbox=x=iw/2:y=0:w=iw/2:h=32:color=blue:t=fill",
                        "-c:v", "libx264", "-preset", "ultrafast", "-crf", "20", "-threads", "4",
                        "-pix_fmt", "yuv420p", str(video)], check=True)
        with (directory / "firefox.log").open("w") as log:
            browser = subprocess.Popen(["firefox", "--headless", "--no-remote", "--profile", str(profile),
                                        "--marionette", "about:blank"], stdout=log, stderr=log)
            native = None
            connection = None
            try:
                def connect():
                    try:
                        return socket.create_connection(("127.0.0.1", port), timeout=1)
                    except OSError:
                        return None
                connection = wait_for(connect)
                connection.settimeout(20)
                firefox = Firefox(connection)
                firefox.call("WebDriver:Navigate", url=fixture.as_uri())
                firefox.script("document.getElementById('video').src = arguments[0]; document.getElementById('play').click(); return true;", video.as_uri())
                wait_for(lambda: firefox.script("return document.getElementById('video').videoWidth;"))
                mock = '''
window.captureResult = {};
const OriginalPeer = window.RTCPeerConnection;
window.RTCPeerConnection = class extends OriginalPeer {
  constructor(...args) { super(...args); window.testPeer = this; }
};
let sourceFrames = 0;
const countSource = () => { ++sourceFrames; document.querySelector('video').requestVideoFrameCallback(countSource); };
document.querySelector('video').requestVideoFrameCallback(countSource);
window.sourceCounter = () => sourceFrames;
window.sourceStarted = performance.now();
window.browser = {runtime: {
  onMessage: {addListener(fn) {window.startCapture = fn;}},
  connect() {return {
    onDisconnect: {addListener(fn) {window.disconnectCapture = fn;}},
    onMessage: {addListener(fn) {window.hostReply = fn;}}, disconnect() {},
    postMessage(message) {
      if (message.action === 'start') captureResult.offer = message;
      if (message.action === 'error') captureResult.error = message.error;
      if (message.action === 'connected') captureResult.connected = true;
    }
  };}
}};
'''
                firefox.script(mock + (ROOT / "extension/capture.js").read_text() + '''
startCapture({action:'start', format:arguments[0], swap:false}).then(reply => captureResult.reply = reply);
return true;
''', '2d' if mono else ('sbs-half' if half else 'sbs-full'))
                result = wait_for(lambda: firefox.script("return captureResult.offer || captureResult.reply;"))
                if result.get("format") != ("2d" if mono else "sbs-full"):
                    raise AssertionError("Incorrect capture format reached native signalling")
                if "sdp" not in result:
                    raise RuntimeError(result)
                (directory / "offer.sdp").write_text(result["sdp"])
                native = subprocess.Popen([str(receiver), str(directory), "5"] + (["2d"] if mono else []), stdout=subprocess.PIPE,
                                          stderr=subprocess.PIPE, text=True)
                def answer():
                    if (directory / "error").exists():
                        raise RuntimeError((directory / "error").read_text())
                    if native.poll() is not None:
                        raise RuntimeError(native.communicate())
                    return (directory / "answer.sdp").read_text() if (directory / "answer.sdp").exists() else None
                sdp = wait_for(answer)
                twcc = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"
                offered = re.search(r"^a=extmap:(\d+) " + re.escape(twcc) + r"\r?$",
                                    result["sdp"], re.MULTILINE)
                if not offered:
                    raise AssertionError("Firefox did not offer transport-wide bandwidth feedback")
                if f"a=extmap:{offered[1]} {twcc}" not in sdp or " transport-cc" not in sdp:
                    raise AssertionError("Receiver dropped bandwidth feedback negotiation")
                if "max-fs=16320" not in sdp:
                    raise AssertionError("Receiver did not advertise full-width SBS frame capacity")
                firefox.script("hostReply({action:'answer',sdp:arguments[0]}); return true;", sdp)
                output, errors = native.communicate(timeout=35)
                firefox.script("testPeer.getStats().then(stats => window.rtcStats = [...stats.values()].filter(s => s.type === 'outbound-rtp' || s.type === 'media-source')); return true;")
                stats = wait_for(lambda: firefox.script("return window.rtcStats;"))
                source_rate = firefox.script("return sourceCounter() * 1000 / (performance.now() - sourceStarted);")
                print(json.dumps({"source_presented_fps": source_rate, "webrtc": stats}), flush=True)
                status = firefox.script("return captureResult;")
                if native.returncode or status.get("error"):
                    raise RuntimeError({"native": output, "errors": errors, "browser": status})
                measured = json.loads(output)
                print(json.dumps({"source_fps": fps, **measured}), flush=True)
                display_width = width * (2 if half else 1)
                scale = max(1, display_width / 3840, height / 1080)
                expected = (int(display_width / scale / 2) * 2, int(height / scale / 2) * 2)
                if (measured["fps"] < (fps * 0.9 if min_fps is None else min_fps) or not measured["stereo"] or
                        (measured["width"], measured["height"]) != expected):
                    raise AssertionError("Stream failed throughput/stereo check")
                firefox.script("disconnectCapture(); return true;")
            finally:
                if connection:
                    connection.close()
                if native and native.poll() is None:
                    native.kill()
                    native.communicate()
                browser.terminate()
                try:
                    browser.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    browser.kill()
                    browser.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receiver", type=Path, default=ROOT.parent.parent / "Debug/BrowserCaptureTest")
    parser.add_argument("--fps", type=int, choices=[30, 60], nargs="+", default=[30, 60])
    parser.add_argument("--width", type=int, choices=[1920, 3840], default=1920)
    parser.add_argument("--height", type=int, choices=[1080, 2160], default=1080)
    parser.add_argument("--half", action="store_true", help="Test half-SBS normalization")
    parser.add_argument("--min-fps", type=float, help="Explicit throughput threshold; default is 90%% of source FPS")
    parser.add_argument("--mono", action="store_true", help="Test 2D capture and native depth inputs")
    args = parser.parse_args()
    if args.mono and args.half:
        parser.error("--mono and --half are mutually exclusive")
    for rate in args.fps:
        run(rate, args.receiver.resolve(), args.width, args.height, args.half, args.min_fps, args.mono)
