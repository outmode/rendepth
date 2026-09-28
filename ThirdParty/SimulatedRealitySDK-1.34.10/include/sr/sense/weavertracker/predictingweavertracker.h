/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "sr/sense/weavertracker/weavertracker.h"
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
 * \brief Sense class which provides predictive weaver position tracking functionality
 *
 * \ingroup DownstreamInterface WeaverTracker API
 */
class DIMENCOSR_API PredictingWeaverTracker : public WeaverTracker, public EyePairListener {
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
     * \brief Returns a class of PredictingWeaverTracker
     *
     * Only one PredictingWeaverTracker can be created per context, if one has already been created a pointer to that one is returned.
     * If one had not been created yet, a new one will be added to the context.
     *
     * \param context to contain the PredictingWeaverTracker
     */
    static PredictingWeaverTracker* create(SRContext& context);

    /*!
     * \brief Constructs a PredictingWeaverTracker
     */
    PredictingWeaverTracker(EyeTracker* rawEyeTracker);

    ~PredictingWeaverTracker();

    /*!
     * \brief Predict for a certain latency and trigger stream output
     * Should not be used directly, for weaving use DX11Weaver/DX12Weaver/GLWeaver instead
     *
     * Listeners will receive a new SR_weaverPosition after predict is called
     *
     * \param uint64_t latency in microseconds to predict for
     * \param SR_weaverPosition to output
     */
    void predict(uint64_t latency, SR_weaverPosition& output);

    /*!
     * \brief Predict for a certain latency and trigger stream output
     * Should not be used directly, for weaving use DX11Weaver/DX12Weaver/GLWeaver instead
     *
     * Listeners will receive a new SR_weaverPosition after predict is called
     *
     * \param uint64_t latency in microseconds to predict for
     */
    void predict(uint64_t latency);

    // Inherited via WeaverTracker
    virtual std::string getName() override;
    virtual std::string getDescription() override;
    virtual void start() override;
    virtual void stop() override;
    virtual std::shared_ptr<WeaverPositionStream> openWeaverPositionStream(WeaverPositionListener* listener) override;
    virtual void streamClosed(WeaverPositionStream* stream) override;

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
