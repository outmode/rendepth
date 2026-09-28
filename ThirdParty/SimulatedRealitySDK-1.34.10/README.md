These are the Windows build interfaces for Simulated Reality SDK 1.34.10,
obtained from [bo3b/SR-lib](https://github.com/bo3b/SR-lib/tree/174a0cf8868ed91838c9765db5128814c7a31c5c/SR-SDK-1.34.10).
The `include` directory contains the SR and OpenCV headers used by the SDK.
The `lib` directories contain x64 and x86 import libraries. See `LICENSE` for
the upstream notices.

Rendepth does not ship or install the SDK runtime DLLs. Lenticular mode loads
those DLLs from the monitor software already installed on the user's system.
