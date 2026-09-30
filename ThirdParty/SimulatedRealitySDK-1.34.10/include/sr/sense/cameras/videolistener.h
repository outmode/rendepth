/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "videoframe.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityCameras
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {

/**
 * \brief Interface for listening to SR_videoFrame updates
 *
 * \ingroup Camera API
 */
class DIMENCOSR_API VideoListener {
public:
    /**
     * \brief Accept an SR_videoFrame frame
     *
     * \param frame represents a new SR_videoFrame update
     *
     * This function is called from an VideoStream.
     * Updates will be frequent, but the next update will only be receivable once accept has returned control to the stream.
     */
    virtual void accept(const VideoFrame& frame) = 0;
};

}

#undef DIMENCOSR_API
