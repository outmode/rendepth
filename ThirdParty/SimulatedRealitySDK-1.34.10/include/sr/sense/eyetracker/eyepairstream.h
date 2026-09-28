/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "eyepair.h"
#include "eyepairlistener.h"
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

class EyeTracker; //forward declaration

/**
 * \brief Stream of SR_eyePair objects
 *
 * Connects an EyeTracker Sense to a EyePairListener object.
 *
 * \ingroup EyeTracker API
 */
class DIMENCOSR_API EyePairStream {
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
    using Frame = SR_eyePair;

    /**
     * \brief Construct a new EyePairStream
     *
     * \param sense refers to the EyeTracker providing updates
     * \param listener refers to the EyePairListener receiving updates
     *
     * Called by the EyeTracker when requested for an EyePairListener through an openEyePairStream call.
     */
    EyePairStream(EyeTracker* sense, EyePairListener* listener);
    ~EyePairStream();

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
    void update(Frame frame);
};

}

#undef DIMENCOSR_API
