# macOS release package

The installer places `Rendepth.app` in `/Applications` and registers its native
host for Chrome and Firefox system-wide. Users install the published browser
extensions from their stores; they do not install Python, GStreamer, or the
extensions in developer mode. Build separately for each supported CPU
architecture until a universal app and native host are available.

Requirements: CMake release build, PyInstaller in a Python 3.13 environment,
GStreamer and the required plugins, a Developer ID Application certificate, a
Developer ID Installer certificate, and Apple notarization credentials. The
extension ID passed below must match the Chrome Web Store **Item ID**, available
as soon as the add-on ZIP is uploaded as a draft.
The Firefox extension must retain `firefox@rendepth.outmode` as its Gecko ID.

```sh
cmake -S . -B cmake-build-mac-export-ninja -G Ninja \
  -DRENDEPTH_MAC_EXPORT_BUNDLE=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DPKG_CONFIG_EXECUTABLE=/opt/homebrew/bin/pkg-config \
  -DRENDEPTH_ONNXRUNTIME_DIR="$PWD/Runtimes/onnxruntime-osx-arm64-1.30.0"
SDKROOT="$(xcrun --show-sdk-path)" \
  cmake --build cmake-build-mac-export-ninja --target Rendepth -j16

python3.13 -m venv /private/tmp/rendepth-host-venv
/private/tmp/rendepth-host-venv/bin/pip install pyinstaller==6.22.3
PYINSTALLER_CONFIG_DIR=/private/tmp/rendepth-pyinstaller-cache \
  /private/tmp/rendepth-host-venv/bin/pyinstaller --noconfirm --clean \
  --onedir --windowed --name RendepthNativeHost \
  --osx-bundle-identifier com.outmode.rendepth.nativehost \
  --codesign-identity 'Developer ID Application: Outmode LLC (PZBA2JJ2RQ)' \
  --paths Browser/Firefox/native \
  --distpath /private/tmp/rendepth-host-release-dist \
  --workpath /private/tmp/rendepth-host-release-build \
  --specpath /private/tmp/rendepth-host-release-build Packaging/Mac/native_host.py

python3 Packaging/Mac/BuildInstaller.py \
  --bundle cmake-build-mac-export-ninja/output/Rendepth.app \
  --native-host /private/tmp/rendepth-host-release-dist/RendepthNativeHost.app \
  --chrome-extension-id hffdjljngfgobaekdbgfgfodecmehgbh \
  --ca-bundle /opt/homebrew/etc/ca-certificates/cert.pem \
  --application-identity 'Developer ID Application: Outmode LLC (PZBA2JJ2RQ)' \
  --installer-identity 'Developer ID Installer: Outmode LLC (PZBA2JJ2RQ)' \
  --signed-bundle-output Distribution/3.0.0/macOS/Rendepth.app \
  --output Distribution/3.0.0/macOS/Rendepth-3.0.0.pkg

pkgutil --check-signature Distribution/3.0.0/macOS/Rendepth-3.0.0.pkg
xcrun notarytool submit Distribution/3.0.0/macOS/Rendepth-3.0.0.pkg \
  --keychain-profile rendepth-notary --wait
xcrun stapler staple Distribution/3.0.0/macOS/Rendepth-3.0.0.pkg
xcrun stapler validate Distribution/3.0.0/macOS/Rendepth-3.0.0.pkg
```

Before the first notarization, create an Apple Account app-specific password and
configure the keychain profile locally (the command prompts for that password):

```sh
xcrun notarytool store-credentials rendepth-notary \
  --apple-id 'YOUR_APPLE_ID_EMAIL' --team-id PZBA2JJ2RQ
```

`xcrun notarytool help store-credentials` also shows the App Store Connect team
API key option. Do not
publish a package until notarization is accepted and staple validation passes.
The package builder deliberately labels output without an Installer identity
as a test package.

The app bundle advertises its supported image, video, disc-image, and audio
extensions to Finder as an alternate viewer. This makes Rendepth available in
**Open With** without replacing the user's default app. Finder passes opened
documents to SDL's file-open event handler.

The builder reads the package version from the app's `Info.plist`, sets the
Installer title to `Rendepth 3.0.0`, and presents the same
`Legal/RENDEPTH_APP_LICENSE` text used by the Windows installer on macOS's
License screen. Installer provides its standard Agree and Disagree controls.
The builder embeds the
GStreamer runtime and plugins, fixes their library paths, signs nested code and
the app, then creates native messaging manifests.
The installer registers the host system-wide for Firefox and Chrome. Its
postinstall script removes per-user `com.outmode.rendepth.json` manifests,
which otherwise take precedence over the packaged registration. It leaves
other applications' manifests and the old, now-unreferenced development
launchers alone.
The browser host is an onedir helper app with its Python framework inside
`Contents/Frameworks`; Firefox starts its executable directly with native
messaging pipes. This avoids extracting Python into a new temporary directory
on every connection. The helper app must be built and signed with the same
Developer ID Application identity as Rendepth. Before publishing, verify the
installed package on a clean Mac by opening a video through Firefox and Chrome.
Run `python3 Packaging/Mac/test_build_installer.py` to check the staged helper
layout and both native messaging manifests.
The builder currently defaults to Homebrew's `/opt/homebrew` paths; use
`--plugin-directory` and
`--plugin-scanner` for another GStreamer installation. The builder also embeds
the PEM CA bundle selected by `--ca-bundle` before signing the app, so HTTPS
model downloads and licensing work without Homebrew on the destination Mac.
Refresh that bundle when preparing a release. Keep the packaged
third-party notices in `Contents/Legal` current with the actual libraries in
the release. The bundle includes usage descriptions for removable and network
volumes, protected media folders, and local network access used by browser video.
These strings explain macOS permission prompts; they do not grant access.
