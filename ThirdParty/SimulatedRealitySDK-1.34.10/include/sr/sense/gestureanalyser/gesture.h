/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <stdint.h>
#include "sr/types.h"

#ifdef __cplusplus
enum SR_gestureType : uint64_t {
    TapGesture = 0,
    SwipeGesture = 1,
    GrabGesture = 2,
    ReleaseGesture = 3
};
#else
typedef uint64_t SR_gestureType;
#endif

/**
 * \brief C-compatible struct notifying the listener that a gesture has been performed by the user
 *
 * \ingroup GestureAnalyser API
 */
typedef struct {
    uint64_t frameId; //!< Autoincrement frame number
    uint64_t time;    //!< Time since epoch in microseconds
    SR_point3d position; //!< Absolute positions where the gesture occured
    SR_gestureType type; //!< Type of gesture
} SR_gesture;
