/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "headlistener.h"
#include <memory>

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

class HeadTracker; //forward declaration

/**
 * \brief Stream of SR_head objects
 *
 * Connects an HeadTracker Sense to a HeadListener object.
 *
 * \ingroup HeadTracker API
 */
class DIMENCOSR_API HeadStream {
private:
    class Impl;
    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(suppress: 4251)
    std::unique_ptr<Impl> pimpl;
public:
    using Frame = SR_head;

    /**
     * \brief Construct a new HeadStream
     *
     * \param sense refers to the HeadTracker providing updates
     * \param listener refers to the HeadListener receiving updates
     *
     * Called by the HeadTracker when requested for an HeadListener through an openHeadStream call.
     */
    HeadStream(HeadTracker* sense, HeadListener* listener);
    ~HeadStream();

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
    void update(Frame& frame);
};

}

#undef DIMENCOSR_API
