/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "sr/types.h"
#include <stdint.h>

#include "sr/sense/eyetracker/eyepair.h"

typedef struct {
    uint64_t frameId; //!< Autoincrement frame number
    uint64_t time;    //!< Time since epoch in microseconds
    SR_point3d position; //!< Position of head in millimeter
    SR_point3d orientation; //!< Rotation of the head in radians.
    //!< Positive rotations are clockwise from the perspective of the user.
    //!< (x, y, z) correspond to (pitch, yaw, roll).
    
} SR_headPose;

typedef struct {
    uint64_t frameId; //!< Autoincrement frame number
    uint64_t time;    //!< Time since epoch in microseconds
    union {
        SR_point3d ears[2]; //!< Positions of left and right ear in millimeter
        struct {
            SR_point3d left; //!< Position of left ear in millimeter
            SR_point3d right; //!< Position of right ear in millimeter
        };
    };
} SR_earPair;

/**
 * \brief C-compatible struct containing the position of head
 *
 * \ingroup HeadTracker API
 */
typedef struct {
    uint64_t frameId; //!< Autoincrement frame number
    uint64_t time;    //!< Time since epoch in microseconds
    SR_headPose headPose;
    SR_eyePair eyes;
    SR_earPair ears;
} SR_head;
