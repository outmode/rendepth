#!/usr/bin/env python3
"""Register Rendepth's shared native host for Chrome on Windows or Linux."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import sys


def extension_id():
    manifest = json.loads((Path(__file__).resolve().parents[1] / 'extension/manifest.json').read_text())
    digest = hashlib.sha256(base64.b64decode(manifest['key'])).hexdigest()[:32]
    return ''.join(chr(ord('a') + int(char, 16)) for char in digest)


def install(executable, directory, identity, launcher=None, register=False):
    if not re.fullmatch('[a-p]{32}', identity):
        raise ValueError('Chrome extension ID must contain 32 letters from a to p')
    executable = executable.resolve(strict=True)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError('Rendepth executable is not executable')
    # Reuse the tested protocol, lifecycle and window-reuse implementation.
    host = Path(__file__).resolve().parents[2] / 'Firefox/native/host.py'
    host = host.resolve(strict=True)
    directory.mkdir(parents=True, exist_ok=True)
    if os.name == 'nt':
        if launcher is None:
            raise ValueError('Windows registration requires --launcher ChromeNativeHost.exe')
        source = launcher.resolve(strict=True)
        if not source.is_file() or source.suffix.lower() != '.exe':
            raise ValueError('--launcher must point to ChromeNativeHost.exe')
        launcher = (directory / 'ChromeNativeHost.exe').resolve()
        if source != launcher:
            shutil.copy2(source, launcher)
        (directory / 'rendepth-chrome-host.conf').write_text(
            '\n'.join((sys.executable, str(host), str(executable))) + '\n', encoding='utf-8')
    else:
        launcher = (directory / 'rendepth-chrome-host').resolve()
        launcher.write_text('#!/bin/sh\nexec ' + ' '.join(shlex.quote(value) for value in
            (sys.executable, '-u', str(host), '--rendepth', str(executable))) + ' "$@"\n')
        launcher.chmod(0o700)
    manifest = directory / 'com.outmode.rendepth.json'
    manifest.write_text(json.dumps({'name': 'com.outmode.rendepth',
        'description': 'Rendepth Chrome photo and video bridge', 'path': str(launcher),
        'type': 'stdio', 'allowed_origins': [f'chrome-extension://{identity}/']}, indent=2) + '\n')
    if register and os.name == 'nt':
        import winreg
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER,
                r'Software\Google\Chrome\NativeMessagingHosts\com.outmode.rendepth') as key:
            winreg.SetValueEx(key, '', 0, winreg.REG_SZ, str(manifest.resolve()))
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rendepth', required=True, type=Path)
    parser.add_argument('--browser', choices=['google-chrome', 'chromium', 'google-chrome-for-testing'], default='google-chrome')
    parser.add_argument('--directory', type=Path, help='Override the native-host registration directory')
    parser.add_argument('--launcher', type=Path, help='Windows ChromeNativeHost.exe built from this checkout')
    parser.add_argument('--extension-id', default=extension_id(), help='Override for a separately packaged/store extension')
    args = parser.parse_args()
    if os.name == 'nt':
        if args.browser != 'google-chrome':
            parser.error('Windows registration currently supports Google Chrome only')
        directory = args.directory or Path(os.environ['LOCALAPPDATA']) / 'Rendepth' / 'Chrome'
    elif sys.platform.startswith('linux'):
        directory = args.directory or Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / args.browser / 'NativeMessagingHosts'
    else:
        parser.error('The Rendepth browser bridge currently supports Windows and Linux only')
    try:
        manifest = install(args.rendepth, directory, args.extension_id, args.launcher, register=True)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(f'Registered {manifest}\nExtension ID: {args.extension_id}')
