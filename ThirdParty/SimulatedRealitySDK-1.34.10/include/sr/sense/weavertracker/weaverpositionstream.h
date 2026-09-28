/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "weaverposition.h"
#include "weaverpositionlistener.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityFaceTrackers
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {

class WeaverTracker; //forward declaration

/**
 * \brief Stream of SR_weaverPosition objects
 *
 * Connects an WeaverTracker Sense to a WeaverPositionListener object.
 *
 * \ingroup WeaverTracker API
 */
class DIMENCOSR_API WeaverPositionStream {
private:
    class Impl;
    Impl* pimpl;
public:
    using Frame = SR_weaverPosition;

    /**
     * \brief Construct a new WeaverPositionStream
     *
     * \param sense refers to the WeaverTracker providing updates
     * \param listener refers to the WeaverPositionListener receiving updates
     *
     * Called by the WeaverTracker when requested for an WeaverPositionListener through an openWeaverPositionStream call.
     */
    WeaverPositionStream(WeaverTracker* sense, WeaverPositionListener* listener);

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
