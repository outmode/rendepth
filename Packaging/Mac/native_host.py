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
    rendepth = executable.parents[1] / "MacOS" / "Rendepth"
    try:
        if not rendepth.is_file() or not os.access(rendepth, os.X_OK):
            raise ValueError("The Rendepth application is missing or not executable")
        host.run(rendepth)
    except (ValueError, OSError, EOFError) as error:
        host.send({"error": str(error)})


if __name__ == "__main__":
    main()
