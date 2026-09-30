#!/usr/bin/env python3
"""Disposable app-side bridge for the Chrome integration test.

The production native host communicates with this private socket. Photos are
decoded with Pillow; videos go through Rendepth's actual WebRTC receiver.
"""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading

from PIL import Image

root = Path(sys.argv[1])
receiver = Path(sys.argv[2])
registry = root / 'runtime/rendepth-browser'
registry.mkdir(parents=True, mode=0o700)
processes = []
lock = threading.Lock()


def emit(value):
    with lock:
        print(json.dumps(value), flush=True)


def collect(process, directory):
    output, error = process.communicate()
    emit({'event': 'video-result', 'code': process.returncode, 'output': output, 'error': error})
    if directory.exists():
        (directory / 'closed').touch()


try:
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as server:
        server.bind(str(registry / 'test.sock'))
        emit({'event': 'ready'})
        while True:
            data, sender = server.recvfrom(4096)
            request = json.loads(data)
            directory = Path(request['directory'])
            emit({'event': 'request', **request})
            if request.get('image'):
                with Image.open(directory / 'image.png') as image:
                    image = image.convert('RGB')
                    emit({'event': 'image', 'size': image.size,
                          'left': image.getpixel((0, 0)), 'right': image.getpixel((image.width - 1, 0))})
            else:
                mode = (root / 'mode').read_text().strip()
                args = [str(receiver), str(directory), 'navigation' if mode == 'navigation' else '2']
                if mode != 'navigation' and request['format'] == '2d':
                    args.append('2d')
                process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                processes.append(process)
                threading.Thread(target=collect, args=(process, directory), daemon=True).start()
            server.sendto(json.dumps({'pid': os.getpid()}).encode(), sender)
except KeyboardInterrupt:
    pass
finally:
    for process in processes:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
