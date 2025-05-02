![Rendepth_Title](https://github.com/user-attachments/assets/227300ae-5ac2-494f-ac21-4e37f473bba9)

Free and Open Source Stereoscopic 3D Media Player.

Converts Any Standard Image to 3D and Supports SBS Stereo Photos.

Visit https://rendepth.com to Download the App.

###### Rendepth Source Code is MIT License. Third Party Dependencies Have Respective Licenses and Copyright.

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
  
Notes
------
Third Party `fCWT` currently has a bug on certain compilers failing to find `assert.h`. Please edit the file `rendepth/ThirdParty/fCWT/src/fcwt/fcwt.cpp` as below:
```
#include "fcwt.h"
#include <cassert>
#include <cstring>
```
Near the top of the file, around line 40.

Third Party `Depth-Anything-V2` will work, out-of-box, however Intel Arc and DirectML support won't. To add support for Arc GPUs and DirectML (which enables AMD GPUs on Windows), please edit `rendepth/Package/Depth-Anything-V2/depth_anything_v2/dpt.py` as follows:
```
import os
directml_available = False
if os.name == 'nt':
  try:
    import torch_directml
    directml_available = True
  except ImportError:
    directml_available = False
```
Near line 10 under the other imports.
```
DEVICE = 'cuda' if torch.cuda.is_available() else "xpu" if torch.xpu.is_available() else 'mps' if torch.backends.mps.is_available() else 'cpu'
  if directml_available and DEVICE == "cpu":
    DEVICE = torch_directml.device()
image = image.to(DEVICE)
```
Near line 225 of function `image2tensor`.

### Made by Outmode.


