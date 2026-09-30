/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "systemevent.h"
#include "systemeventlistener.h"

#include <memory>

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

class SystemSense; //forward declaration

/**
 * \brief Stream of SR_systemEvent objects
 *
 * Connects a SystemSense to a SystemEventListener object.
 *
 * \ingroup System API
 */
class DIMENCOSR_API SystemEventStream {
    class Impl;
    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(suppress: 4251)
    std::unique_ptr<Impl> pimpl;

public:
    using Frame = SystemEvent; //!< Used in OutputStreams template

    /**
     * \brief Construct a new SystemEventStream
     *
     * \param sense refers to the SystemSense providing updates
     * \param listener refers to the SystemEventListener receiving updates
     *
     * Called by the SystemSense when requested for an SystemEventListener through an openSystemEventStream call.
     */
    SystemEventStream(SystemSense* sense, SystemEventListener* listener);
    ~SystemEventStream();

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
    void update(SystemEvent frame);
};

}

#undef DIMENCOSR_API
