/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "opencv2/opencv.hpp"
#include "sr/types.h"

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
 * \brief Class of objects defining the relation between different coordinate systems
 *
 * \ingroup DownstreamInterface Core API
 */
class DIMENCOSR_API Transformation {
private:
    bool verifyRotation();

    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(push)
#pragma warning(disable: 4251)
    cv::Mat scaling;
    cv::Mat rotation;
    cv::Mat translation;

    //Combined for efficiency
    cv::Mat transformation;
    cv::Mat untransformation;
#pragma warning(pop)

public:
    Transformation(cv::Matx31d scaling, cv::Matx33d rotation, cv::Matx31d translation);
    Transformation(cv::Matx33d rotation, cv::Matx31d translation);
    Transformation();
    Transformation(const Transformation& other);
    Transformation& operator=(const Transformation& other);

    cv::Mat getRotation();
    cv::Mat getTranslation();
    cv::Mat getMatrix();

    cv::Mat apply(cv::Mat points);
    cv::Mat apply(SR_point3d* points, size_t size);
    cv::Mat reverse(cv::Mat points);
};

}

#undef DIMENCOSR_API
