#!/bin/sh
# Sourced by postinstall. A user manifest takes priority over the system-wide
# one, so remove the exact Rendepth registration filename in each browser.

migrate_registration_home() {
    rendepth_home=$1
    for rendepth_browser in 'Mozilla' 'Google/Chrome'; do
        rendepth_manifest="$rendepth_home/Library/Application Support/$rendepth_browser/NativeMessagingHosts/com.outmode.rendepth.json"
        if { [ -e "$rendepth_manifest" ] || [ -L "$rendepth_manifest" ]; } &&
            [ ! -d "$rendepth_manifest" ]; then
            /bin/rm -f "$rendepth_manifest"
            echo "Removed obsolete Rendepth browser registration: $rendepth_manifest"
        fi
    done
}
