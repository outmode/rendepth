/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#ifndef CAMERAS_C_H
#define CAMERAS_C_H

#include "sr/core_c.h"

#include "sr/sense/cameras/videoframe.h"
#include "sr/sense/cameras/cameradescriptor.h"

typedef void* SR_camera;
typedef void* SR_videoListener;

#ifdef WIN32
#ifdef __cplusplus
#   ifdef COMPILING_DLL_SimulatedRealityCameras
#     define SRAPI extern "C" __declspec(dllexport)
#   else
#     define SRAPI extern "C" __declspec(dllimport)
#   endif
#else
#   ifdef COMPILING_DLL_SimulatedRealityCameras
#     error Trying to compile SimulatedRealityCameras.dll using a non-C++ compiler! Use a C++ compiler instead!
#   else
#     define SRAPI __declspec(dllimport)
#   endif
#endif
#else
#   define SRAPI
#endif

/*!
 * \brief Creates a functional Camera instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_camera ( void* ) which is the address of the C++ SR::Camera implementation. It can be used to provide streams of video data
 *
 * The Camera class is abstract and requires a device specific implementation to be used in applications.
 * This function constructs an Camera suitable for use in your application on the current device.
 *
 * \ingroup API_C
 */
SRAPI SR_camera createCamera(SRContext context);

/*!
 * \brief Creates a Camera instance to provide feedback about the Eyetracker
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_camera ( void* ) which is the address of the C++ SR::Camera implementation coupled to the Eyetracker application
 *
 * \ingroup API_C
 */
SRAPI SR_camera createCameraByDescriptor(SRContext context, SR_cameraDescriptor descriptor);

/*!
 * \brief Create a new callback function to listen to a specific camera
 *
 * \param camera is the address of the C++ SR::Camera implementation to connect with. It is provided by the createCamera function.
 * \param acceptVideoCallback is a function pointer to a callback function which will be called when new SR_videoFrame data is available.
 * \return SR_videoListener ( void* ) which should be used to clean up the underlying objects used to facilitate the video update callbacks.
 *
 * \ingroup API_C
 */
SRAPI SR_videoListener createVideoListener(SR_camera camera, void (*acceptVideoCallback)(SR_videoFrame));

/*!
 * \brief Cleans up underlying object instances used to facilitate video update callbacks.
 *
 * \param videoListener ( void* ) provided by the createVideoListener function.
 *
 * \ingroup API_C
 */
SRAPI void deleteVideoListener(SR_videoListener videoListener);

#undef SRAPI

#endif  // CAMERAS_C_H
