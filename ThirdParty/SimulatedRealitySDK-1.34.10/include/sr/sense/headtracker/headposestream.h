/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "headposelistener.h"
#include "head.h"
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

class HeadPoseTracker; //forward declaration

/**
 * \brief Stream of SR_headPose objects
 *
 * Connects an HeadPoseTracker Sense to a HeadPoseListener object.
 *
 * \ingroup HeadPoseTracker API
 */
class DIMENCOSR_API HeadPoseStream {
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
    using Frame = SR_headPose;

    /**
     * \brief Construct a new HeadPoseStream
     *
     * \param sense refers to the HeadPoseTracker providing updates
     * \param listener refers to the HeadPoseListener receiving updates
     *
     * Called by the HeadPoseTracker when requested for an HeadPoseListener through an openHeadPoseStream call.
     */
    HeadPoseStream(HeadPoseTracker* sense, HeadPoseListener* listener);
    ~HeadPoseStream();

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
