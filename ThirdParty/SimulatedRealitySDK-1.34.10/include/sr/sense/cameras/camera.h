/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "sr/sense/core/sense.h"

#include "videostream.h"
#include "cameradescriptor.h"
#include "sr/sense/core/transformation.h"
#include "sr/management/srcontext.h"

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
 * \defgroup Camera
 * \brief Classes that enable applications to get access to video input
 */

/**
 * Type of function used to list @link SR_cameraDescriptor SR_cameraDescriptors @endlink
 */
typedef std::vector<SR_cameraDescriptor>(CameraListFunction)();

/**
 * Type of function used to create a Camera implementation using a SR_cameraDescriptor and registering it to the SRContext
 */
typedef Camera* (CameraCreateImplementationFunction)(SRContext&, SR_cameraDescriptor);

/**
 * \brief Vector of functions to list camera descriptors of a specific type
 *
 * Construct On First Use Idiom3
 *
 * \return Reference to a static std::vector<CameraListFunction*>
 */
DIMENCOSR_API std::vector<CameraListFunction*>& cameraListImplementationFunctions();

/**
 * \brief Map of camera type to functions to create cameras of that type
 *
 * Construct On First Use Idiom
 *
 * \return Reference to a static std::map<std::string, CameraCreateImplementationFunction*>
 */
DIMENCOSR_API std::map<std::string, CameraCreateImplementationFunction*>& cameraCreateImplementationFunctions();

/**
 * \brief Class of Sense dealing with video data
 *
 * Extentions of Camera can either provide input to the system or output in the user
 *
 * \ingroup DownstreamInterface Camera API
 */
class DIMENCOSR_API Camera : public Sense {

public:
    /**
     * \brief Class describing a physical camera
     */
    class DIMENCOSR_API Descriptor {
    public:
        SR_cameraDescriptor info;

        /**
         * \brief Construct a Camera::Descriptor with a struct combining a camera type and serial number
         *
         * \param SR_cameraDescriptor containing a camera type identifier and a serial number
         */
        Descriptor(SR_cameraDescriptor info) : info{ info.serialNumber, info.cameraTypeLength, info.cameraType } {
        }

        /**
         * \brief Operator allowing implicit casting to SR_cameraDescriptor
         */
        operator SR_cameraDescriptor() {
            return info;
        }

        /**
         * \brief Operator allowing implicit casting to std::string
         */
        operator std::string() {
            return std::string(info.cameraType, info.cameraTypeLength) + std::to_string(info.serialNumber);
        }
    };

    /**
     * \brief Static function to make a Camera implementation available through the Camera::listDescriptors and Camera::create functions
     *
     * \param listFunction is one of the functions that will be called when Camera::listDescriptors is called
     * \param createFunction is one of the functions that can be called when Camera::create is called for a camera of the right type
     * \param cameraType indicates when this createFunction should be used
     */
    static void addImplementation(CameraListFunction* listFunction, CameraCreateImplementationFunction* createFunction, std::string cameraType);

    /**
     * \brief List camera identifiers
     *
     * \return list of camera identifiers available to the SR system
     */
    static std::vector<SR_cameraDescriptor> listDescriptors();

    /**
     * \brief Create a camera with a given identifier
     *
     * \param context is the environment in which created senses are kept track of
     * \param cameraDescriptor indicates what type of Camera implementation should be constructed
     * \return Camera* which can provide streams of video data
     */
    static Camera* create(SRContext &context, Camera::Descriptor cameraDescriptor);

    /**
     * \brief Create a camera
     *
     * \param context is the environment in which created senses are kept track of
     * \return Camera* which can provide streams of video data
     */
    static Camera* create(SRContext &context);

    //Destruct streams
    virtual ~Camera();

    /**
     * \brief Get descriptor to identify physical Camera
     *
     * \return descriptor to identify physical Camera
     */
    virtual Camera::Descriptor getDescriptor() = 0;

    /**
     * \brief Get the number of available streams
     *
     * Camera modules (with multiple sensors) can be represented as a Camera implementation with multiple streams.
     * This can be helpful when the frames are related, which would be the case with calibrated stereo cameras offering depth information.
     *
     * \return positive integer representing the number of available video streams.
     */
    virtual const unsigned int getStreamCount() = 0;

    /**
     * \brief Creates a VideoStream for \p listener to be connected to
     *
     * \param listener will receive video updates from now on
     * \return @link VideoStream std::shared_ptr<VideoStream> @endlink to be used to close the stream correctly when it is no longer required
     */
    virtual std::shared_ptr<VideoStream> openVideoStream(VideoListener* listener) = 0;

    /**
     * \brief Closes a specific VideoStream
     *
     * \param stream will no longer supply video updates
     */
    virtual void streamClosed(VideoStream* stream) = 0;
};

}

#undef DIMENCOSR_API
