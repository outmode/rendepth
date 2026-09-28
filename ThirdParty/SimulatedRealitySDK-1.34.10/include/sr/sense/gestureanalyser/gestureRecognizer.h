/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

// The gesture recognizer functionality is only available for 64-bit Windows applications
#ifdef _WIN64

#include "sr/sense/handtracker/handpose.h"
#include "opencv2/opencv.hpp"
#include <fstream>
#include <cstring>
#include "sr/utility/exception.h"
#include "sr/utility/logging.h"
#include <memory>

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
     * \brief Class of Exception which indicates the Tensorflow gesture classifier can not be initialized correctly
     * \ingroup GestureAnalyser API
     */
    class DIMENCOSR_API GestureRecognizerException : public SR::Exception {
    public:
        /**
         * \brief Construct a new GestureRecognizerException
         * \param specificErrorMessage indicates the detailed error message
         */
        GestureRecognizerException(std::string specificErrorMessage);
    };

    /**
     * \brief Enumeration containing gesture name
     * \ingroup GestureAnalyser API,
     */
    enum SR_gestureName {
        FIST,
        POINT,
        PINCH,
        FLAT,
        PINCHGRABRELEASE
    };

    /**
     * \brief Enumeration containing gesture classification model
     * \ingroup GestureAnalyser API
     */
    enum SR_gestureClassificationModel {
        NN4,
        NN5
    };

    /**
     * \brief SR_gestureData is a structure containing enumeration with SR_gestureName and float with probability of the gesture prediction
     * \ingroup GestureAnalyser API
     */
    struct SR_gestureData {
        SR_gestureName gestureName;
        float prob;
    };

    /**
     * \brief Class that recognizes hand state from hand pose represented by SR_handPose
     * This class uses neural network (NN) to predict hand state from a hand model represented by 21 hand landmarks. NN is built
     * using Tensorflow library.
     * The previously trained NN model is loaded and further applied to the input hand model to predict hand state.
     *
     * \ingroup GestureAnalyser API
     */
    class DIMENCOSR_API GestureRecognizer {
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
        /**
         * \brief construct GestureRecognizer from the pre-trained tensorflow model
         * \param nnModel could be either SR_gestureClassificationModel::NN4 or SR_gestureClassificationModel::NN5. If it's NN5, the predicted gesture is among POINT, PINCH, FLAT, FIST and PINCHGRABRELEASE(NO GESTURE).
         * If it's NN4, the predicted gesture is among POINT, PINCH, FLAT and FIST
         * \throw std::runtime_error when classificationmodel.pb cannot be found in ProgramData/Simulated Reality/Server/Resources/model/gesture-detection/classificationmodel. If not there, please reinstall the Simulated Reality package
         * \throw GestureRecognizerException when unable to import the graph provided by classificationmodel.pd.
         * \throw GestureRecognizerException when unable to create a session on the graph created from classificationmodel.pd
         */
        GestureRecognizer(SR_gestureClassificationModel nnModel);

        /**
         * \brief Cleans up GestureRecognizer implementation
         */
        ~GestureRecognizer();

        /**
         * \brief predict() predicts hand gesture from SR_handpose
         * \throw GestureRecognizerException when prediction fails due to failure when running operations on the model
         * \return SR_gestureData structure that contains enumeration with gesture name and prediction probability
         */
        SR_gestureData predict(SR_handPose pose);
    };
}

#undef DIMENCOSR_API

#endif
