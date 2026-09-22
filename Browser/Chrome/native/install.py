#!/usr/bin/env python3
"""Register Rendepth's shared native host for Chrome or Chromium on Linux."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import sys


def extension_id():
    manifest = json.loads((Path(__file__).resolve().parents[1] / 'extension/manifest.json').read_text())
    digest = hashlib.sha256(base64.b64decode(manifest['key'])).hexdigest()[:32]
    return ''.join(chr(ord('a') + int(char, 16)) for char in digest)


def install(executable, directory, identity):
    if not re.fullmatch('[a-p]{32}', identity):
        raise ValueError('Chrome extension ID must contain 32 letters from a to p')
    executable = executable.resolve(strict=True)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError('Rendepth executable is not executable')
    # Reuse the tested protocol, lifecycle and window-reuse implementation.
    host = Path(__file__).resolve().parents[2] / 'Firefox/native/host.py'
    host = host.resolve(strict=True)
    directory.mkdir(parents=True, exist_ok=True)
    launcher = (directory / 'rendepth-chrome-host').resolve()
    launcher.write_text('#!/bin/sh\nexec ' + ' '.join(shlex.quote(value) for value in
        (sys.executable, '-u', str(host), '--rendepth', str(executable))) + ' "$@"\n')
    launcher.chmod(0o700)
    manifest = directory / 'com.outmode.rendepth.json'
    manifest.write_text(json.dumps({'name': 'com.outmode.rendepth',
        'description': 'Rendepth Chrome photo and video bridge', 'path': str(launcher),
        'type': 'stdio', 'allowed_origins': [f'chrome-extension://{identity}/']}, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rendepth', required=True, type=Path)
    parser.add_argument('--browser', choices=['google-chrome', 'chromium', 'google-chrome-for-testing'], default='google-chrome')
    parser.add_argument('--directory', type=Path, help='Override the native-host registration directory')
    parser.add_argument('--extension-id', default=extension_id(), help='Override for a separately packaged/store extension')
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('The Rendepth browser bridge currently supports Linux only')
    directory = args.directory or Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / args.browser / 'NativeMessagingHosts'
    try:
        manifest = install(args.rendepth, directory, args.extension_id)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(f'Registered {manifest}\nExtension ID: {args.extension_id}')
