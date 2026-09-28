/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#ifndef HANDTRACKERS_C_H
#define HANDTRACKERS_C_H

#include "sr/core_c.h"

#include "sr/sense/handtracker/handpose.h"
#include "sr/sense/handtracker/handevent.h"

typedef void* SR_handTracker;
typedef void* SR_handPoseListener;
typedef void* SR_handEventListener;

#ifdef WIN32
#ifdef __cplusplus
#   ifdef COMPILING_DLL_SimulatedRealityHandTrackers
#     define SRAPI extern "C" __declspec(dllexport)
#   else
#     define SRAPI extern "C" __declspec(dllimport)
#   endif
#else
#   ifdef COMPILING_DLL_SimulatedRealityHandTrackers
#     error Trying to compile SimulatedRealityHandTrackers.dll using a non-C++ compiler! Use a C++ compiler instead!
#   else
#     define SRAPI __declspec(dllimport)
#   endif
#endif
#else
#   define SRAPI
#endif

/**
 * \brief Creates a functional HandTracker instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_handTracker ( void* ) which is the address of the C++ SR::HandTracker implementation. It can be used to provide streams of hand data
 *
 * The HandTracker class is abstract and requires a device specific implementation to be used in applications.
 * This function constructs an HandTracker suitable for use in your application on the current device.
 *
 * \ingroup API_C
 */
SRAPI SR_handTracker createHandTracker(SRContext context);

/**
 * \brief Create a new callback function to listen to a specific handtracker
 *
 * \param handTracker is the address of the C++ SR::HandTracker implementation to connect with. It is provided by the createHandTracker function.
 * \param acceptHandPoseCallback is a function pointer to a callback function which will be called when new SR_handPose data is available.
 * \return SR_handPoseListener ( void* ) which should be used to clean up the underlying objects used to facilitate the hand pose update callbacks.
 *
 * \ingroup API_C
 */
SRAPI SR_handPoseListener createHandPoseListener(SR_handTracker handTracker, void (*acceptHandPoseCallback)(SR_handPose));

/**
 * \brief Cleans up underlying object instances used to facilitate hand pose update callbacks.
 *
 * \param handPoseListener ( void* ) provided by the createHandPoseListener function.
 *
 * \ingroup API_C
 */
SRAPI void deleteHandPoseListener(SR_handPoseListener handPoseListener);

/**
 * \brief Create a new callback function to listen to a specific handtracker
 *
 * \param handTracker is the address of the C++ SR::HandTracker implementation to connect with. It is provided by the createHandTracker function.
 * \param acceptHandEventCallback is a function pointer to a callback function which will be called when new SR_handEvent data is available.
 * \return SR_handEventListener ( void* ) which should be used to clean up the underlying objects used to facilitate the hand event update callbacks.
 *
 * \ingroup API_C
 */
SRAPI SR_handEventListener createHandEventListener(SR_handTracker handTracker, void (*acceptHandEventCallback)(SR_handEvent));

/**
 * \brief Cleans up underlying object instances used to facilitate hand event update callbacks.
 *
 * \param handEventListener ( void* ) provided by the createHandEventListener function.
 *
 * \ingroup API_C
 */
SRAPI void deleteHandEventListener(SR_handEventListener handEventListener);

#undef SRAPI

#endif // HANDTRACKERS_C_H
