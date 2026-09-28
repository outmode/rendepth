/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "videoframe.h"
#include "videolistener.h"

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

class Camera; //forward declaration

/**
 * \brief Stream of SR_videoFrame objects
 *
 * Connects an Camera Sense to a VideoListener object.
 *
 * \ingroup Camera API
 */
class DIMENCOSR_API VideoStream {
private:
    class Impl;
    Impl* pimpl;
public:
    using Frame = SR::VideoFrame;

    /**
     * \brief Construct a new VideoStream
     *
     * \param sense refers to the Camera providing updates
     * \param listener refers to the VideoListener receiving updates
     *
     * Called by the Camera when requested for an VideoListener through an openVideoStream call.
     */
    VideoStream(Camera* sense, VideoListener* listener);

    /**
     * \brief Stop listening and notify sense to stop sending new frames. (Called by listener)
     */
    void stopListening();

    /**
     * \brief Close the stream. (Called by sense)
     */
    void close();

    /**
     * \brief Send new frame to listener.
     *
     * \param frame represents a single update
     */
    void update(Frame frame);
};

}

#undef DIMENCOSR_API
