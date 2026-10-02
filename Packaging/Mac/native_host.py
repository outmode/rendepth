"""Standalone browser native host embedded in Rendepth.app.

PyInstaller freezes this entry point with Browser/Firefox/native/host.py. The
host executable lives in Contents/Helpers, so its app path
does not depend on a developer checkout or an installed Python interpreter.
"""

import os
from pathlib import Path
import sys

import host


def main():
    executable = Path(sys.executable).resolve()
    try:
        # The onedir host is a nested app in Rendepth.app/Contents/Helpers.
        # Keep Python.framework inside that signed app instead of extracting it
        # to a fresh, unnotarized temporary directory on every browser request.
        if (executable.parent.name != "MacOS" or
                executable.parents[1].name != "Contents" or
                executable.parents[2].name != "RendepthNativeHost.app" or
                executable.parents[3].name != "Helpers" or
                executable.parents[4].name != "Contents" or
                executable.parents[5].name != "Rendepth.app"):
            raise ValueError("The Rendepth browser host is outside its application bundle")
        rendepth = executable.parents[4] / "MacOS" / "Rendepth"
        if not rendepth.is_file() or not os.access(rendepth, os.X_OK):
            raise ValueError("The Rendepth application is missing or not executable")
        host.run(rendepth)
    except (ValueError, OSError, EOFError) as error:
        host.send({"error": str(error)})


if __name__ == "__main__":
    main()
