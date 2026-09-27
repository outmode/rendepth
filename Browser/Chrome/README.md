# Rendepth Companion for Chrome

Rendepth Companion sends web photos and videos to the Rendepth desktop app for
3D viewing. It requires the desktop app; the add-on is not a standalone viewer.

## Version compatibility

The add-on starts at **3.0**, matching the desktop app's **3.0** release family.
Keep major/minor versions aligned; patch versions can advance independently.
For example, Companion **3.0.2** and desktop **3.0.4** belong to the same compatible
release family. Browser/platform minimum versions are separate requirements.
This is the release-version convention, not a runtime version-enforcement check.

Manifest V3 port of the Firefox extension, with the same popup layout, font,
icons, controls, remembered preferences and native photo/video protocol.
The native viewer/host supports Windows, Linux and macOS. No Chrome Web Store upload
or signing is required for local testing.

## Install locally

1. On Windows, build Rendepth and its Chrome launcher from a Visual Studio
   developer shell (or the configured CLion toolchain):
   ```powershell
   cmake --build cmake-build-debug --target Rendepth ChromeNativeHost --parallel 10
   ```
   On Linux, build Rendepth:
   ```sh
   cmake --build cmake-build-debug --target Rendepth --parallel 10
   ```
   On macOS, use the same build command. Browser video also requires
   GStreamer 1.22 or newer and the libnice plugin. Install them with
   `brew install gstreamer libnice-gstreamer`, then configure with
   `-DPKG_CONFIG_EXECUTABLE=/opt/homebrew/bin/pkg-config` and rebuild.
   Photos work without GStreamer.
2. Register Chrome's native host. On Windows, run:
   ```powershell
   py -3 Browser/Chrome/native/install.py --rendepth Debug/Rendepth.exe --launcher Debug/ChromeNativeHost.exe
   ```
   This installs the launcher and manifest under `%LOCALAPPDATA%\Rendepth\Chrome`
   and registers the manifest for the current user in Chrome's native-messaging
   registry key. The registration stores absolute paths, so reinstall after
   moving the checkout or changing the Python interpreter.

   On Linux, run:
   ```sh
   python3 Browser/Chrome/native/install.py --rendepth "$PWD/Debug/Rendepth"
   ```
   For Chromium, add `--browser chromium`. The installer uses
   `~/.config/google-chrome/NativeMessagingHosts` by default (respects
   `XDG_CONFIG_HOME`), independently of Firefox's registration. All platforms
   reuse `Browser/Firefox/native/host.py`; keep both directories in the checkout.
   On macOS, run:
   ```sh
   python3 Browser/Chrome/native/install.py --rendepth "$PWD/Debug/Rendepth"
   ```
   For a bundle, point `--rendepth` at `Rendepth.app/Contents/MacOS/Rendepth`.
   The manifest goes in `~/Library/Application Support/Google/Chrome/NativeMessagingHosts`.
   Use `--browser chromium` or `--browser google-chrome-for-testing` for their
   separate registration directories.
3. Open `chrome://extensions`, enable **Developer mode**, choose **Load
   unpacked**, and select `Browser/Chrome/extension`. Pin Rendepth Companion if desired.
4. Open a page with a playing video or an image. Use the toolbar popup for video,
   or **Open in Rendepth** in the media's right-click menu. An existing Rendepth
   window is reused; otherwise the host launches it.

The manifest's public key gives unpacked installations a stable extension ID:
`ocmhnmfbdiamechohheannkbhdpbnjgl`. The installer derives this ID automatically.
A separately packaged/store extension may need `--extension-id <its-id>`.
The public key is an identity anchor for local loading, not a signing credential.

After updating the extension, use **Reload** in `chrome://extensions` and reload
any previously captured page. Restart Rendepth if its executable changed.
On Windows, rerun the installer after rebuilding `ChromeNativeHost.exe`.

## Features

- **Images:** **2D Photo** converts a still image to depth on Free and Pro;
  **Cross-Eye** interprets full SBS as R/L; **Parallel** interprets it as L/R.
  Stereo photos skip inference. Host access is requested through Chrome's normal
  permission prompt before opening HTTP/HTTPS images. PNG transfer preserves
  decoded dimensions and source pixels. Closing the page or Chrome leaves the
  app's photo open. Limits: 32 MiB download/PNG, 32 megapixels, 16384 pixels per
  side, and even width for stereo. Animated images become stills.
- **Video:** **2D Video**, **SBS Half**, **SBS Full**, including optional half-SBS
  expansion. Playback and audio stay in Chrome; only video is sent over local
  WebRTC/VP8. Same 60 fps ceiling, 3840×1080 bounds and requested 16 Mbit/s ceiling
  as Firefox; actual quality and throughput depend on the browser and source.
  Live 2D depth conversion requires Pro; flat 2D video and source stereo do not.
- **Source quality:** optional **Limit source to 1080p**, with bounded attempts
  on YouTube and a manual-quality notice on unsupported/uncooperative sites.
  Actual video dimensions confirm success.
- **Navigation:** retain the viewer across page changes; **Refresh Video**
  reconnects manually, including waiting for a paused video's Play action.
  **Automatically reconnect on this site** requests optional site access and
  remembers the explicit opt-in. Navigation outside that origin waits for a
  manual action. During an in-page video replacement, temporary cross-origin
  capture failures retain the viewer and retry every second until the
  source becomes capturable. Recovery stops on Stop, tab closure, or navigation
  away from the current origin; it cannot override the site’s media restrictions. Stop and closing the video tab end the capture.

## Chrome-specific implementation

`worker.js` hosts signaling and session state. An active native messaging port
keeps the worker alive during capture and navigation. Idle settings and status
use Chrome storage. Menus are installed on extension installation/update rather
than duplicated on every service-worker startup.

`api.js` adapts Chrome's callback/Promise, action, context-menu and script-injection
APIs. Chrome supplies a media URL instead of Firefox's target-element ID; capture
selects a matching visible media element in the clicked frame. Toolbar capture
selects the largest visible playing video. Identical-URL players are resolved by
visible area. Cross-origin frames may need site access before injection.

Images use `OffscreenCanvas` in the service worker, without an extra hidden tab
or offscreen document. Blob URLs are read in their document. Video capture stays
in the content script. YouTube quality changes use one fixed `MAIN`-world
operation restricted to the active capture tab/frame, an opted-in limiter, and
YouTube hostnames. Page code cannot send arbitrary native commands.

Protected/DRM video and sources Chrome does not expose to `captureStream()`
cannot be captured. Some authenticated/hotlink-protected images need a manual
download. Sites with custom right-click menus may hide Chrome's media menu.
These limitations depend on the site/browser even when Firefox can capture it.

## Validation

```sh
node Browser/Chrome/test_background.mjs
node Browser/Chrome/test_capture.mjs
node Browser/Chrome/test_images.mjs
node Browser/Chrome/test_popup.mjs
node Browser/Chrome/test_quality.mjs
python3 -m unittest discover -s Browser/Chrome/native -p 'test_*.py'
cmake --build cmake-build-debug --target BrowserCaptureTest --parallel 10
```

On Linux, run `node Browser/Chrome/test_runtime.mjs` for the full integration
suite. On Windows, run `node Browser/Chrome/test_windows_runtime.mjs` after
registering the native host. It loads the real extension in an isolated Chrome
profile and checks photo transfer and all four video format/expansion choices
through the registered host and desktop app. Its temporary manifest pre-grants
access only to the local fixture server; it does not alter the normal profile.

The Linux integration test uses an isolated Chrome profile and local fixtures,
the production extension and native host, and Rendepth's WebRTC receiver test binary.
It checks all photo/video formats, exact stereo-photo pixels, automatic recovery
after an 11-second simulated player ad state, and navigation
with a 32-second paused interval before reconnecting to the retained receiver.
Its temporary manifest pre-grants access only to `127.0.0.1`; automated CDP calls
do not simulate the human permission prompt. It does not change your normal
browser profile/registration or test physical stereo display output. It needs
Chrome with `Extensions.loadUnpacked` CDP support, FFmpeg and Python Pillow.

References: [MV3 worker lifetime](https://developer.chrome.com/docs/extensions/develop/concepts/service-workers/lifecycle),
[native messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging),
[script injection](https://developer.chrome.com/docs/extensions/reference/api/scripting).

## Capture selection during ads

Capture reads explicit ad-state classes/attributes on the selected video and its
ancestors (including `ad-showing`, `vjs-ad-playing`, `jw-flag-ads`,
`data-ad-state="playing"`/`"active"`, and `data-is-ad="true"`). A separate
`video-overlay` is excluded only when paired with a `.video-bg-pic video` content
player. There is no advertising-domain list or classification based on CDN/CORS.

When the selected player signals an ad, Rendepth retains its last frame while
capture reconnects/checks readiness every second. This handling does not
pause, seek, mute, reload, remove, or skip anything in the website's player.
Stop, tab closure, or leaving the capture's origin cancels recovery. CORS
failures also use recovery, without treating them as proof of advertising.

Detection is best-effort: unmarked ads and ads stitched into a stream without
exposed player state cannot reliably be distinguished from the main video.

Player-state reference: [Video.js ad-break state](https://github.com/videojs/videojs-contrib-ads/blob/main/src/adBreak.js).
