# Rendepth Companion for Firefox

Rendepth Companion sends web photos and videos to the Rendepth desktop app for
3D viewing. It requires the desktop app; the add-on is not a standalone viewer.

## Version compatibility

The add-on starts at **3.0**, matching the desktop app's **3.0** release family.
Keep major/minor versions aligned; patch versions can advance independently.
For example, Companion **3.0.2** and desktop **3.0.4** belong to the same compatible
release family. Browser/platform minimum versions are separate requirements.
This is the release-version convention, not a runtime version-enforcement check.

## Web photos (Free and Pro)

Right-click an image and choose **Open in Rendepth → 2D Photo**, **Cross-Eye**, or **Parallel**.
2D Photo opens the image and starts the existing still-photo depth conversion,
including on Free. Parallel interprets full-width SBS as left/right (L/R).
Cross-Eye interprets full-width SBS as right/left (R/L). Both preserve the source
pixels and skip depth inference.
Rendepth's existing display and
eye-swap controls apply.

When you open an HTTP/HTTPS image, the extension requests Firefox's permission
for the image host before reading or fetching it. Firefox shows its standard
prompt if access has not already been granted. Declining cancels the image
opening. This permission covers the host, not just one image. Blob and data
images do not request host access. There is no separate permission confirmation
in the extension popup. The extension uses the selected image URL and
transfers a lossless PNG at its decoded dimensions. There is no video capture
or VP8 compression. Rendepth keeps its own temporary copy until app exit, so
closing the page or browser does not remove the photo. Opening a photo replaces
the active video capture. Existing native-host registration remains valid;
restart Rendepth and reload the temporary extension after building this version.

Limits: 32 MiB downloaded/PNG data, 32 megapixels, and 16384 pixels per side.
Both stereo options require an even width. Animated images become a still image. Blob
images are snapshotted from the selected page element. Sites that reject image
fetches or require unavailable authentication may require saving the image
manually. No arbitrary URL or destination path is passed to the native host.

Image checks:
```sh
node Browser/Firefox/test_images.mjs
python3 Browser/Firefox/test_images_browser.py
python3 -m unittest discover -s Browser/Firefox/native -p 'test_host.py'
cmake --build cmake-build-debug --target BrowserBridgeTest --parallel 14
Debug/BrowserBridgeTest
```

## Live video

This Linux development extension sends **only the active video's track** to a
running Rendepth window, launching the app if none is available. It uses a direct
**WebRTC/VP8 video stream**, with a **60 fps ceiling**, instead of JPEG snapshots.
Choose **2D Video** (the first/default option) to run ordinary video through
Rendepth’s standard live video depth model and 3D renderer automatically. The
existing Rendepth model-quality and 3D display controls apply. The format menu
offers **2D Video**, **SBS Half**, and **SBS Full**. Stereo modes preserve source
stereo and skip depth inference, with the left eye first. Eye swapping is controlled in Rendepth. Playback and audio stay in Firefox.
Without Pro, **2D Video** streams as flat video and shows the upgrade message;
depth conversion requires Pro. Deactivating Pro stops depth conversion while
keeping the browser video connected.

## Try it

1. Install GStreamer's WebRTC, VP8 and libnice plugins. On Fedora:
   ```sh
   sudo dnf install gstreamer1-plugins-good gstreamer1-plugins-bad-free libnice-gstreamer1
   ```
   On Debian/Ubuntu:
   ```sh
   sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-nice
   ```
   Building also needs the development packages providing `gstreamer-webrtc-1.0`
   (1.22 or newer), `gstreamer-sdp-1.0` and `gstreamer-video-1.0`.
2. Build the app:
   ```sh
   cmake --build cmake-build-debug --target Rendepth --parallel 12
   ```
3. Register the native host for your Linux user, if not already registered:
   ```sh
   python3 Browser/Firefox/native/install.py --rendepth "$PWD/Debug/Rendepth"
   ```
   This creates two files in `~/.mozilla/native-messaging-hosts/`. Register again
   if you move the checkout or change the executable path.
4. Restart Rendepth after building. In Firefox 138 or newer, open
   `about:debugging#/runtime/this-firefox`, choose **Load Temporary Add-on**, and
   select `Browser/Firefox/extension/manifest.json`. For an already loaded add-on,
   use **Reload**, then reload the video page. The old JPEG extension and receiver
   are not compatible with the new signalling protocol.
5. Play a video. Click the extension toolbar button, select **2D Video**,
   **SBS Half**, or **SBS Full**, then **Open Playing Video**.
   Normal capture needs no site-access prompt. Optionally enable **Automatically
   reconnect on this site** in the popup to request access for automatic capture
   after navigating to another page on that site.
   The toolbar selects the largest playing video in the main document. To choose
   a specific video, use its right-click **Open in Rendepth** submenu. On sites
   with custom video menus, Shift + right-click can expose Firefox's menu.
   Player overlays, such as Vimeo's, resolve to the visible video in the nearest
   player container. If the target was removed, capture falls back to the visible
   playing video in the same document.
6. Select your usual stereo output in Rendepth. Stop from the extension popup or
   the capture button in Rendepth. Stopping clears capture and leaves the window
   open; closing the window also ends the browser connection.

Stopping or losing the browser connection releases the last displayed frame and
returns Rendepth to its empty background.
Closing capture also closes secondary light-field output windows; the main
Rendepth window stays open. A native-host crash is detected after
about five seconds without its heartbeat; a disconnected WebRTC transport gets
a five-second recovery window. Pausing, buffering and waiting for the next video
on the same page keep the connection and last picture.

The existing native-host registration remains valid. Version 0.2.6 adds the
`storage` permission to remember the optional source-quality limit locally. Temporary add-ons disappear when Firefox restarts; this has
not been signed or published. The installer targets conventional Linux Firefox;
Flatpak/sandboxed native-host discovery needs separate testing.

To remove registration, delete only `com.outmode.rendepth.json` and
`rendepth-firefox-host` from `~/.mozilla/native-messaging-hosts/`.

## Performance and limits

- Source-paced video up to **60 fps**, within **3840×1080**, with an encoder bitrate
  requested ceiling of **16 Mbit/s**. Firefox's resolution-based encoder limit
  still constrained the tested 1080p stream to about **5 Mbit/s**, so the requested
  ceiling must not be read as achieved bitrate or a quality guarantee. A 24 fps source
  remains 24 fps. The sender now prefers preserving resolution; Firefox may
  reduce frame rate to fit encoder/transport capacity. Hardware encoding is not guaranteed.
- The receiver preserves Firefox's offered transport-wide sequence extension so
  bandwidth feedback works. Previously the answer advertised `transport-cc` but
  omitted its `extmap`; a detailed 1080p60 fixture averaged about 15 decoded fps.
  With feedback negotiated, the same fixture reached 60 decoded fps. A longer
  run settled near 5 Mbit/s, which can still produce visible VP8 artifacts at
  1080p60. Raising `maxBitrate` alone does not override Firefox's internal limit.
- The native receiver keeps decoded YUV planes for the renderer, avoiding the
  former JPEG decode and CPU RGBA conversion. The RTP jitter buffer is configured
  for 20 ms; this is **not** a measured total latency. Audio remains in Firefox
  and can lead the picture. Audio/video synchronization is still future work.
- One capture session at a time. An existing window replaces its current media.
  With several windows, the most recently opened available instance is selected.
  A busy/unresponsive app reports an error instead of launching a duplicate.
- Pausing retains the last picture. Ending or removing a video waits for the next
  visible playing video in the same page, retaining the peer and viewer. Leaving
  the document now waits for the next page; stopping capture, closing its tab or
  closing the viewer ends the connection.
  Resets/quality changes of the active video element retain the
  connection and replace its captured track when playback resumes. Start a new
  connection to change input format or select another video.
- Firefox enforces protected/cross-origin media restrictions. The extension does
  not extract media URLs or bypass those restrictions. One public YouTube video has been tested, including quality changes. This is
  not a guarantee for all YouTube videos, ads, protected content or other sites.
- Images, TAB input and automatic stereo detection are not exposed by this extension yet.

## Architecture

`selected video → captureStream (video only) → Firefox WebRTC VP8 encoder →
local encrypted RTP → GStreamer webrtcbin → VP8 decoder → newest YUV frame → GPU`

There is no per-frame JavaScript timer, canvas, base64 conversion, native message,
JPEG file or disk polling for video. Native messaging carries a bounded SDP offer
and answer plus small navigation control messages. Connection descriptions are exchanged through a
private temporary directory; media travels directly between the two local peers.
No external signalling, STUN or TURN service is configured. WebRTC uses temporary
UDP sockets and local host candidates; there is no HTTP/TCP video server.

The extension uses `activeTab`, `menus`, `nativeMessaging`, and `storage`, and injects capture
code after an explicit action, then on same-origin navigation during that session
when automatic reconnection is explicitly enabled and optional site access is granted. The executable path comes from local host
registration, never page content. Each viewer advertises a Unix-domain control
socket in `$XDG_RUNTIME_DIR/rendepth-browser` (or `/tmp/rendepth-browser-<uid>`).
The app acknowledges session acceptance on its main thread, then negotiates and
decodes asynchronously. Stale sockets are skipped; a launch lock avoids duplicate
windows. Removing session files stops the receiver without terminating the app.

The decoder's output queue and the renderer's mailbox each hold only the newest
frame. Slow rendering cannot accumulate an ever-growing queue of decoded frames.
SDP messages are size-bounded, connection setup has timeouts, and missing plugins
or decode/negotiation failures are reported to the extension.

## Checks

```sh
python3 -m unittest discover -s Browser/Firefox/native -v
cmake --build cmake-build-debug --target BrowserCaptureTest BrowserBridgeTest --parallel 12
./Debug/BrowserCaptureTest
./Debug/BrowserBridgeTest
node --check Browser/Firefox/extension/background.js
node --check Browser/Firefox/extension/capture.js
node --check Browser/Firefox/extension/popup.js
node Browser/Firefox/test_capture.mjs
node Browser/Firefox/test_quality.mjs
node Browser/Firefox/test_background.mjs
node Browser/Firefox/test_popup.mjs
python3 Browser/Firefox/test_stream.py
```

`test_stream.py` needs Firefox, FFmpeg with libx264, and the GStreamer runtime
plugins. It creates an isolated Firefox profile and timestamped 1080p SBS files,
then exercises the production capture script and native receiver at 30/60 fps.
It checks decoded throughput (at least 90% of source rate), YUV plane dimensions,
left/right colors, and stop cleanup. The test mocks the extension port; native
host tests separately cover bounded signalling, fragmented input, instance reuse,
launch fallback, busy/stale instances and cleanup. `BrowserBridgeTest` covers
settings, validation, replacement and shutdown; `BrowserCaptureTest` covers
missing/invalid offers, restart and cancellation.

### YouTube findings and resolution trials

A real YouTube quality switch reproduced the original cutoff: the video element
emitted `emptied`, and the extension treated that as the end of capture. The
extension now retains the peer, captures the replacement video track, updates
scaling and uses `replaceTrack`. Brief buffering no longer counts as a capture
restriction while the source lacks playable future frames. The receiver tolerates
brief oversized frames while a scaling change takes effect. Node regressions
cover reset/replacement, scaling, buffering, cancellation and protected replacements.

With Firefox 155, the repaired extension stayed connected for a minute while the
public Blender video `aqz-KE-bpKQ` changed from its initial quality to 4K and then
to 1080p60. Window-render diagnostics measured about **24–25 distinct frames/s**
with the 4K source scaled to 1080p, and about **59–60 frames/s at 1920×1080** after
selecting **1080p60 in YouTube**. The browser was headless and the actual Rendepth
window used X11. These measurements count distinct video frames after GPU window
draw completion, not compositor scanout or end-to-end latency. The temporary
extension was granted access to YouTube for automation; the shipping manifest
still uses `activeTab`.

Version 0.2.2 enables **3840×1080 full SBS**, preserving 1920×1080 pixels per
eye when the source has that resolution. Restart Rendepth, reload the temporary
extension in `about:debugging#/runtime/this-firefox`, and reload the video page.
Choose **SBS Full** for full-width SBS material. This is an experimental resolution
increase, not a 60 fps guarantee: a detailed 3840×1080/60 fixture initially measured
about **26 decoded fps**, with roughly 38 ms per encoded frame in Firefox. A separate
30-second test through the actual extension/native host/viewer displayed
3840×1080 continuously, mostly **35–42 distinct frames/s** after startup. These
are headless Firefox tests with detailed synthetic motion, not a guarantee for
a particular YouTube video.

Version **0.2.3** adds **Expand SBS Half up to 3840×1080**, enabled by default.
For 3840×2160 half-SBS, it preserves all source columns and halves the height,
then signals **SBS Full** to the viewer so eye proportions remain correct.
Continue selecting **SBS Half** in the extension for half-SBS source material;
the conversion to full-SBS is automatic. Lower-resolution half-SBS expands
horizontally within the same bounds (1920×1080 becomes 3840×1080); this does not
create additional source detail.

This path draws decoded video into a canvas on each presented source frame and
captures the canvas as a VP8 WebRTC stream. There are no JPEGs or per-frame native
messages. The canvas track stays attached through YouTube quality changes, and
buffering retains the last picture. Both source-reset recovery and real YouTube
3840×2160 to 3840×1080 output were checked. **The scaling path did not sustain
60 fps in the test environment**, including a normal Firefox window. Canvas
processing adds work, so this option can materially reduce performance.

To return to faster direct capture, uncheck **Expand SBS Half up to 3840×1080**
and click **Open Playing Video** again. With expansion disabled, a 3840×2160
source still scales proportionally to 1920×1080. SBS Full sources continue using
direct capture, preserving their aspect ratio within 3840×1080. Full
3840×2160 output remains outside the current stream limit. The older YouTube
measurements above precede the bandwidth-feedback fix.

Run the higher-resolution throughput check with:

```sh
python3 Browser/Firefox/test_stream.py --width 3840 --fps 60
```

This deliberately reports failure if it cannot sustain 90% of the source rate;
it also verifies the decoded dimensions instead of accepting silent downscaling.

To record the actual viewer-window frame rate and worst gap, launch with:

```sh
RENDEPTH_BROWSER_STATS=1 ./Debug/Rendepth
```

Diagnostics are off by default. The earlier isolated receiver benchmark measured
30.1/60.0 decoded fps from local 30/60 fps files, with Firefox adapting those runs
to 1280×720. That test alone did not establish real-site rendering performance.
Headless canvas animation was slower than its requested frame rate, so the
throughput fixture uses timestamped encoded video files instead.

For a manual visual test, serve this directory with
`python3 -m http.server 8765 --bind 127.0.0.1`, open
`http://127.0.0.1:8765/test-video.html`, and click **Play test video**. Choose
**SBS Full**. The eyes should read LEFT and RIGHT; swap eyes in Rendepth to
reverse them.
This canvas fixture is for visual checks, not frame-rate benchmarking.

References:
- [Firefox native messaging](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Native_messaging)
- [Video element capture](https://developer.mozilla.org/en-US/docs/Web/API/HTMLMediaElement/captureStream)
- [WebRTC encoder parameters](https://developer.mozilla.org/en-US/docs/Web/API/RTCRtpSender/setParameters)
- [Firefox resolution-based bitrate limits](https://searchfox.org/firefox-main/source/dom/media/webrtc/libwebrtcglue/VideoStreamFactory.cpp)
- [GStreamer WebRTC receiver](https://gstreamer.freedesktop.org/documentation/webrtc/)

To check half-SBS scaling correctness independently of the 60 fps target:

```sh
python3 Browser/Firefox/test_stream.py --width 3840 --height 2160 --half --fps 60 --min-fps 1
```

This checks dimensions, stereo ordering and cleanup and reports actual throughput;
the explicit low threshold is a functional check, not a 60 fps performance pass.
Omit `--min-fps 1` to enforce the normal 90% throughput requirement.

### 2D Video conversion (v0.2.4)

Restart Rendepth, reload the extension and reload the video page after updating.
Select **2D Video** in the popup or video context menu. The native receiver keeps
full display frames in YUV and supplies aspect-preserving RGB model inputs up to
392 pixels on the longer side at up to 20 Hz. These enter the existing
`VideoDepthProcessor`, depth smoothing and 3D rendering path. The selected video
model determines inference resolution/rate; display frame rate and depth inference
rate are separate. SBS modes do not prepare these RGB inputs or start the model.

Validation included an actual extension/native-host/viewer session that loaded
DA2-SMALL-336, generated and uploaded depth, then disconnected while leaving the
viewer open. The bridge/native-host tests cover 2D alongside both stereo modes.
To verify receiver throughput and depth-input dimensions/colors:

```sh
python3 Browser/Firefox/test_stream.py --mono --fps 60
```

This receiver test prepares model inputs but does not run inference; the actual
viewer test above separately verified model execution and depth upload.

### Following videos and YouTube Shorts (v0.2.5)

Capture follows the visible playing video as a site replaces or switches players.
YouTube watch-page navigation and Shorts scrolling retain the same WebRTC peer,
format/scaling choices, eye order and native session. Offscreen preloaded videos
are ignored; the current visible player is preferred over small hover previews.
If a player ends or disappears before its replacement loads, direct capture
unhooks its track while the viewer retains the last picture. Scaled SBS keeps its
canvas track and retargets drawing. The popup reports that it is waiting for the
next video. Explicit Stop still releases capture; full document navigation now
uses the waiting state added in v0.2.7.

This follows **in-page navigation**, as used by YouTube's watch links and Shorts.
Version 0.2.7 also supports a full browser reload or navigation that replaces
the entire document; see the navigation lifecycle notes below.

Validation: Node tests cover disconnected players, loading gaps, offscreen Shorts,
source reset, old-listener removal, canvas retargeting and shutdown. Actual YouTube
tests clicked two watch links, entered Shorts and advanced twice while retaining
streaming status. A separate real extension/native-host/viewer test removed the
player, waited two seconds, then resumed on a new element with one WebRTC peer.

### Live color/depth alignment

2D conversion buffers color to cover observed inference latency (up to 250 ms,
128 MiB / 32 queued frames) and selects the nearest completed depth map. The
buffer preserves the incoming color cadence after filling. Live depth updates
are applied immediately, without the additional 50 ms depth crossfade. This
reduces temporal misalignment but adds video delay relative to audio, which
still plays in Firefox. Slow inference can still leave residual mismatch.
SBS capture bypasses this delay.

Video depth is reconstructed at twice the inference dimensions before display
scaling. This softens coarse-grid stair steps; it does not add model detail.
Firefox frames do not supply decoder motion vectors to Rendepth.

`RENDEPTH_BROWSER_STATS=1` also logs mean/maximum absolute color-to-depth timestamp
difference and the color buffer delay. These measure frame pairing, not the
model's accuracy or end-to-end audio/video synchronization.

Buffer cadence, reset, drain and memory bounds can be checked with:

```sh
cmake --build cmake-build-debug --target LiveVideoBufferTest --parallel 12
./Debug/LiveVideoBufferTest
```

Video inference shares the CPU with Firefox playback and WebRTC encoding. Its
ONNX worker pool is limited to at most four threads (one quarter of reported
hardware concurrency, minimum one), with idle spinning disabled. The worker
honors the selected model's depth-update rate and keeps only the newest pending
input. This leaves CPU time for browser playback without lowering the configured
stream resolution or bitrate. The startup log reports the depth CPU budget.

### Diagnosing browser playback stalls

Receiver throughput alone does not establish smooth playback in Firefox. Use a
visible, foreground test window to compare playback alone, direct capture with
no encoder/receiver, streaming, and playback after stopping:

```sh
python3 Browser/Firefox/test_stream.py --mono --width 3840 --height 2160 --fps 30 --windowed --diagnose --min-browser-fps 20
```

The isolated profile leaves the user's Firefox profile unchanged. Reports include
browser video-frame callback rate, dropped frames, maximum callback gap, document
visibility, source dimensions, WebRTC encoding time and native receiver rate.
Callback rate is a presentation proxy, not measured monitor scanout. Keep the
window visible and avoid other performance tests while measuring. The receiver's
normal 90% throughput and exact output-dimension checks still apply; the optional
browser threshold adds a separate failure condition. `--motion` tests a motion
content hint plus `maintain-framerate` without changing extension defaults.
`--capture-script PATH` tests an alternate sender implementation. Width 2560 and
height 1440 are also supported for resolution comparisons.

A local windowed 4K30 H.264 fixture reproduced browser-only stuttering in 2D mode:

| Capture path | Browser callback fps | Receiver fps | Mean encode time |
| --- | ---: | ---: | ---: |
| Direct, existing settings | 2.6 | 25.7 | 4.8 ms |
| Direct, motion experiment | 2.6 | 25.3 | 4.0 ms |
| Temporary canvas variant, scaled before capture | 19.2 | 19.6 | 6.5 ms |

Playback without streaming and with capture alone measured about 24–25 callback
fps. The failing direct-stream run dropped 189 browser frames and had a 1.42 s
callback gap. The motion experiment also reduced output to 1280×720. The canvas
experiment preserved 1920×1080 but reduced receiver throughput, so neither is a
shipping fix. All three failed the normal 30 fps throughput/dimension criteria.
The canvas variant was a temporary extension of the existing canvas path to 2D,
with aspect-preserving scaling instead of half-SBS expansion.

A 2560×1440/30 source using the existing sender measured 23.6 browser callback fps
(24.6 baseline), zero dropped browser frames during streaming and 30.1 receiver
fps at 1920×1080; its normal receiver checks passed. These synthetic measurements
do not establish performance on every site or hardware configuration.
At 1440p60 the problem returned: browser callbacks fell from 24.9 to 5.7 fps,
with 417 dropped frames, while the receiver sustained 56.8 fps and encoding
averaged 3.8 ms. The new browser threshold correctly failed this run even though
the receiver passed its 90% throughput target.
At 1080p60 browser callbacks stayed near baseline (24.2 versus 24.9 fps), with
one dropped frame during streaming, and the receiver sustained 58.8 fps. Both
the receiver checks and the explicit 20 fps browser threshold passed. Selecting
1080p in the source player is therefore a tested workaround on this setup.

The reproduction uses `BrowserCaptureTest`, which prepares small depth inputs but
does not run depth inference or the Rendepth renderer. The evidence isolates a
problem associated with feeding high-resolution captured video into WebRTC; it
does not yet identify the responsible Firefox thread or prove GPU readback is
the cause. Mean encode time excludes other capture/conversion costs. Profiling
Firefox during the visible-window reproduction is the next step before changing
codecs or shipping a different capture path. Lowering the site's source quality
is a workaround to try; lowering only the WebRTC output resolution did not cure
the 4K-source stall in the motion experiment.

### Optional source-quality limit (v0.2.6)

Reload the temporary extension and the video page. In the popup, enable
**Limit source to 1080p**, then choose **Open Playing Video** (or **Refresh Video**).
The choice is off by default and saved locally; right-click capture uses the same
choice. Changing it applies to the next capture, not an already running session.
It can reduce source detail in high-resolution SBS, including full-width SBS.

On YouTube, the extension tries the selected player's quality controls to choose
1080p, or the highest available lower quality. It checks decoded source dimensions
before reporting success, including portrait 1080×1920 videos. Already smaller
sources are left alone. Quality changes and in-page navigation are monitored
while capture is active; rejected or ignored requests receive bounded retries.
Other sites, missing controls and requests that do not lower the source produce
an instruction in the popup to select 1080p or lower in the site's quality menu.
The notice remains separate from connection status, so connecting cannot hide it.

These are website source-quality changes, not additional WebRTC downscaling.
YouTube's player controls are undocumented and can change. The public iframe
API's `setPlaybackQuality` is a no-op, so it is not used. Only the selected video's
containing player on a YouTube hostname is accessed, using Firefox's
`wrappedJSObject`; only primitive quality strings are passed to it. The quality limiter itself requires no extra site permissions, remote scripts
or URL extraction. Navigation in v0.2.7 separately requests optional site access.

Stopping capture stops monitoring. It does not restore the site's previous
quality setting; use the site's quality menu to return to Auto or a higher
quality. Unchecking the option also leaves the current site selection intact.

Validation: a temporary Firefox extension with the production quality helper
changed the public Blender YouTube fixture from 3840×2160 to 1920×1080 and
reported success only after the dimensions changed. Node checks cover opt-in,
unsupported/spoofed hosts, ignored/throwing controls, bounded retries, navigation,
portrait sources, lower-quality fallback, saved preferences, context-menu capture,
notice routing and capture shutdown. This establishes one live YouTube path,
not compatibility with every YouTube layout or site.

References:
- [YouTube quality API changes](https://developers.google.com/youtube/iframe_api_revision_history)
- [Firefox page-script object access](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Sharing_objects_with_page_scripts)

### Keeping the viewer open while browsing (v0.2.7)

Restart the rebuilt Rendepth app, reload the temporary extension, and reload the
video page. From v0.2.8, normal capture never requests site access. After full-page
navigation, the viewer stays open; click **Refresh Video** to reconnect. Refresh
retains the current capture format and can wait for the next video's Play click.

For hands-free resumption on later pages, enable **Automatically reconnect on this
site** in the popup. Its explanation appears before the opt-in, and enabling
this setting requests Firefox's optional site permission. Opening web images can
also request access to their image host. The reconnect choice is saved per
origin and defaults to off, even if site permission was granted by an older build.
From v0.2.9, the persistent background page handles both the permission request
and saving the choice, so closing the popup during the permission prompt cannot
lose the setting.
Declining permission leaves manual capture available. Disabling the option stops
a pending automatic resume and removes that site's optional access; an already
playing capture continues. Navigating to a different origin always waits for an
explicit action instead of automatically capturing that site.

A page unload now reports navigation and releases only the page's WebRTC peer.
The extension background retains the native port and its heartbeat, and the native
receiver stays active with the last displayed picture. Once the new document is
ready and automatic reconnection is enabled, its capture script waits for a visible playing video. It does not force a
paused video to play. Playback starts a new peer within the existing native
session, preserving input format, quality-limit choice and the Rendepth viewer,
including its secondary light-field windows. Automatic page-load resumption looks
for video in the main document; embedded players can be selected again with their
context menu when needed.

**Stop**, closing the capture tab/browser, or closing capture in Rendepth ends the
session and retains the existing cleanup behavior. An unrelated tab closing has
no effect. Navigation may wait indefinitely while the native host remains alive;
a dead host still fails its five-second heartbeat check. The five-second transport
failure grace period also covers the brief race between document teardown and the
navigation message arriving. Separate offer/answer generations and request IDs
prevent late old-page signaling from being applied to a new peer.

The receiver remains active across replacement, so `Main.cpp` does not enter its
capture-stop path or recreate the viewer. The bridge is attached only once. The
native-host registration continues to use the same host script path; reinstallation
is unnecessary when the checkout/executable paths have not changed.

Validation includes real Firefox navigating between local video pages, a paused
next video held for seven seconds (longer than the previous disconnect timeout),
manual Refresh without auto-injection, opt-in automatic resumption, new-peer
frame decoding, a single native bridge attachment, explicit Stop, and
closing the capture tab while another tab stays open. This tests the production
extension, host and receiver; a localhost signaling adapter in the temporary test
extension avoids modifying native-host registration. It does not run the renderer
or verify physical light-field output. Run it with:

```sh
cmake --build cmake-build-debug --target BrowserCaptureTest --parallel 14
python3 Browser/Firefox/test_navigation.py
```

## Capture selection during ads

Capture reads explicit ad-state classes/attributes on the selected video and its
ancestors (including `ad-showing`, `vjs-ad-playing`, `jw-flag-ads`,
`data-ad-state="playing"`/`"active"`, and `data-is-ad="true"`). A separate
`video-overlay` is excluded only when paired with a `.video-bg-pic video` content
player. There is no advertising-domain list or classification based on CDN/CORS.

When the selected player signals an ad, Rendepth retains its last frame while
capture reconnects/checks readiness every three seconds. This handling does not
pause, seek, mute, reload, remove, or skip anything in the website's player.
Stop, tab closure, or leaving the capture's origin cancels recovery. CORS
failures also use recovery, without treating them as proof of advertising.

Detection is best-effort: unmarked ads and ads stitched into a stream without
exposed player state cannot reliably be distinguished from the main video.

Player-state reference: [Video.js ad-break state](https://github.com/videojs/videojs-contrib-ads/blob/main/src/adBreak.js).
