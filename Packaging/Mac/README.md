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
cmake -S . -B cmake-build-mac-export -DRENDEPTH_MAC_EXPORT_BUNDLE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-mac-export --target Rendepth -j28

python3.13 -m venv /private/tmp/rendepth-host-venv
/private/tmp/rendepth-host-venv/bin/pip install pyinstaller==6.22.3
PYINSTALLER_CONFIG_DIR=/private/tmp/rendepth-pyinstaller-cache \
  /private/tmp/rendepth-host-venv/bin/pyinstaller --noconfirm --clean \
  --onefile --name RendepthNativeHost \
  --codesign-identity 'Developer ID Application: Outmode LLC (PZBA2JJ2RQ)' \
  --paths Browser/Firefox/native \
  --distpath /private/tmp/rendepth-host-dist \
  --workpath /private/tmp/rendepth-host-build \
  --specpath /private/tmp/rendepth-host-build Packaging/Mac/native_host.py

python3 Packaging/Mac/BuildInstaller.py \
  --bundle cmake-build-mac-export/output/Rendepth.app \
  --native-host /private/tmp/rendepth-host-dist/RendepthNativeHost \
  --chrome-extension-id hffdjljngfgobaekdbgfgfodecmehgbh \
  --application-identity 'Developer ID Application: Outmode LLC (PZBA2JJ2RQ)' \
  --installer-identity 'Developer ID Installer: Outmode LLC (PZBA2JJ2RQ)' \
  --output /private/tmp/Rendepth-3.0.0.pkg

pkgutil --check-signature /private/tmp/Rendepth-3.0.0.pkg
xcrun notarytool submit /private/tmp/Rendepth-3.0.0.pkg \
  --keychain-profile rendepth-notary --wait
xcrun stapler staple /private/tmp/Rendepth-3.0.0.pkg
xcrun stapler validate /private/tmp/Rendepth-3.0.0.pkg
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

The builder reads the package version from the app's `Info.plist`, embeds the
GStreamer runtime and plugins, fixes their library paths, signs nested code and
the app, then creates native messaging manifests. It currently defaults to
Homebrew's `/opt/homebrew` paths; use `--plugin-directory` and
`--plugin-scanner` for another GStreamer installation. Keep the packaged
third-party notices in `Contents/Legal` current with the actual libraries in
the release.
