/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include <string>
#include <chrono>
#include <thread>
#include <mutex>

#include "transformation.h"

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

//Forward declaration
class SRContext;

/**
 * \brief Class of objects dealing with @link Stream Streams @endlink of data
 *
 * A Sense can either collect data about the real world or communicate data about the virtual world.
 * This two-way street brings the two worlds closer together and allows real-time interaction.
 *
 * \ingroup DownstreamInterface Core API
 */
class DIMENCOSR_API Sense {
    friend class SRContext;

protected:
    //Defines the position and orientation of the Sense with respect to a fixed space, often related to the display
    Transformation calibration;

public:
    /// Sets a Transformation object to represent the sense's calibration with respect to the display
    void setCalibration(Transformation calibration);

    /// Gets callibration
    Transformation getCalibration();

    /// Ensures the proper destruction of the sense implementation
    virtual ~Sense() {};

    /// Return the name of the sense
    virtual std::string getName() = 0;

    /// Return a description of the sense
    virtual std::string getDescription() = 0;

    /// Start continuous updates, default behaviour is defined in the updater() function
    virtual void start() = 0;

    /// Stop continuous updates
    virtual void stop() = 0;
};

}

#undef DIMENCOSR_API
