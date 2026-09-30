![Rendepth_Title](https://github.com/user-attachments/assets/227300ae-5ac2-494f-ac21-4e37f473bba9)

Free and Open Source Stereoscopic 3D Media Player.

Converts Any Standard Image/Video to 3D and Supports Native Stereo Media.

Visit https://rendepth.com to Download the App.

Original Rendepth source code is available under the [MIT License](LICENSE).
The combined application is distributed under [GNU GPL version 3](Legal/RENDEPTH_APP_LICENSE).
Dependencies retain their own licenses; the collected texts and attributions are in
[THIRD_PARTY_LICENSING](Legal/THIRD_PARTY_LICENSING). See the
[licensing maintenance guide](Tools/Legal/README.md) for release-source and notice requirements.

Stereo 3D Samples
------
![Japan_1080P_anaglyph](https://github.com/user-attachments/assets/4488e967-21f0-4e29-82a0-26d6b5447944)

![Japan_1080P_free_view](https://github.com/user-attachments/assets/6804236b-4631-46c2-ba54-20920844b082)

![Japan_1080P_sbs](https://github.com/user-attachments/assets/47d03596-8218-47d6-8b69-ab4acefea47e)

![Japan_1080P_rgbd](https://github.com/user-attachments/assets/c30d9f27-8c2c-40be-a23e-934211771656)

#### Enjoying Rendepth? Consider donating to support further development: https://rendepth.com/donate

Build Instructions
------
Use CMake 3.22 or newer, a C++20 compiler, and Git. Clone with the pinned
dependencies, or initialize them after cloning:

```sh
git clone --recurse-submodules https://github.com/outmode/rendepth.git
cd rendepth
```

After pulling Rendepth updates, run `git submodule update --init --recursive`
to synchronize nested dependencies with the recorded commits. Do not use
`--remote`, which follows upstream branches instead.

On Linux, install development files for libcurl, OpenSSL, GTK 3, GLib/GIO,
GStreamer 1.0 (core, app, video, WebRTC, and SDP; WebRTC requires 1.22 or
newer), libbluray, and libdvdread, plus pkg-config and GNU Make. Supply a
CPU-only ONNX Runtime shared library, its `LICENSE` and
`ThirdPartyNotices.txt`, and compatible ONNX Runtime headers. From the
repository root, configure and build a Release executable:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRENDEPTH_ONNXRUNTIME_INCLUDE_DIR=/path/to/ort-headers/include/onnxruntime \
  -DRENDEPTH_CPU_RUNTIME_LIBRARY=/path/to/cpu-sdk/lib/libonnxruntime.so.1 \
  -DRENDEPTH_CPU_RUNTIME_NOTICES_DIR=/path/to/cpu-sdk
cmake --build build --parallel 14 --target Rendepth
```

Point `RENDEPTH_ONNXRUNTIME_INCLUDE_DIR` to the directory containing
`onnxruntime_cxx_api.h`. See [Linux inference packaging](Packaging/Linux/README.md)
for CPU runtime requirements and GPU pack assembly.

On Windows, use an x64 MSVC build and an x64 MinGW GCC toolchain for the bundled
MVC decoder. Provide a CPU-only x64 ONNX Runtime SDK with headers and notices
(`RENDEPTH_CPU_RUNTIME_DIR`), plus an MSVC-compatible FFmpeg SDK
(`RENDEPTH_FFMPEG_ROOT`). The FFmpeg SDK needs `include`, `lib`,
`debug/lib`, `bin`, and `debug/bin`; install libbluray and libdvdread in that
same SDK, or set `RENDEPTH_DISC_ROOT` to their SDK. For example, configure with
`-DRENDEPTH_CPU_RUNTIME_DIR=C:/SDKs/onnxruntime-win-x64 -DRENDEPTH_FFMPEG_ROOT=C:/SDKs/vcpkg/installed/x64-windows`,
then run `cmake --build build --config Release --parallel 28 --target Rendepth`.
For a local UI test in CLion, select the `Debug-Visual Studio` CMake profile
and the `Rendepth` run configuration, then build and run it directly. This
builds `Debug/Rendepth.exe` with the repository's assets and shaders; the
Windows installer script is only needed when making an installer. Set the
profile's build option to `-j 28`. If the profile uses the Visual Studio 2026
generator, use CLion's bundled CMake rather than an older system CMake.
See [Windows inference packaging](Packaging/Windows/README.md) for pack details.
On macOS, provide an ONNX Runtime SDK via `RENDEPTH_ONNXRUNTIME_DIR` and the
libbluray and libdvdread development packages; see the disc playback notes below.
The SDK must include the Core ML execution provider. Depth and super resolution
use Core ML's CPU and GPU compute units by default, with CPU fallback when a
model cannot load through Core ML. The `DepthTest` and `SuperTest` tools accept
`--provider coreml` or `--provider cpu` to select an engine explicitly.

The Release executable is written to `Binary` in the repository root. Keep
`Assets`, `Binary`, `Library`, `Runtimes`, and `Shaders` together when running or
packaging it. FFmpeg and the SDL libraries are built from the pinned sources on
Linux and macOS. Precompiled shaders are included, so a normal build does not
require a separate SDL_shadercross installation.

Dependency versions are pinned by Git submodule commits. The SDL and SyLC pins are:

| Dependency | Version / commit |
| --- | --- |
| SDL | `release-3.4.16` |
| SDL_image | `release-3.4.6` |
| SDL_ttf | `release-3.2.2` |
| SDL_shadercross | `1ff05bec573988a98ef9e0260b4da44f512b8367` (no upstream release tags) |
| SyLC | `v5.3.1` |

- Linux/macOS can use GCC/Clang. Windows builds use MSVC.
- Windows builds bundle a CPU inference runtime and offer optional NVIDIA CUDA
  and DirectML (AMD, Intel Arc, NVIDIA) packs. Configure a CPU-only x64 SDK with
  `RENDEPTH_CPU_RUNTIME_DIR`; select the engine in Settings and restart.
  See [Windows inference packaging](Packaging/Windows/README.md) for pack
  assembly, installation, and validation.
- Linux builds use one executable with a bundled CPU inference runtime. Settings
  selects CPU, NVIDIA CUDA, or AMD ROCm on the next launch. GPU runtime packs
  live separately in `~/.Rendepth/Runtimes`; missing or failing packs fall back
  to CPU. See [Linux inference packaging](Packaging/Linux/README.md) for build
  inputs, pack layout, and validation. Selecting a missing GPU pack downloads
  it from `rendepth.com/packs` when that pack has been published there.
- Windows FFmpeg runtime DLLs are copied next to `Rendepth.exe` automatically.
- Set `RENDEPTH_MAC_EXPORT_BUNDLE=ON` in a separate macOS export build directory
  to create the app bundle; CLion builds a regular executable.

Lightfield Displays
------
In lightfield mode, Rendepth automatically selects up to two connected displays:
one CubeVi C1 and one Looking Glass (including LKG Go). Each output uses its own
model's calibration and shares the loaded image/depth source. Additional displays
of the same type are skipped because calibration is shared within each model.
Connecting or disconnecting a display refreshes the outputs automatically.
Native stereo sources use two views on each supported lightfield display;
RGB-D images and quilts keep their multiview paths.

`RENDEPTH_NATIVE_DISPLAY` still restricts selection to matching display names when set.

Disc playback
------
DVD, Blu-ray and audio CD playback are built for Windows, macOS and Linux.
`libbluray` and `libdvdread` development packages are required: configuration
fails if either is missing instead of producing a player with disabled readers.

- Windows: install `libbluray:x64-windows` and `libdvdread:x64-windows` with
  vcpkg in the same SDK as FFmpeg. `RENDEPTH_DISC_ROOT` defaults to
  `RENDEPTH_FFMPEG_ROOT`; a separate SDK must contain `include`, `lib`,
  `debug/lib`, `bin` and `debug/bin`. Runtime DLLs are copied and packaged.
- macOS: install `libbluray` and `libdvdread` development packages and expose
  their prefix through `CMAKE_PREFIX_PATH` or pkg-config. App bundles copy
  their linked libraries automatically. Audio CDs use the native raw-device
  reader; select their Finder volume or an audio track in Load Media.
- Linux: install the distribution's libbluray and libdvdread development
  packages. Installed packages also require the corresponding runtime libraries.

If CMake asks for `RENDEPTH_BLURAY_INCLUDE_DIR`, point it to the directory
containing `libbluray/bluray.h` (for example, `<sdk>/include` or `/usr/include`),
not to the `libbluray` directory itself. `RENDEPTH_DVDREAD_INCLUDE_DIR` similarly
points to the directory containing `dvdread/dvd_reader.h`. On Windows, setting
`RENDEPTH_DISC_ROOT` to the SDK prefix lets CMake find both headers and libraries.

Blu-ray remains a Pro feature. Protected media still requires decryption support
and any keys required by the underlying disc libraries; enabling a backend does
not make every encrypted disc readable.

Opening a Blu-ray or DVD automatically starts the most likely main feature.
The disc's main-title hint is preferred, with the reader's longest-title choice
as a fallback. On supported builds, a 3D title with matching duration and chapter
count is preferred over the 2D choice. Use **Track Selection** during playback
to browse and choose another title manually.

Blu-ray 3D decoding
------
Native MVC decoding uses the BSD-licensed `edge264` sources from the pinned
[SyLC](https://github.com/5ymph0en1x/SyLC) submodule. Only that decoder is built;
Rendepth continues to use libbluray for disc access and FFmpeg for transport,
audio, and subtitles. Initialize it with
`git submodule update --init ThirdParty/SyLC`.

MVC playlists are labelled **Blu-ray 3D** in the title browser. Their original
left and right views enter Rendepth's full-resolution stereo rendering path;
depth inference is disabled for these titles. Decryption requirements are the
same as for ordinary Blu-ray playback.

Decoded pictures remain owned until both views have been submitted, so the
right eye retains its inter-view references. A background reader prefetches
compressed dependent-view packets so disc reads overlap decoding. The base
stream also prefetches its next 2 MiB input block while decoding consumes the
current one. The readers serialize whole refill chunks to avoid competing
small reads on the same optical drive. The dependent queue has a 4 MiB
high-water mark and a 128-packet limit; seeking and closing stop the reader
before resetting the demuxer. x86-64 builds include runtime-selected
SSE/AVX2 decoder variants.
MVC playback primes one second of decoded video before starting audio and
allows 1.5 seconds in the presentation queue to bridge optical-drive stalls.
This uses more memory and adds startup buffering compared with ordinary video.

The source decoder builds with GCC/Clang and POSIX threads. Windows MSVC builds
compile it separately with x64 MinGW GCC into `rendepth-mvc.dll`, with static
GCC/winpthreads runtime support and an MSVC import library. Set
`RENDEPTH_MVC_GCC` if GCC is not found in PATH, CLion's bundled MinGW, or the
standard MSYS2 locations. The application and FFmpeg continue to use MSVC.
Decoder and runtime notices are included in THIRD_PARTY_LICENSING.

`MvcRuntimeTest` checks the decoder ABI and worker lifecycle without a disc.
`AudioCdReaderTest` checks PCM streaming, seeking, disc signatures and TOC parsing.

`BlurayPlaylistTest` checks playlist parsing without a disc.
`DiscReadAheadTest` checks buffered reads, seeks, EOF and I/O errors without a disc.
`MvcPlaybackTest <disc path>` checks stereo output, seeking, clip transitions,
audio and looping using a readable 1080p MVC disc. Optional title-index and
seek-seconds arguments select a particular title and seek for diagnosis.
Appending a `frame.yuv` path runs a 240-frame throughput check and saves frame
24 as 3840×1080 planar YUV420P for inspecting both eyes without the renderer.
The test also reports the 95th-percentile and maximum frame-delivery gaps,
and the number exceeding 100 ms. These measure decoder delivery, not display
pacing; compare first reads from optical media with repeated runs, since the
OS file cache can hide disc stalls.
An optional final frame-count argument extends the throughput run beyond its
default 240 frames, for example `MvcPlaybackTest <disc> 132 1950 frame.yuv 720`.
Build a test target with
`cmake --build <build-directory> --target <test-name> --parallel 10`.

### Made by Outmode.

Keyboard presentation controls
------
- **0:** Disable 3D and hide the 3D button, as with Disabled in settings.
- **1:** Turn on 3D in the current display mode; each later press advances to the
  next mode, looping after the last one. Disabled is skipped.
- **2:** Switch to 2D using the display mode selected in settings.
- **3:** Switch to 3D using the display mode selected in settings.

When 3D is disabled, **2** and **3** restore the last saved display mode in its
2D or 3D view, including across restarts. Choose the display format in settings
or cycle with **1**. **4–9** do not change presentation modes. If no display mode
has been used yet, **1/2/3** use Natural Color.
