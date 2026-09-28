/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "head.h"

namespace SR {

/**
 * \brief Interface for listening to SR_headPose updates
 * This interface is supported from Eye Tracker version 1.5.4
 *
 * \ingroup HeadPoseTracker API
 */
class HeadPoseListener {
public:
    /**
     * \brief Accept an SR_headPose frame
     *
     * \param frame represents a new SR_headPose update
     *
     * This function is called from an HeadPoseStream.
     * Updates will be frequent, but the next update will only be receivable once accept has returned control to the stream.
     */
    virtual void accept(const SR_headPose& frame) = 0;
};

}
