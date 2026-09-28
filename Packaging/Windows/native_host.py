"""Self-contained browser host installed beside Rendepth.exe on Windows.

Freeze this entry point with PyInstaller for the release installer. Firefox and
Chrome both start the same executable; neither needs Python on the user's PC.
"""

from pathlib import Path
import sys

import host


def main():
    rendepth = Path(sys.executable).resolve().with_name("Rendepth.exe")
    try:
        if not rendepth.is_file():
            raise ValueError("The Rendepth application is missing")
        host.run(rendepth)
    except (ValueError, OSError, EOFError) as error:
        host.send({"error": str(error)})


if __name__ == "__main__":
    main()
