# Rendepth Companion store submission

Build the upload archives from the repository root:

```powershell
py -3 Packaging/Browser/Build-Addons.py
```

The outputs are in `Distribution/3.0/Browser`. Each ZIP has `manifest.json` at
its root. The Chrome upload omits the public `key` used only to give unpacked
development installs a stable ID. Firefox's add-on ID is already fixed as
`firefox@rendepth.outmode`; AMO signs the uploaded ZIP before it can be installed
permanently.

The Chrome ZIP has been uploaded as a draft in the
[Chrome Web Store Developer Dashboard](https://chrome.google.com/webstore/devconsole/).
Its **Item ID** is `hffdjljngfgobaekdbgfgfodecmehgbh`; public
publication is not needed to learn it. Give that 32-letter ID to the desktop
installer with `-ChromeExtensionId` and to the macOS package with
`--chrome-extension-id`. Do not use the unpacked development ID
`ocmhnmfbdiamechohheannkbhdpbnjgl` for public installers. Once the dashboard
has assigned its public key, replace the local manifest `key` with it so an
unpacked copy can use the store ID during testing. Rebuild the upload ZIP after
any source change.

Upload the Firefox ZIP through the
[AMO developer hub](https://addons.mozilla.org/developers/). Select a listed
add-on if it should be publicly searchable. Download the signed XPI from AMO
for an installation test. The uploaded ZIP is not itself a signed XPI.

Suggested store name: **Rendepth Companion**

Suggested short description: **Send web photos and videos to Rendepth for 3D
viewing. Requires the Rendepth desktop app.**

Suggested full description:

> Open a web image in Rendepth from its right-click menu, or send the playing
> video from the toolbar popup. Choose 2D, side-by-side half, or side-by-side
> full format. Video playback and audio remain in the browser while the desktop
> app displays the image or video in 3D. The desktop app must be installed.
> Live depth conversion of ordinary 2D video requires Rendepth Pro; stereo
> source viewing and still-photo conversion are available without Pro.

Permission explanations for the store dashboards:

| Permission | Reason |
| --- | --- |
| `activeTab` and Chrome `scripting` | Read the video the user selected and run capture code in that tab. |
| `contextMenus` / Firefox `menus` | Add the user-invoked image and video commands. |
| `nativeMessaging` | Connect to the locally installed Rendepth desktop app. |
| `storage` | Remember format, quality, and site reconnect preferences locally. |
| Optional HTTP/HTTPS host access | Read a selected image or reconnect video on a site after the user opts in. |

The extension handles user-selected web media locally and makes browser requests
to fetch selected images. The Firefox manifest declares `websiteContent` because
selected media crosses from Firefox into the local desktop app. Chrome's Privacy
practices tab also requires a data handling disclosure when processing stays on
the device. Provide the public
Rendepth privacy-policy URL and truthful dashboard declarations. The Chrome
listing also needs a store icon and at least one screenshot of actual use; use
the dashboard's current asset specifications. Neither store upload nor review
is automated by this repository.
