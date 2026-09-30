/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#ifndef CORE_C_H
#define CORE_C_H

#include "sr/sense/system/systemevent.h"

typedef void* SRContext;

#ifdef WIN32
#ifdef __cplusplus
#   ifdef COMPILING_DLL_SimulatedRealityCore
#     define SRAPI extern "C" __declspec(dllexport)
#   else
#     define SRAPI extern "C" __declspec(dllimport)
#   endif
#else
#   ifdef COMPILING_DLL_SimulatedRealityCore
#     error Trying to compile SimulatedRealityCore.dll using a non-C++ compiler! Use a C++ compiler instead!
#   else
#     define SRAPI __declspec(dllimport)
#   endif
#endif
#else
#   define SRAPI
#endif

/**
 * \defgroup API_C
 * \brief Application Programming Interface to be used by C SR applications
 */

/**
 * \brief Construct an instance of SRContext, the environment in which created senses are kept track of
 *
 * \return a new instance of SRContext
 *
 * \ingroup API_C
 */
SRAPI SRContext newSRContext();

/**
 * \brief Construct an instance of SRContext, the environment in which created senses are kept track of
 * 
 * \param lensPreference is the initial lens preference. If it is set to false and there are no other applications which prefer the lens to be turned on, it will start in off state.
 *
 * \return a new instance of SRContext
 *
 * \ingroup API_C
 */
SRAPI SRContext newSRContextLensPreference(bool lensPreference);

/**
 * \brief Initialize all senses
 *
 * \param context is provided from newSRContext
 *
 * Call *after* all objects and senses have been added.
 *
 * \ingroup API_C
 */
SRAPI void initializeSRContext(SRContext context);

/**
 * \brief Delete and clean up the instance of SRContext
 *
 * \param context is provided from newSRContext
 *
 * \ingroup API_C
 */
SRAPI void deleteSRContext(SRContext context);

typedef void* SR_systemSense;
typedef void* SR_systemEventListener;

/**
 * \brief Creates a functional SystemSense instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_systemSense ( void* ) which is the address of the C++ SR::SystemSense implementation. It can be used to provide streams of system event data.
 *
 * \ingroup API_C
 */
SRAPI SR_systemSense createSystemSense(SRContext context);

/**
 * \brief Add a new callback function to listen to a specific systemSense
 *
 * \param systemSense is the address of the C++ SR::SystemSense implementation to connect with. It is provided by the createSystemSense function.
 * \param acceptSystemEventCallback is a function pointer to a callback function which will be called when new SR_systemEvent data is available.
 * \return SR_systemEventListener ( void* ) which should be used to clean up the underlying objects used to facilitate the hand event update callbacks.
 *
 * \ingroup API_C
 */
SRAPI SR_systemEventListener createSystemEventListener(SR_systemSense systemSense, void(*acceptSystemEventCallback)(SR_systemEvent));

/**
 * \brief Cleans up underlying object instances used to facilitate system event update callbacks.
 *
 * \param systemEventListener ( void* ) provided by the createSystemEventListener function.
 *
 * \ingroup API_C
 */
SRAPI void deleteSystemEventListener(SR_systemEventListener systemEventListener);

#undef SRAPI

#endif // CORE_C_H
