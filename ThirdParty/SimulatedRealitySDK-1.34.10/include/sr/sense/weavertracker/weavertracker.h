/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <map>
#include <thread>
#include <mutex>
#include "weaverposition.h"
#include "weaverpositionstream.h"
#include "sr/management/srcontext.h"

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

/**
 * \defgroup WeaverTracker
 * \brief Classes that enable applications to get access to weaving data
 */

/**
 * \brief Sense class which provides weave tracking functionality to the SR system
 *
 * \ingroup DownstreamInterface WeaverTracker API
 */
class DIMENCOSR_API WeaverTracker : public Sense {

public:
    /**
     * \brief Creates a functional WeaverTracker instance
     *
     * \param context is the environment in which created senses are kept track of
     * \return WeaverTracker* which can provide streams of weaving data
     *
     * The WeaverTracker class is abstract and requires a device specific implementation to be used in applications.
     * This function constructs an EyeTracker suitable for use in your application on the current device.
     */
    static WeaverTracker* create(SRContext &context);

    /**
     * \brief Creates a WeaverPositionStream for \p listener to be connected to
     *
     * \param listener will receive weaver position updates from now on
     * \return @link WeaverPositionStream std::shared_ptr<WeaverPositionStream> @endlink to be used to close the stream correctly when it is no longer required
     */
    virtual std::shared_ptr<WeaverPositionStream> openWeaverPositionStream(WeaverPositionListener* listener) = 0;

    /**
     * \brief Closes a specific WeaverPositionStream
     *
     * \param stream will no longer supply weaverposition updates
     */
    virtual void streamClosed(WeaverPositionStream* stream) = 0;
};

}

#undef DIMENCOSR_API
