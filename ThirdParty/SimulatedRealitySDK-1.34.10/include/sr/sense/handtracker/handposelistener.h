/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "handpose.h"
#include "handevent.h"

namespace SR {

/**
 * \brief Interface for listening to SR_handEvent updates
 *
 * \ingroup HandTracker API
 */
class HandEventListener {
public:
    /**
     * \brief Accept an SR_handEvent frame
     *
     * \param handEvent represents a new SR_handEvent update
     *
     * This function is called from a HandEventStream.
     * Updates will be frequent, but the next update will only be receivable once accept has returned control to the stream.
     */
    virtual void accept(const SR_handEvent& handEvent) = 0;
};

/**
 * \brief Interface for listening to SR_handPose updates
 *
 * \ingroup HandTracker API
 */
class HandPoseListener {
public:
    /**
     * \brief Accept an SR_handPose frame
     *
     * \param handPose represents a new SR_handPose update
     *
     * This function is called from a HandPoseStream.
     * Updates will be frequent, but the next update will only be receivable once accept has returned control to the stream.
     */
    virtual void accept(const SR_handPose& handPose) = 0;
};

}
