/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#ifdef __cplusplus
#include <memory>
#include "opencv2/core/mat.hpp"
#endif

/**
 * \brief C-compatible struct containing an image recorded by a Camera
 *
 * \ingroup Camera API
 */
typedef struct {
    uint64_t frameId;
    uint64_t time;
    uint64_t streamId;
    uint64_t channels;
    uint64_t height;
    uint64_t width;
    int64_t valueType;

    uint64_t cropY;
    uint64_t cropX;
    uint64_t originalHeight;
    uint64_t originalWidth;

    void *data;
} SR_videoFrame;

#ifdef __cplusplus
namespace SR {

/**
 * \brief Class containing an image recorded by a Camera
 *
 * The class can be converted to a C-compatible struct when required, the original C++ object should be kept in scope to guarantee the image data being available.
 * If the memory is copied the ownership is no longer governed by the shared_ptr.
 *
 * \ingroup Camera API
 */
class VideoFrame {
public:
    uint64_t frameId;
    uint64_t time;
    uint64_t streamId;
    std::shared_ptr<cv::Mat> image;

    operator SR_videoFrame() {
        return {
            frameId,
            time,
            streamId,
            (uint64_t)image->channels(),
            (uint64_t)image->size().height,
            (uint64_t)image->size().width,
            image->type(),
            0, 0, 0, 0,
            image->data,
        };
    }
};

}
#endif
