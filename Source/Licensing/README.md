# Rendepth Pro licensing

The options menu opens a native license window under the app version line:
GTK 3 on Linux, Win32 controls on Windows, and Cocoa on macOS. Activation is
optional. Free users can convert still images/photos and display native stereo
photos and videos (including native stereo browser streams). Mono browser streams
also play without activation, with an upgrade message and no inferred depth. Video depth
conversion, mono browser video conversion, and Blu-ray playback require Pro.
DVD and audio playback remain available without activation. Locked actions show
"Upgrade to Pro to Unlock Feature" using the centered in-app message. The license
window remains available from the options menu. Deactivation stops active conversion
and Blu-ray playback, including an open Blu-ray title menu. Browser video keeps
playing flat when its depth conversion stops. There is no first-run
activation requirement. The version line only displays
"Pro License" when a complete local activation record exists.

## Seller setup

Configure the Rendepth Pro product in Lemon Squeezy for a $49 perpetual license,
license-key generation, and a limit of 5 computers. Use Test Mode for the full
purchase/activation/deactivation acceptance test before releasing.

The owner-supplied store ID `474981` and product ID `1373415` are the build
defaults. They still need confirmation against a test activation. For existing
build directories with cached values, explicitly supply these **public** settings
(the checkout URL below was supplied for Test Mode):

```sh
cmake -S . -B <build-directory> \
  -DRENDEPTH_LICENSE_STORE_ID=474981 \
  -DRENDEPTH_LICENSE_PRODUCT_ID=1373415 \
  -DRENDEPTH_LICENSE_VARIANT_ID=0 \
  -DRENDEPTH_LICENSE_PURCHASE_URL=https://shop.rendepth.com/checkout/buy/dea78604-79fe-4ef0-b9b8-b57d6017a9c3 \
  -DRENDEPTH_LICENSE_SUPPORT_URL=mailto:support@outmode.com
cmake --build <build-directory> --parallel 14
```

Store and product IDs must be positive. Variant ID is optional: 0 accepts all
variants of the configured product, so specify it when the product has multiple
editions. Setting either store or product ID to 0 disables activation. Empty URLs disable the corresponding
purchase/support buttons. Support defaults to `mailto:support@outmode.com`,
which opens the customer's email application. A support HTTPS page can be
configured later. The native dialog remains accessible for inspection.
No seller API key or webhook secret belongs in these settings or the binary.

Use the identifiers belonging to the actual test product during development,
then rebuild using the live product identifiers for release. Never ship a build
configured to accept a test product as the production build.

## Behavior and persistence

`LicenseManager` owns policy, `LemonSqueezyClient` owns provider HTTP/JSON, and
`LicenseStorage` owns the record. `LicenseService` performs explicit requests
on a worker thread; native widgets are serviced on SDL's main thread. Closing
and reopening the window does not start another request. App shutdown waits
for an in-flight operation to finish so it can persist a successful activation.

Requests use HTTPS, certificate verification, bounded response sizes, timeouts,
form encoding, and no redirects. Response bodies and keys are not logged. The
store/product/optional variant, returned key, active status, perpetual expiry,
and activation instance are checked before activation is accepted. A successful
HTTP status alone is insufficient. There are no automatic request retries.

The record lives in `License.json` in `SDL_GetPrefPath("Outmode", "Rendepth")`,
separately from preferences and executable files. Writes use a per-record
cross-process lock and atomic replacement. POSIX records are created with mode
0600; Windows uses the per-user application-data directory's permissions. The
record contains the full key for voluntary deactivation and should be treated
as private. There is no hardware identifier or machine fingerprint.

Normal startup only reads the local record. It makes **zero licensing network
requests**, does not expire records, and does not compare an existing activation
to a future build's configured product IDs. App updates/reinstallation preserve
activation if the per-user data directory is preserved. OS reinstallation or
removing that directory can require activation recovery through support.

A repeated activation on an already licensed installation consumes no slot.
Storage is checked before activation. If persistence subsequently fails, the
manager attempts to release the new instance and reports whether support is
needed. Wrong-product activations are rejected and their newly created instance
is released when the response identifies it safely.

Deactivation contacts Lemon Squeezy only after an explicit confirmation. Failed
requests retain the local record. After confirmed success, an inactive marker
is atomically written before deleting the record; a failed deletion cannot
revive the activation. If even writing that marker fails, the retained record
and error are exposed to the user for recovery.

An interrupted request can have an uncertain server outcome. The UI directs
customers to support instead of automatically retrying and consuming more slots.
Lost/dead/reformatted computers are handled through seller support; there is no
Rendepth backend, account system, or periodic validation service.

## Verification

```sh
cmake --build <build-directory> --target LicensingTest --parallel 14
./Binary/LicensingTest
# Linux with a desktop session:
cmake --build <build-directory> --target LicenseDialogTest --parallel 14
./Binary/LicenseDialogTest
```

Core tests inject a fake provider transport: valid/invalid activation, wrong
store/product/variant, HTTP failures and malformed replies, private storage,
no duplicate activation, offline trust of old records, changed configuration,
corrupt/missing records, deactivation success/failure, save failure rollback,
and simulated fifth/sixth-computer/support-reset behavior. The native GTK test
checks entry, busy controls, licensed state, deactivation confirmation, closing,
and reopening. These tests do not consume real activation slots.

Release checks still require real Lemon Squeezy Test Mode purchase/activation,
offline relaunch, deactivation and recovery, plus Windows/macOS build and native
UI checks. Test the real fifth/sixth-computer limit on the seller-configured
product. Pro feature policy must be decided before enforcing restrictions.

API references:
- https://docs.lemonsqueezy.com/api/license-api
- https://docs.lemonsqueezy.com/api/license-api/activate-license-key
- https://docs.lemonsqueezy.com/api/license-api/deactivate-license-key
