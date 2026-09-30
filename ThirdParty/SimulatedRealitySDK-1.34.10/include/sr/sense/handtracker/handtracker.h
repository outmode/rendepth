/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "sr/management/srcontext.h"

#include <map>
#include <thread>
#include <mutex>
#include <string>

#include "handpose.h"
#include "handposestream.h"
#include "handevent.h"
#include "handeventstream.h"

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
 * \defgroup HandTracker
 * \brief Classes that enable applications to get access to data about the pose of the user's hands
 */

/**
 * \brief Sense class which provides hand tracking functionality to the SR system
 *
 * \ingroup DownstreamInterface HandTracker API
 */
class DIMENCOSR_API HandTracker : public Sense {

public:
    /**
     * \brief Creates a functional HandTracker instance
     *
     * The HandTracker class is abstract and requires a device specific implementation to be used in applications.
     * This function constructs a HandTracker suitable for use in your application on the current device.
     */
    static HandTracker* create(SRContext &context);

    /**
     * \brief Creates a HandPoseStream for listener to be connected to
     * \param listener will receive handpose updates from now on
     */
    virtual std::shared_ptr<HandPoseStream> openHandPoseStream(HandPoseListener* listener) = 0;
    /**
     * \brief Creates a HandPoseStream specific for \p handIdentifier for \p listener to be connected to
     * \param listener will receive handpose updates about hand \p handIdentifier from now on
     * \param handIdentifier refers to the a specific instance of a hand that was detected by the system
     */
    virtual std::shared_ptr<HandPoseStream> openHandPoseStream(HandPoseListener* listener, uint64_t handIdentifier) = 0;
    /**
     * \brief Creates a HandEventStream for \p listener to be connected to
     * \param listener will receive hand events from now on
     *
     * When a new handPoseStream is registered, existing hands will raise an `CreateHand` event.
     */
    virtual std::shared_ptr<HandEventStream> openHandEventStream(HandEventListener* listener) = 0;
    /**
     * \brief Closes a specific HandPoseStream
     * \param stream will no longer supply handpose updates
     */
    virtual void streamClosed(HandPoseStream* stream) = 0;
    /**
     * \brief Closes a specific HandEventStream
     * \param stream will no longer supply hand events
     */
    virtual void streamClosed(HandEventStream* stream) = 0;

    /**
     * \brief Creates a HandPoseStream specific for \p createEvent for \p listener to be connected to
     *
     * \param createEvent received by an instance of HandEventListener used to open a new stream
     * \param listener instance of HandPoseListener that should be receiving the data from the new stream
     *
     * \deprecated Creating a new stream in event handling function can cause deadlock
     */
    static std::shared_ptr<HandPoseStream> openDynamicHandPoseStream(SR_handEvent createEvent, HandPoseListener* listener) {
        if (createEvent.eventType == CreateHand) {
            HandTracker* sender = (HandTracker*)createEvent.sender;
            return sender->openHandPoseStream(listener, createEvent.handId);
        }
        else {
            return nullptr;
        }
    }

    /**
     * \brief Closes a specific HandPoseStream referenced by \p destroyEvent
     *
     * \param destroyEvent received by an instance of HandEventListener signaling that no more data about a specific hand will be available
     * \param listener instance of HandPoseListener that was receiving data after a call to openDynamicHandPoseStream
     *
     * \deprecated Creating a new stream in event handling function can cause deadlock
     */
    static void closeDynamicHandPoseStream(SR_handEvent destroyEvent, HandPoseListener* listener) {
        if (destroyEvent.eventType == DestroyHand) {
            HandTracker* sender = (HandTracker*)destroyEvent.sender;
            // Do stuff
        }
    }
};

}

#undef DIMENCOSR_API
