/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include <vector>

#include "opencv2/opencv.hpp"

#include "sr/sense/core/transformation.h"

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
 * \brief Class representing both real and virtual objects as nodes in the World tree
 *
 * \ingroup UpstreamInterface Core API
 */
class DIMENCOSR_API WorldObject {
protected:
    WorldObject *parent; 

    Transformation parentRelation;
    
    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(suppress: 4251)
    cv::Vec3d relativePosition;

public:
    /**
     * Construct a WorldObject
     * \param parent indicates that the object is related to another WorldObject
     */
    WorldObject(WorldObject *parent);

    /**
     * Construct a root WorldObject to represent an object in the real world that is not dependent on another for it's position
     *
     * Alternatively, the position of this object may not be relevant
     * Previously Worldobject(nullptr) was used to imply this
     */
    WorldObject();

    cv::Vec3d getRelativePosition();

    cv::Vec3d getAbsolutePosition();
};

}

#undef DIMENCOSR_API
