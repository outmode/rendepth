# Model ZIP checksums

Upload the ONNX and Core ML `*.zip.sha256` files in this directory to
`https://rendepth.com/models/`, beside the ZIPs with matching names. Each file
contains the lowercase SHA-256 of the exact compressed ZIP bytes, two spaces,
the ZIP basename, and a trailing newline, matching the GPU pack checksum format.

`Source/ModelDownloader.cpp` pins both each ZIP hash and its extracted ONNX hash.
Changing a server ZIP requires regenerating its sidecar and shipping updated
pins in a new app release. Keep the published ZIP bytes stable for existing app
releases. The app downloads the sidecar first and will reject a missing or
mismatched sidecar, so publish these files before distributing the updated app.

Compiled Core ML models use separate `-coreml.zip` names so they cannot replace
the existing ONNX archives. Run `Tools/package_coreml_models.py` to create the
seven archives and matching SHA-256 sidecars in `Distribution/CoreMLModels/`.
Keep a copy of each sidecar here with the pins used by the app.
Each archive contains one top-level `.mlmodelc` directory. The macOS app pins
each archive SHA-256 and compiled-directory SHA-256 in `ModelDownloader.cpp`,
downloads the archive on first Core ML use, and caches it in the configured
Models folder. Upload the ZIP and sidecar together and keep their bytes stable
for app releases using those pins.
