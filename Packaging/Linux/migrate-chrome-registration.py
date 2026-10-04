#!/usr/bin/env python3
"""Remove only old development registrations that hide the packaged Chrome host."""

import json
from pathlib import Path
import pwd


DEVELOPMENT_ORIGIN = "chrome-extension://ocmhnmfbdiamechohheannkbhdpbnjgl/"
RELATIVE_MANIFEST = Path(".config/google-chrome/NativeMessagingHosts/com.outmode.rendepth.json")


def remove_development_registration(home):
    manifest = home / RELATIVE_MANIFEST
    try:
        data = json.loads(manifest.read_text())
        if (isinstance(data, dict) and data.get("name") == "com.outmode.rendepth" and
                data.get("allowed_origins") == [DEVELOPMENT_ORIGIN]):
            manifest.unlink()
            print(f"Removed obsolete Rendepth Chrome registration: {manifest}")
            return True
    except FileNotFoundError:
        pass
    except (OSError, ValueError, TypeError, UnicodeError) as error:
        print(f"Could not check Rendepth Chrome registration {manifest}: {error}")
    return False


if __name__ == "__main__":
    for home in {Path(account.pw_dir) for account in pwd.getpwall() if account.pw_dir}:
        remove_development_registration(home)
