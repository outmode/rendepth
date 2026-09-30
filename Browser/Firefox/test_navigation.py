#!/usr/bin/env python3
"""Full-document navigation with real Firefox, production host and native receiver.

The temporary extension uses production background/capture scripts. A localhost
signalling adapter replaces connectNative to avoid altering host registration.
No account, external site, user profile or media renderer is used.
"""
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import tempfile
import threading
import time
import zipfile
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

from test_stream import Firefox, wait_for
from native.host import read_message
import struct

ROOT = Path(__file__).resolve().parent
RECEIVER = ROOT.parent.parent / 'Debug/BrowserCaptureTest'


def run(close_tab=False):
    with tempfile.TemporaryDirectory(prefix='rendepth-navigation-test-') as temporary:
        root = Path(temporary)
        profile = root / 'profile'
        profile.mkdir()
        registry = root / 'rendepth-browser'
        registry.mkdir(mode=0o700)
        replies = queue.Queue()
        captures, receivers, requests = [], [], []
        diagnostics = []
        processes = []
        done = threading.Event()
        native_lock = threading.Lock()
        native = subprocess.Popen(['python3', '-u', str(ROOT / 'native/host.py'), '--rendepth', str(RECEIVER)],
                                  env=dict(os.environ, XDG_RUNTIME_DIR=str(root)),
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        processes.append(native)

        def native_replies():
            try:
                while (reply := read_message(native.stdout)) is not None:
                    replies.put(reply)
            finally:
                replies.put({'closed': True})
        threading.Thread(target=native_replies, daemon=True).start()

        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=str(root), **kwargs)

            def log_message(self, *_args):
                pass

            def do_GET(self):
                if self.path == '/reply':
                    try:
                        reply = replies.get(timeout=0.2)
                    except queue.Empty:
                        reply = None
                    data = json.dumps(reply).encode()
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/json')
                    self.send_header('Content-Length', str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                else:
                    super().do_GET()

            def do_POST(self):
                data = self.rfile.read(int(self.headers.get('Content-Length', 0)))
                if self.path == '/diagnostic':
                    diagnostics.append(json.loads(data))
                    self.send_response(204)
                    self.end_headers()
                    return
                with native_lock:
                    if self.path == '/disconnect':
                        if not native.stdin.closed:
                            native.stdin.close()
                    elif not native.stdin.closed:
                        native.stdin.write(struct.pack('=I', len(data)) + data)
                        native.stdin.flush()
                self.send_response(204)
                self.end_headers()

        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as bridge:
            bridge.bind(str(registry / 'receiver.sock'))
            bridge.settimeout(0.1)

            def receive_request():
                while not done.is_set():
                    try:
                        data, address = bridge.recvfrom(4096)
                    except socket.timeout:
                        continue
                    request = json.loads(data)
                    requests.append(request)
                    capture = Path(request['directory'])
                    captures.append(capture)
                    receiver = subprocess.Popen([str(RECEIVER), str(capture), 'navigation'],
                                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                    receivers.append(receiver)
                    processes.append(receiver)
                    bridge.sendto(json.dumps({'pid': receiver.pid}).encode(), address)
            worker = threading.Thread(target=receive_request, daemon=True)
            worker.start()
            server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            origin = f'http://127.0.0.1:{server.server_port}'
            connection = None
            try:
                subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
                                'testsrc2=size=640x360:rate=30:duration=10', '-c:v', 'libx264',
                                '-preset', 'ultrafast', '-threads', '4', str(root / 'video.mp4')], check=True)
                for name, autoplay in [('first', 'autoplay'), ('next', '')]:
                    (root / f'{name}.html').write_text(f'<!doctype html><video {autoplay} muted loop controls src="video.mp4" width="640"></video>')
                adapter = '''
function testNative() {
  let listener, disconnected, closed = false;
  const poll = async () => {
    while (!closed) {
      const message = await (await fetch(TEST_ORIGIN + '/reply')).json();
      if (message?.closed) {closed = true;disconnected?.();}
      else if (message) listener?.(message);
    }
  };
  void poll();
  return {postMessage(message) {void fetch(TEST_ORIGIN + '/message', {method:'POST', body:JSON.stringify(message)});},
    disconnect() {closed = true;void fetch(TEST_ORIGIN + '/disconnect', {method:'POST'});},
    onMessage:{addListener(fn) {listener = fn;}}, onDisconnect:{addListener(fn) {disconnected = fn;}}};
}
'''
                addon = root / 'test.xpi'
                extension = ROOT / 'extension'
                manifest = json.loads((extension / 'manifest.json').read_text())
                manifest['permissions'].append('http://127.0.0.1/*')
                background = f'const TEST_ORIGIN = {json.dumps(origin)};\n' + adapter
                background += (extension / 'background.js').read_text().replace(
                    'browser.runtime.connectNative("com.outmode.rendepth")', 'testNative()')
                background += ('autoReconnectOrigins.add(new URL(TEST_ORIGIN).origin);\n' if close_tab else '')
                background += '''
setInterval(()=>fetch(TEST_ORIGIN + '/diagnostic',{method:'POST',body:JSON.stringify({status,session:session?{id:session.id,navigating:session.navigating,resuming:session.resuming,content:!!session.content}:null})}),1000);
browser.tabs.onUpdated.addListener((id, change, tab) => {
  if (change.status === 'complete' && tab.url === TEST_ORIGIN + '/first.html')
    void start(id, 0, null, 'sbs-full', false, tab.url);
  if (change.url?.endsWith('#resume')) void start(id, 0, null, 'sbs-full', false, tab.url);
  if (change.url?.endsWith('#stop')) stop();
});
'''
                with zipfile.ZipFile(addon, 'w') as archive:
                    for file in extension.rglob('*'):
                        if file.is_file() and file.name not in ('background.js', 'manifest.json'):
                            archive.write(file, str(file.relative_to(extension)))
                    archive.writestr('background.js', background)
                    archive.writestr('manifest.json', json.dumps(manifest))
                with socket.socket() as reservation:
                    reservation.bind(('127.0.0.1', 0))
                    marionette_port = reservation.getsockname()[1]
                (profile / 'user.js').write_text(f'user_pref("marionette.port", {marionette_port});\nuser_pref("media.autoplay.default", 0);\n')
                with (root / 'firefox.log').open('w') as log:
                    browser = subprocess.Popen(['firefox', '--headless', '--no-remote', '--profile', str(profile),
                                                '--marionette', 'about:blank'], stdout=log, stderr=log)
                    processes.append(browser)
                    def connect():
                        try:
                            return socket.create_connection(('127.0.0.1', marionette_port), timeout=1)
                        except OSError:
                            return None
                    connection = wait_for(connect)
                    connection.settimeout(30)
                    firefox = Firefox(connection)
                    firefox.call('Addon:Install', path=str(addon), temporary=True)
                    firefox.call('WebDriver:Navigate', url=origin + '/first.html')
                    wait_for(lambda: captures and (captures[0] / 'test-first-frame').exists(), seconds=30)
                    capture = captures[0]
                    firefox.call('WebDriver:Navigate', url=origin + '/next.html')
                    wait_for(lambda: (capture / 'state').read_text() == 'waiting')
                    assert firefox.script("return document.querySelector('video').paused;"), 'Resume forced playback'
                    time.sleep(7)
                    if not close_tab:
                        assert diagnostics[-1]['session']['resuming'] is False, 'Default navigation auto-injected capture'
                        firefox.script("location.hash = 'resume'; return true;")
                        wait_for(lambda: diagnostics[-1]['session']['resuming'])
                    assert receivers[0].poll() is None and native.poll() is None, 'Navigation ended the native session'
                    assert capture.exists() and not (capture / 'closed').exists()
                    assert not (capture / 'test-resumed-frame').exists(), 'Captured before user playback'
                    firefox.script("document.querySelector('video').play(); return true;")
                    wait_for(lambda: (capture / 'test-resumed-frame').exists(), seconds=25)
                    assert len(requests) == 1, 'Navigation reattached/reopened the viewer'
                    if close_tab:
                        # Keep another tab so this tests tabs.onRemoved, not browser exit.
                        firefox.call('WebDriver:NewWindow', type='tab')
                        firefox.call('WebDriver:CloseWindow')
                    else:
                        firefox.script("location.hash = 'stop'; return true;")
                    wait_for(lambda: receivers[0].poll() is not None, seconds=10)
                    output, errors = receivers[0].communicate(timeout=2)
                    assert receivers[0].returncode == 0, (output, errors)
                    print(('Tab close: ' if close_tab else 'Explicit Stop: ') + output.strip(), flush=True)
            except Exception:
                print({'diagnostics': diagnostics[-5:], 'requests': requests, 'native_exit': native.poll()}, flush=True)
                for receiver in receivers:
                    if receiver.poll() is not None:
                        print(receiver.communicate(), flush=True)
                raise
            finally:
                if connection:
                    connection.close()
                for process in reversed(processes):
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                done.set()
                worker.join(timeout=2)
                server.shutdown()
                server.server_close()
                for process in processes:
                    for stream in [process.stdin, process.stdout, process.stderr]:
                        if stream and not stream.closed:
                            stream.close()


if __name__ == '__main__':
    run()
    run(close_tab=True)
