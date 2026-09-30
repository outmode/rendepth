/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#ifndef FACETRACKERS_C_H
#define FACETRACKERS_C_H

#include "sr/core_c.h"

#include "sr/sense/eyetracker/eyepair.h"
#include "sr/sense/headtracker/head.h"
#include "sr/sense/weavertracker/weaverposition.h"

typedef void* SR_eyeTracker;
typedef void* SR_eyePairListener;

typedef void* SR_headTracker;
typedef void* SR_headListener;

typedef void* SR_predictingWeaverTracker;
typedef void* SR_weaverPositionListener;

#ifdef WIN32
#ifdef __cplusplus
#   ifdef COMPILING_DLL_SimulatedRealityFaceTrackers
#     define SRAPI extern "C" __declspec(dllexport)
#   else
#     define SRAPI extern "C" __declspec(dllimport)
#   endif
#else
#   ifdef COMPILING_DLL_SimulatedRealityFaceTrackers
#     error Trying to compile SimulatedRealityFaceTrackers.dll using a non-C++ compiler! Use a C++ compiler instead!
#   else
#     define SRAPI __declspec(dllimport)
#   endif
#endif
#else
#   define SRAPI
#endif

/**
 * \brief Creates a functional EyeTracker instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_eyeTracker ( void* ) which is the address of the C++ SR::EyeTracker implementation. It can be used to provide streams of eye position data
 *
 * The EyeTracker class is abstract and requires a device specific implementation to be used in applications.
 * This function constructs an EyeTracker suitable for use in your application on the current device.
 *
 * \ingroup API_C
 */
SRAPI SR_eyeTracker createEyeTracker(SRContext context);

/**
 * \brief Create a new callback function to listen to a specific eyetracker
 *
 * \param eyeTracker is the address of the C++ SR::EyeTracker implementation to connect with. It is provided by the createEyeTracker function.
 * \param acceptEyePairCallback is a function pointer to a callback function which will be called when new SR_eyePair data is available.
 * \return SR_eyePairListener ( void* ) which should be used to clean up the underlying objects used to facilitate the eye position update callbacks.
 *
 * \ingroup API_C
 */
SRAPI SR_eyePairListener createEyePairListener(SR_eyeTracker eyeTracker, void (*acceptEyePairCallback)(SR_eyePair));

/**
 * \brief Cleans up underlying object instances used to facilitate eye position update callbacks.
 *
 * \param eyePairListener ( void* ) provided by the createEyePairListener function.
 *
 * \ingroup API_C
 */
SRAPI void deleteEyePairListener(SR_eyePairListener eyePairListener);

/**
 * \brief Creates a functional Headtracker instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_headtracker ( void* ) which is the address of the C++ SR::Headtracker implementation. It can be used to provide streams of head position data
 *
 * The Headtracker class is abstract and requires a device specific implementation to be used in applications.
 * This function constructs an HeadTracker suitable for use in your application on the current device.
 *
 * \ingroup API_C
 */
SRAPI SR_headTracker createHeadTracker(SRContext context);

/**
 * \brief Create a new callback function to listen to a specific headtracker
 *
 * \param headtracker is the address of the C++ SR::HeadTracker implementation to connect with. It is provided by the createHeadTracker function.
 * \param acceptHeadCallback is a function pointer to a callback function which will be called when new SR_head data is available.
 * \return SR_headListener ( void* ) which should be used to clean up the underlying objects used to facilitate the head position update callbacks.
 *
 * \ingroup API_C
 */
SRAPI SR_headListener createHeadListener(SR_headTracker headTracker, void (*acceptHeadCallback)(SR_head));

/**
 * \brief Cleans up underlying object instances used to facilitate head position update callbacks.
 *
 * \param headListener ( void* ) provided by the creaeteHeadListener function.
 *
 * \ingroup API_C
 */
SRAPI void deleteHeadListener(SR_headListener headListener);

/**
 * \brief Creates a functional predicting PredictingWeaverTracker instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_weaverTracker ( void* ) which is the address of the C++ SR::PredictingWeaverTracker implementation. It can be used to provide streams of weaver position data
 * after every predict call
 *
 * The PredictingWeaverTracker class is abstract and requires a device specific implementation to be used in applications.
 * This function constructs an PredictingWeaverTracker suitable for use in your application on the current device.
 *
 * \ingroup API_C
 */
SRAPI SR_predictingWeaverTracker createPredictingWeaverTracker(SRContext context);

/**
 * \brief Create a new callback function to listen to a specific PredictingWeaverTracker
 *
 * \param predictingWeaverTracker is the address of the C++ SR::EyeTracker implementation to connect with. It is provided by the createEyeTracker function.
 * \param acceptWeaverPositionCallback is a function pointer to a callback function which will be called when new SR_weaverPosition data is available.
 * \return SR_weaverPositionListener ( void* ) which should be used to clean up the underlying objects used to facilitate the weaver position update callbacks.
 *
 * \ingroup API_C
 */
SRAPI SR_weaverPositionListener createWeaverPositionListener(SR_predictingWeaverTracker predictingWeaverTracker, void (*acceptWeaverPositionCallback)(SR_weaverPosition));

/**
 * \brief Cleans up underlying object instances used to facilitate weaver position update callbacks.
 *
 * \param weaverPositionListener ( void* ) provided by the createWeaverPositionListener function.
 *
 * \ingroup API_C
 */
SRAPI void deleteWeaverPositionListener(SR_weaverPositionListener weaverPositionListener);

/*!
 * \brief Predict for a certain latency and trigger stream output
 * Should not be used directly, for weaving use DX11Weaver/DX12Weaver/GLWeaver instead
 *
 * Listeners will receive a new SR_weaverPosition after predict is called
 *
 * \param uint64_t latency in microseconds to predict for
 * \param SR_weaverPosition to output
 *
 * \ingroup API_C
 */
SRAPI void predictWeaverPositionWithOutput(SR_predictingWeaverTracker predictingWeaverTracker, uint64_t latency, SR_weaverPosition* output);

/*!
 * \brief Predict for a certain latency and trigger stream output
 * Should not be used directly, for weaving use DX11Weaver/DX12Weaver/GLWeaver instead
 *
 * Listeners will receive a new SR_weaverPosition after predict is called
 *
 * \param uint64_t latency in microseconds to predict for
 *
 * \ingroup API_C
 */
SRAPI void predictWeaverPosition(SR_predictingWeaverTracker predictingWeaverTracker, uint64_t latency);

#undef SRAPI

#endif // FACETRACKERS_C_H
