/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include <mutex>

#include "sr/sense/cameras/camera.h"
#include "sr/utility/exception.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityCameras
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {

/**
 * \brief Class of Exception which indicates the CameraController can not be used at this point in time
 *
 * Also occurs when the CameraController is already controlled by some other (part of the) application.
 *
 * \ingroup Camera API
 */
class DIMENCOSR_API CameraControllerUnavailableException : public SR::Exception {
public:
    /**
     * \brief Construct a new CameraControllerUnavailableException
     *
     * \param identifier indicates which Camera is unavailable
     */
    CameraControllerUnavailableException(std::string identifier);
};

//forward declaration
class UniqueCameraController;

/**
 * \brief Type of function used for gaining control of camera sensor parameters
 */
typedef UniqueCameraController(CameraControlFunction)(Camera*);

/**
 * \brief Map of camera type to functions to create cameras of that type
 *
 * Construct On First Use Idiom
 *
 * \return Reference to a static std::map<std::string, CameraCreateImplementationFunction*>
 */
DIMENCOSR_API std::map<std::string, CameraControlFunction*>& cameraControlFunctions();

/**
 * \brief Interface defining camera functions to control sensor parameters
 *
 * \ingroup Camera API
 */
class DIMENCOSR_API CameraController {
public:
    /**
     * \brief Static function to make a CameraController implementation available through the CameraController::control function
     *
     * \param controlFunction is one of the functions that can be called when CameraController::control is called for a camera of the right type
     * \param cameraType indicates when this controlFunction should be used
     */
    static void addImplementation(CameraControlFunction* controlFunction, std::string cameraType);

    /**
     * \brief Attempt to gain control of camera sensor
     *
     * \param camera refers to a Camera instance that has already been created
     * \throw CameraControllerUnavailableException if the camera is already being controlled or if the camera can not be controlled
     * \return non-copyable UniqueCameraController object that provides access to CameraController interface until it goes out of scope
     */
    static UniqueCameraController control(Camera* camera);

    /**
     * \brief Set camera shutter / exposure time in seconds
     *
     * A higher shutter time will produce brighter images but may increase latency.
     * The effectiveness of changing the shutter time is also dependent on the aperture of the camera.
     *
     * \param shutterTime in seconds
     */
    virtual void setShuttertime(float shutterTime) = 0;

    /**
     * \brief Set camera gain
     *
     * A higher gain will produce brighter images but may affect image quality.
     * Values produced by the sensor are multiplied by a factor derived from this gain value.
     * Implementations may vary.
     *
     */
    virtual void setGain(float gain) = 0;

    /**
     * \brief Get current shutter / exposure time in seconds
     *
     * A higher shutter time will produce brighter images but may increase latency.
     * The effectiveness of changing the shutter time is also dependent on the aperture of the camera.
     *
     * \return current shutter / exposure time in seconds
     */
    virtual float getShuttertime() = 0;

    /**
     * \brief Get current camera gain factor
     *
     * A higher gain will produce brighter images but may affect image quality.
     * Values produced by the sensor are multiplied by a factor derived from this gain value
     * Implementations may vary.
     *
     * \return current camera gain factor
     */
    virtual float getGain() = 0;
};

/**
 * \brief Non-copyable class of CameraController interface that can only be constructed once per Camera (Camera::Descriptor)
 *
 * \ingroup Camera API
 */
class DIMENCOSR_API UniqueCameraController : public CameraController {
    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(suppress: 4251)
    static std::map<std::string, std::mutex> mutex;

    Camera::Descriptor descriptor;
    CameraController* controller;

    UniqueCameraController(const UniqueCameraController&) = delete; // non construction-copyable
    UniqueCameraController& operator=(const UniqueCameraController&) = delete; // non copyable

public:
    UniqueCameraController(UniqueCameraController && other) : descriptor(other.descriptor), controller(other.controller){};
    UniqueCameraController& operator=(UniqueCameraController && other);

    /**
     * \brief Construct UniqueCameraController to reserve memory
     */
    UniqueCameraController();

    /**
     * \brief Construct instance of UniqueCameraController of that can only be created for a specific Camera if another UniqueCameraController instance does not exist for it
     *
     * \throw CameraControllerUnavailableException if the Camera is already being controlled through another UniqueCameraController
     */
    UniqueCameraController(Camera* camera, CameraController* controller);

    /**
     * \brief Destructs UniqueCameraController instance and allow a new one to be acquired
     */
    ~UniqueCameraController();

    // Inherited via CameraController
    virtual void setShuttertime(float shutterTime);
    virtual void setGain(float gain);
    virtual float getShuttertime();
    virtual float getGain();
};

}

#undef DIMENCOSR_API
