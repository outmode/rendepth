/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "headposestream.h"
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
 * \defgroup HeadPoseTracker
 * \brief Classes that enable applications to get access to data about the pose of the user's head
 */

/**
 * \brief Sense class which provides head pose tracking functionality to the SR system
 *
 * \ingroup DownstreamInterface HeadPoseTracker API
 */
class DIMENCOSR_API HeadPoseTracker : public Sense {

public:
    /**
     * \brief Creates a functional HeadPoseTracker instance
     *
     * \param context is the environment in which created senses are kept track of
     * \return HeadPoseTracker* which can provide streams of head pose data
     *
     * The HeadPoseTracker class is abstract and requires a device specific implementation to be used in applications.
     * This function constructs an HeadPoseTracker suitable for use in your application on the current device.
     */
    static HeadPoseTracker* create(SRContext &context);

    /**
     * \brief Creates a HeadPoseStream for \p listener to be connected to
     *
     * \param listener will receive head pose updates from now on
     * \return @link HeadPoseStream std::shared_ptr<HeadPoseStream> @endlink to be used to close the stream correctly when it is no longer required
     */
    virtual std::shared_ptr<HeadPoseStream> openHeadPoseStream(HeadPoseListener* listener) = 0;

    /**
     * \brief Closes a specific HeadPoseStream
     *
     * \param stream will no longer supply headpose updates
     */
    virtual void streamClosed(HeadPoseStream* stream) = 0;
};

}

#undef DIMENCOSR_API
