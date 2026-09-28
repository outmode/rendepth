/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <map>
#include <thread>
#include <mutex>
#include "systemevent.h"
#include "systemeventstream.h"
#include "systemeventlistener.h"
#include "sr/management/srcontext.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityCore
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {

/**
 * \defgroup System
 * \brief Classes that enable applications to get access to data about SR system
 */

/**
 * \brief Sense class which shares information about the SR system throughout the SR system and to applications
 *
 * \ingroup DownstreamInterface System API
 */
class DIMENCOSR_API SystemSense : public Sense {

public:
    /**
     * \brief Creates a functional SystemSense instance
     *
     * \param context is the environment in which created senses are kept track of
     * \return SystemSense* which can provide streams of system-wide events
     */
    static SystemSense* create(SRContext &context);

    /**
     * \brief Creates a SystemEventStream for \p listener to be connected to
     *
     * \param listener will receive system-wide event updates from now on
     * \return @link SystemEventStream std::shared_ptr<SystemEventStream> @endlink to be used to close the stream correctly when it is no longer required
     */
    virtual std::shared_ptr<SystemEventStream> openSystemEventStream(SystemEventListener* listener) = 0;

    /**
     * \brief Closes a specific SystemEventStream
     *
     * \param stream will no longer supply SystemEvent updates
     */
    virtual void streamClosed(SystemEventStream* stream) = 0;
};

}

#undef DIMENCOSR_API
