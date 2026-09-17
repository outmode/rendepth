# Firefox → Rendepth: live 2D and SBS video

This Linux development extension sends **only the active video's track** to a
running Rendepth window, launching the app if none is available. It uses a direct
**WebRTC/VP8 video stream**, with a **60 fps ceiling**, instead of JPEG snapshots.
Choose **2D Video** (the first/default option) to run ordinary video through
Rendepth’s standard live video depth model and 3D renderer automatically. The
existing Rendepth model-quality and 3D display controls apply. Half/full SBS and
swapped eyes are also selectable; those modes preserve source stereo and skip
depth inference. Playback and audio stay in Firefox.

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
5. Play an SBS video. Click the extension toolbar button, select **SBS Half** or
   **SBS Full**, optionally select **Swap eyes**, then **Open Playing Video**.
   The toolbar selects the largest playing video in the main document. To choose
   a specific video, use its right-click **Open in Rendepth** submenu. On sites
   with custom video menus, Shift + right-click can expose Firefox's menu.
6. Select your usual stereo output in Rendepth. Stop from the extension popup or
   the capture button in Rendepth. Stopping clears capture and leaves the window
   open; closing the window also ends the browser connection.

The existing native-host registration remains valid. No extension permission
changes are needed. Temporary add-ons disappear when Firefox restarts; this has
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
  the document, stopping capture or closing the viewer ends the connection.
  Resets/quality changes of the active video element retain the
  connection and replace its captured track when playback resumes. Start a new
  connection to change input format or select another video.
- Firefox enforces protected/cross-origin media restrictions. The extension does
  not extract media URLs or bypass those restrictions. One public YouTube video has been tested, including quality changes. This is
  not a guarantee for all YouTube videos, ads, protected content or other sites.
- Images, TAB input, automatic stereo detection and 2D depth conversion are not
  exposed by this extension yet.

## Architecture

`selected video → captureStream (video only) → Firefox WebRTC VP8 encoder →
local encrypted RTP → GStreamer webrtcbin → VP8 decoder → newest YUV frame → GPU`

There is no per-frame JavaScript timer, canvas, base64 conversion, native message,
JPEG file or disk polling for video. Native messaging carries a bounded SDP offer
and answer only. These small connection descriptions are exchanged through a
private temporary directory; media travels directly between the two local peers.
No external signalling, STUN or TURN service is configured. WebRTC uses temporary
UDP sockets and local host candidates; there is no HTTP/TCP video server.

The extension uses `activeTab`, `menus`, and `nativeMessaging`, and injects capture
code only after an explicit action. The executable path comes from local host
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
**SBS Full**. The eyes should read LEFT and RIGHT; Swap eyes should reverse them.
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
next video. Explicit Stop and closing/leaving the document still release capture.

This follows **in-page navigation**, as used by YouTube's watch links and Shorts.
A full browser reload or navigation that replaces the entire document still
requires opening capture again; no additional site permissions were added.

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
