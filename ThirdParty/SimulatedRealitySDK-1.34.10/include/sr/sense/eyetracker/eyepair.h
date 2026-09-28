/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "sr/types.h"
#include <stdint.h>

#ifdef __cplusplus
enum SR_eyeSide : uint64_t {
    LeftEye = 0, // Left eye
    RightEye = 1 // Right eye
};
#else
typedef uint64_t SR_eyeSide;
#endif

/**
 * \brief C-compatible struct containing the position of two eyes
 *
 * \ingroup EyeTracker API
 */
typedef struct {
    uint64_t frameId; //!< Autoincrement frame number
    uint64_t time;    //!< Time since epoch in microseconds
    union {
        SR_point3d eyes[2]; //!< Absolute positions of left and right eye in millimeter
        struct {
            SR_point3d left;  //!< Absolute position of left eye in millimeter
            SR_point3d right; //!< Absolute position of right eye in millimeter
        };
    }; //!< \private Eye position data can be adressed in multiple ways
} SR_eyePair;
