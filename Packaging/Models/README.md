# Model ZIP checksums

Upload the seven `*.zip.sha256` files in this directory to
`https://rendepth.com/models/`, beside the ZIPs with matching names. Each file
contains the lowercase SHA-256 of the exact compressed ZIP bytes, two spaces,
the ZIP basename, and a trailing newline, matching the GPU pack checksum format.

`Source/ModelDownloader.cpp` pins both each ZIP hash and its extracted ONNX hash.
Changing a server ZIP requires regenerating its sidecar and shipping updated
pins in a new app release. Keep the published ZIP bytes stable for existing app
releases. The app downloads the sidecar first and will reject a missing or
mismatched sidecar, so publish these files before distributing the updated app.
