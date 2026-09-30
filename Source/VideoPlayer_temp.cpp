#include "VideoPlayer.h"
#include "BlurayReader.h"
#include "DvdReader.h"
#include <memory>

struct VideoPlayer::Impl {
    std::unique_ptr<BlurayReader> blurayReader;
    std::unique_ptr<DvdReader> dvdReader;
};
