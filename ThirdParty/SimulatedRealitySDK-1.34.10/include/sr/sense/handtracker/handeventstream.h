/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "handevent.h"
#include "handposelistener.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityHandTrackers
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {

class HandTracker; //forward declaration

/**
 * \brief Stream of SR_handEvent objects
 *
 * Connects a HandTracker Sense to a HandEventListener object.
 *
 * \ingroup HandTracker API
 */
class DIMENCOSR_API HandEventStream {
    class Impl;
    Impl* pimpl;
public:
    using Frame = SR_handEvent;

    /**
     * \brief Construct a new HandEventStream
     *
     * \param sense refers to the HandTracker providing updates
     * \param listener refers to the HandEventListener receiving updates
     *
     * Called by the HandTracker when requested for an HandEventListener through an openHandEventStream call.
     */
    HandEventStream(HandTracker* sense, HandEventListener* listener);

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
     * \param handEvent represents a single update
     */
    void update(Frame handEvent);
};

}

#undef DIMENCOSR_API
