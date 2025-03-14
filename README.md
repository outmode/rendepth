![Rendepth_Title](https://github.com/user-attachments/assets/227300ae-5ac2-494f-ac21-4e37f473bba9)

Free and Open Source Stereoscopic 3D Media Player.

Converts Any Standard Image to 3D and Supports SBS Stereo Photos.

Visit https://rendepth.com to Download the App.

###### Rendepth Source Code is MIT License. Third Party Dependencies Have Respective License and Copyright.

Build Instructions
------
- Clone this repository: `git clone https://github.com/outmode/rendepth.git`
- Go to the root folder: `cd rendepth`
- Initialize submodules: `git submodule update --init --recursive`
- Check `ThirdParty` folder and install dependencies for each library.
- Build and Install `SDL_shadercross` needed for compiling shaders.
- Navigate to the root folder of the repo: `rendepth`
- Make build directory: `mkdir build`
- Navigate to directory: `cd build`
- Build for Release: `cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release`
- Compile project: `cmake --build .`
- App will be built in `Binary` folder.
- Folders `Assets` `Binary` `Library` `Shaders` must remain together.
- Rendepth requires C/C++ compiler with OpenMP 4.5 support.
- Linux/macOS can use g++/clang, Windows may require MinGW.
- `RENDEPTH_DLL_DIR` points to MinGW shared library folder on Windows.
- `RENDEPTH_OMP_DYLIB` points to the `libomp` shared library on macOS.
- `RENDEPTH_MAC_BUNDLE` set `ON` to create macOS bundle after building.
