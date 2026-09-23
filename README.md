![Rendepth_Title](https://github.com/user-attachments/assets/227300ae-5ac2-494f-ac21-4e37f473bba9)

Free and Open Source Stereoscopic 3D Media Player.

Converts Any Standard Image to 3D and Supports SBS Stereo Photos.

Visit https://rendepth.com to Download the App.

Original Rendepth source code is available under the [MIT License](LICENSE).
The combined application is distributed under [GNU GPL version 3](Legal/RENDEPTH_APP_LICENSE).
Dependencies retain their own licenses; the collected texts and attributions are in
[THIRD_PARTY_LICENSING](Legal/THIRD_PARTY_LICENSING). See the
[licensing maintenance guide](Tools/Legal/README.md) for release-source and notice requirements.

Lightfield Displays
------
In lightfield mode, Rendepth automatically selects up to two connected displays:
one CubeVi C1 and one Looking Glass (including LKG Go). Each output uses its own
model's calibration and shares the loaded image/depth source. Additional displays
of the same type are skipped because calibration is shared within each model.
Connecting or disconnecting a display refreshes the outputs automatically.

`RENDEPTH_NATIVE_DISPLAY` still restricts selection to matching display names when set.

Stereo 3D Samples
------
![Japan_1080P_anaglyph](https://github.com/user-attachments/assets/4488e967-21f0-4e29-82a0-26d6b5447944)

![Japan_1080P_free_view](https://github.com/user-attachments/assets/6804236b-4631-46c2-ba54-20920844b082)

![Japan_1080P_sbs](https://github.com/user-attachments/assets/47d03596-8218-47d6-8b69-ab4acefea47e)

![Japan_1080P_rgbd](https://github.com/user-attachments/assets/c30d9f27-8c2c-40be-a23e-934211771656)

#### Enjoying Rendepth? Consider donating to support further development: https://rendepth.com/donate

Build Instructions
------
- Clone this repository: `git clone https://github.com/outmode/rendepth.git`
- Go to the root folder: `cd rendepth`
- Initialize submodules: `git submodule update --init --recursive`
- After pulling Rendepth updates, run the same command to synchronize the pinned dependencies, including nested submodules. Do not add `--remote`: that follows upstream branches instead of Rendepth's recorded commits.
- Check `ThirdParty` folder and install dependencies for each library.
- Build and Install `SDL_shadercross` needed for compiling shaders.
- Navigate to the root folder of the repo: `rendepth`
- Make build directory: `mkdir build`
- Navigate to directory: `cd build`
- Build for Release: `cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release`
- Compile project: `cmake --build .`
- App will be built in `Binary` folder.

Dependency versions are pinned by Git submodule commits. The SDL and SyLC pins are:

| Dependency | Version / commit |
| --- | --- |
| SDL | `release-3.4.16` |
| SDL_image | `release-3.4.6` |
| SDL_ttf | `release-3.2.2` |
| SDL_shadercross | `1ff05bec573988a98ef9e0260b4da44f512b8367` (no upstream release tags) |
| SyLC | `v5.3.1` |
- Folders `Assets` `Binary` `Library` `Shaders` must remain together.

- Linux/macOS can use g++/clang. Windows builds use MSVC.
- Windows builds bundle a CPU inference runtime and offer optional NVIDIA CUDA
  and DirectML (AMD, Intel Arc, NVIDIA) packs. Configure a CPU-only x64 SDK with
  `RENDEPTH_CPU_RUNTIME_DIR`; select the engine in Settings and restart.
  See [Windows inference packaging](Packaging/Windows/README.md) for pack
  assembly, installation, and validation. Keep `Runtimes` beside `Binary`.
- Linux builds use one executable with a bundled CPU inference runtime. Settings
  selects CPU, NVIDIA CUDA, or AMD ROCm on the next launch. GPU runtime packs
  live separately in `~/.Rendepth/Runtimes`; missing or failing packs fall back
  to CPU. See [Linux inference packaging](Packaging/Linux/README.md) for build
  inputs, pack layout, and validation. GPU downloads are not yet integrated.
- To enable Windows video playback, install an MSVC-compatible FFmpeg SDK (for
  example with vcpkg) and configure with
  `-DRENDEPTH_ENABLE_FFMPEG=ON -DRENDEPTH_FFMPEG_ROOT=<vcpkg>/installed/x64-windows`.
  FFmpeg's runtime DLLs are copied next to `Rendepth.exe` automatically.
- `RENDEPTH_DLL_DIR` points to the MinGW shared library folder for legacy
  MinGW builds.
- `RENDEPTH_MAC_BUNDLE` set `ON` to create macOS bundle after building.

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
