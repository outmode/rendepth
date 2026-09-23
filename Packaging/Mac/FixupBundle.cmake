# Copy linked SDK libraries (including libbluray/libdvdread and their
# dependencies) into the bundle and rewrite their install names for relocation.
include(BundleUtilities)
fixup_bundle("${RENDEPTH_BUNDLE}" "" "")
