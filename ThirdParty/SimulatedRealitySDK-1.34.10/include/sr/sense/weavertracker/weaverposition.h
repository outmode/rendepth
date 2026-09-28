/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "sr/types.h"
#include <stdint.h>


/**
 * \brief C-compatible struct containing the weaver position
 *
 * \ingroup WeaverTracker API
 */
typedef struct {
    uint64_t frameId; //!< Autoincrement frame number
    uint64_t time;    //!< Time of capture since epoch in microseconds
    SR_point3d weaverPosition; //!< Absolute weaver position in centimeters
} SR_weaverPosition;
