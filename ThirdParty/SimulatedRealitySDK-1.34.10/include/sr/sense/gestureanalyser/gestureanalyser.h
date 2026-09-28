/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include <map>

#include "sr/management/srcontext.h"
#include "gesture.h"
#include "gesturestream.h"

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

/**
 * \defgroup GestureAnalyser
 * \brief Classes that enable applications to get access to data about gestures performed by the user
 */

/**
 * \brief Sense class which provides gesture analysis functionality to the SR system 
 *
 * \ingroup DownstreamInterface GestureAnalyser API
 */
class DIMENCOSR_API GestureAnalyser : public Sense {

public:
    /**
     * \brief Creates a functional GestureAnalyser instance
     *
     * \param context is the environment in which created senses are kept track of
     * \return GestureAnalyser* which can provide streams of gesture data
     *
     * The GestureAnalyser class is abstract and requires a device specific implementation to be used in applications.
     * This function constructs an GestureAnalyser suitable for use in your application on the current device.
     */
    static GestureAnalyser* create(SRContext &context);

    /**
     * \brief Creates a GestureStream for \p listener to be connected to
     *
     * \param listener will receive gesture updates from now on
     * \return @link GestureStream std::shared_ptr<GestureStream> @endlink to be used to close the stream correctly when it is no longer required
     */
    virtual std::shared_ptr<GestureStream> openGestureStream(GestureListener* listener) = 0;

    /**
     * \brief Closes a specific GestureStream
     *
     * \param stream will no longer supply gesture updates
     */
    virtual void streamClosed(GestureStream* stream) = 0;
};

}

#undef DIMENCOSR_API
