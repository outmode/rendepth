/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <map>
#include <thread>
#include <mutex>
#include "eyepair.h"
#include "eyepairstream.h"
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
 * \defgroup EyeTracker
 * \brief Classes that enable applications to get access to data about the position of the user's eyes
 */

/**
 * \brief Sense class which provides face tracking functionality to the SR system
 *
 * \ingroup DownstreamInterface EyeTracker API
 */
class DIMENCOSR_API EyeTracker : public Sense {

public:
    /**
     * \brief Creates a functional EyeTracker instance
     *
     * \param context is the environment in which created senses are kept track of
     * \return EyeTracker* which can provide streams of eye position data
     *
     * The EyeTracker class is abstract and requires a device specific implementation to be used in applications.
     * This function constructs an EyeTracker suitable for use in your application on the current device.
     */
    static EyeTracker* create(SRContext &context);

    /**
     * \brief Creates a functional EyeTracker instance providing unfiltered data
     *
     * \param context is the environment in which created senses are kept track of
     * \return EyeTracker* which can provide streams of eye position data
     *
     * The EyeTracker class is abstract and requires a device specific implementation to be used in applications.
     * This function constructs an EyeTracker suitable for use in your application on the current device.
     */
    static EyeTracker* createRaw(SRContext &context);

    /**
     * \brief Creates a EyePairStream for \p listener to be connected to
     *
     * \param listener will receive eyepair updates from now on
     * \return @link EyePairStream std::shared_ptr<EyePairStream> @endlink to be used to close the stream correctly when it is no longer required
     */
    virtual std::shared_ptr<EyePairStream> openEyePairStream(EyePairListener* listener) = 0;

    /**
     * \brief Closes a specific EyePairStream
     *
     * \param stream will no longer supply eyepair updates
     */
    virtual void streamClosed(EyePairStream* stream) = 0;
};

}

#undef DIMENCOSR_API
