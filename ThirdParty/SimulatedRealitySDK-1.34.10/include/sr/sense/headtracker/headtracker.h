/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "headstream.h"
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
 * \defgroup HeadTracker
 * \brief Classes that enable applications to get access to data about the position of the user's head
 */

/**
 * \brief Sense class which provides face tracking functionality to the SR system
 *
 * \ingroup DownstreamInterface HeadTracker API
 */
class DIMENCOSR_API HeadTracker : public Sense {

public:
    /**
     * \brief Creates a functional HeadTracker instance
     *
     * \param context is the environment in which created senses are kept track of
     * \return HeadTracker* which can provide streams of head position data
     *
     * The HeadTracker class is abstract and requires a device specific implementation to be used in applications.
     * This function constructs an HeadTracker suitable for use in your application on the current device.
     */
    static HeadTracker* create(SRContext &context);

    /**
     * \brief Creates a HeadStream for \p listener to be connected to
     *
     * \param listener will receive head updates from now on
     * \return @link HeadStream std::shared_ptr<HeadStream> @endlink to be used to close the stream correctly when it is no longer required
     */
    virtual std::shared_ptr<HeadStream> openHeadStream(HeadListener* listener) = 0;

    /**
     * \brief Closes a specific HeadStream
     *
     * \param stream will no longer supply head updates
     */
    virtual void streamClosed(HeadStream* stream) = 0;
};

}

#undef DIMENCOSR_API
