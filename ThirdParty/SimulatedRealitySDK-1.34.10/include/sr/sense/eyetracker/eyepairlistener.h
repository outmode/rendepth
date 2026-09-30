/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "eyepair.h"

namespace SR {

/**
 * \brief Interface for listening to SR_eyePair updates
 *
 * \ingroup EyeTracker API
 */
class EyePairListener {
public:
    /**
     * \brief Accept an SR_eyePair frame
     *
     * \param frame represents a new SR_eyePair update
     *
     * This function is called from an EyePairStream.
     * Updates will be frequent, but the next update will only be receivable once accept has returned control to the stream.
     */
    virtual void accept(const SR_eyePair& frame) = 0;
};

}
