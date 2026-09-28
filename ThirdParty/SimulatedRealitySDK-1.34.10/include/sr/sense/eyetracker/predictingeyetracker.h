/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "sr/sense/eyetracker/eyetracker.h"
#include "sr/sense/eyetracker/eyepairlistener.h"

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
 * \brief Sense class which provides predictive eye tracking functionality
 *
 * \ingroup DownstreamInterface EyeTracker API
 */

class DIMENCOSR_API PredictingEyeTracker : public EyeTracker, public EyePairListener {
    class Impl;
    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(suppress: 4251)
    std::unique_ptr<Impl> pimpl;

public:
    /*!
     * \brief Returns a class of PredictingEyeTracker
     *
     * Only one PredictingEyeTracker can be created per context, if one has already been created a pointer to that one is returned.
     * If one had not been created yet, a new one will be added to the context.
     *
     * \param context to contain the PredictingEyeTracker
     */
    static PredictingEyeTracker* create(SRContext& context);

    /*!
     * \brief Constructs a PredictingEyeTracker
     */
    PredictingEyeTracker(EyeTracker* rawEyeTracker);

    /*!
     * \brief Predict for a certain latency, output an SR_eyePair and trigger stream output
     *
     * Listeners will receive a new SR_eyePair after predict is called
     *
     * \param uint64_t latency in microseconds to predict for
     * \param SR_eyePair to output
     */
    void predict(uint64_t latency, SR_eyePair& output);

    /*!
     * \brief Predict for a certain latency and trigger stream output
     *
     * Listeners will receive a new SR_eyePair after predict is called
     *
     * \param uint64_t latency in microseconds to predict for
     */
    void predict(uint64_t latency);

    /*!
     * \brief Set the delay from the moment the eyetracker loses the user to the moment the eye position for both eyes will start moving to the default position.
     * \brief The default position of the eyes is (0, 100, 600). As it is the same for the left and right eye, this effectively causes a transition from 3D to 2D. However, the lens will remain on and eyetracking will remain active, continuously looking for a new user to track.
     *
     * \param uint64_t delay in milliseconds
     */
    void setFaceLostDelay(uint64_t delay);

    // Inherited via EyeTracker
    virtual std::string getName() override;
    virtual std::string getDescription() override;
    virtual void start() override;
    virtual void stop() override;
    virtual std::shared_ptr<EyePairStream> openEyePairStream(EyePairListener* listener) override;
    virtual void streamClosed(EyePairStream* stream) override;

    // Inherited via EyePairListener

    /*!
     * \brief Receives SR_eyePair to filter and use in predictions
     *
     * \param frame of incoming SR_eyePair
     */
    virtual void accept(const SR_eyePair& frame) override;
};

}

#undef DIMENCOSR_API
