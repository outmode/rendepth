#include <edge264.h>
#include <cstdio>
#include <initializer_list>

// Exercise the decoder ABI and worker lifecycle, including the separate
// MinGW DLL used by MSVC builds. No disc or graphics device is required.
int main() {
    for (int workers : {1, 2, 0}) {
        auto* decoder = edge264_alloc(workers, nullptr, nullptr, 0, nullptr, nullptr, nullptr);
        if (!decoder) return 1;
        Edge264Frame frame{};
        if (edge264_get_frame(decoder, &frame, 1) == 0) return 2;
        edge264_flush(decoder);
        edge264_bump_frames(decoder);
        edge264_free(&decoder);
        if (decoder) return 3;
    }
    std::puts("PASS: MVC decoder ABI and worker lifecycle");
}
