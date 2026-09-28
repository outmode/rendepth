/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "weaverposition.h"

namespace SR {

/**
 * \brief Interface for listening to SR_weaverPosition updates
 *
 * \ingroup WeaverTracker API
 */
class WeaverPositionListener {
public:
    /**
     * \brief Accept an SR_weaverPosition frame
     *
     * \param frame represents a new SR_weaverPosition update
     *
     * This function is called from an WeaverPositionStream.
     * Updates will be frequent, but the next update will only be receivable once accept has returned control to the stream.
     */
    virtual void accept(const SR_weaverPosition& frame) = 0;
};

}
