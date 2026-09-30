/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#include <map>

#include "sr/sense/core/sense.h"
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
 * \brief Class representing the configuration of a single Sense in the SRContext
 *
 * \ingroup API
 * \deprecated Senses should handle their own configuration.
 */
class DIMENCOSR_API SenseConfiguration {
    Transformation transformation;

public:
    SenseConfiguration(cv::Mat scaling, cv::Mat rotation, cv::Mat translation);
    SenseConfiguration(cv::Vec3d rotation, cv::Vec3d translation);
    SenseConfiguration();

    SenseConfiguration(const SenseConfiguration& other);
    SenseConfiguration& operator=(const SenseConfiguration& other);

    Transformation getCalibration();
};

/// Class representing the configuration of the SRContext
class DIMENCOSR_API Configuration {
    // Mapping from Sense names to calibrations, any names that match the constructable senses will actually be usable and calibrated
    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(suppress: 4251)
    std::map<std::string, SenseConfiguration> senseConfigurations;

    // The default height and width associated with instances of Screen
    float displayHeight;
    float displayWidth;

    void load(const char* path);

public:
    Configuration(std::string path);

    Configuration();

    void calibrate(Sense* sense);

    float getDisplayHeight();

    float getDisplayWidth();
};

}

#undef DIMENCOSR_API
