# Copy linked SDK libraries (including libbluray/libdvdread and their
# dependencies) into the bundle and rewrite their install names for relocation.
include(BundleUtilities)
file(GLOB RENDEPTH_GSTREAMER_PLUGINS
    "${RENDEPTH_BUNDLE}/Contents/PlugIns/libgst*.dylib")
fixup_bundle("${RENDEPTH_BUNDLE}" "${RENDEPTH_GSTREAMER_PLUGINS}" "")
