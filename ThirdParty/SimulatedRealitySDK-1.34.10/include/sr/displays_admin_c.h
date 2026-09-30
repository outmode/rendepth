/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#ifndef DISPLAYS_ADMIN_C_H
#define DISPLAYS_ADMIN_C_H

#include "sr/core_c.h"

typedef void* SR_screen;

#ifdef WIN32
#ifdef __cplusplus
#   ifdef COMPILING_DLL_SimulatedRealityDisplays
#     define SRAPI extern "C" __declspec(dllexport)
#   else
#     define SRAPI extern "C" __declspec(dllimport)
#   endif
#else
#   ifdef COMPILING_DLL_SimulatedRealityDisplays
#     error Trying to compile SimulatedRealityDisplays.dll using a non-C++ compiler! Use a C++ compiler instead!
#   else
#     define SRAPI __declspec(dllimport)
#   endif
#endif
#else
#   define SRAPI
#endif

typedef void* SR_switchableLensHintAdmin;

/**
 * \brief Creates a functional SwitchableLensHintAdmin instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return pointer to instance of underlying SR::SwitchableLensHintAdmin
 *
 * \ingroup ADMIN_C
 */
SRAPI SR_switchableLensHintAdmin createSwitchableLensHintAdmin(SRContext context);

/**
 * Expresses preference to enable the lens such that it affects the light transmitted through it
 *
 * \ingroup ADMIN_C
 */
SRAPI void lensEnableHintAdmin(SR_switchableLensHintAdmin lensAdmin);

/**
 * Expresses preference to disable the lens to minimize the effect on the light transmitted through it
 *
 * \ingroup ADMIN_C
 */
SRAPI void lensDisableHintAdmin(SR_switchableLensHintAdmin lensAdmin);

/**
 * Allows switching the lens such that it affects the light transmitted through it
 *
 * \ingroup ADMIN_C
 */
SRAPI void enableDefaultSwitching(SR_switchableLensHintAdmin lensAdmin);

/**
 * Disable the lens to minimize the effect on the light transmitted through it
 *
 * \ingroup ADMIN_C
 */
SRAPI void disableByForce(SR_switchableLensHintAdmin lensAdmin);

/**
 * \brief Cleans up underlying object instances used to facilitate indicating lens switch admin features
 *
 * \param lensAdmin ( void* ) provided by the createSwitchableLensHintAdmin function.
 *
 * \ingroup ADMIN_C
 */
SRAPI void deleteSwitchableLensHintAdmin(SR_switchableLensHintAdmin lensAdmin);

/**
 * \brief Get the Version info of the switchable lens
 *
 * \param lensAdmin ( void* ) provided by the createSwitchableLensHintAdmin function.
 *
 * \ingroup ADMIN_C
 */
SRAPI char* getVersion(SR_switchableLensHintAdmin lensAdmin);

/**
 * \brief Get the Serial Number of the switchable lens
 *
 * \param lensAdmin ( void* ) provided by the createSwitchableLensHintAdmin function.
 *
 * \ingroup ADMIN_C
 */
SRAPI char* getSerialNumber(SR_switchableLensHintAdmin lensAdmin);

/**
 * \brief Get the Additional Serial Numbers of the switchable lens
 *
 * \param lensAdmin ( void* ) provided by the createSwitchableLensHintAdmin function.
 *
 * \param index refers to the number of serial number which can be between 1-4, otherwise it returns ""
 *
 * \ingroup ADMIN_C
 */
SRAPI char* getAdditionalSerialNumber(SR_switchableLensHintAdmin lensAdmin, unsigned char index);

#ifdef WIN32
/**
 * \brief Detecting if any screen attached or detached. This blocking function will run until new attach or detach of screen happens
 *
 * \param serialNumbers (const char*) get screens' serial numbers, the argument should have 1+255*32 bytes of memory. First byte is number of devices, max 255 devices, 32 bytes each
 * \param timeBetweenChecks is an integer expressing the number of milliseconds to wait between reading attempts from shared memory
 * \param active is a reference to a boolean that reflects whether the function should keep waiting, if it is ever set to false the function will stop blocking
 *
 * \ingroup API_C
 */
SRAPI void getNewDeviceSerialNumbers(const char* serialNumbers, unsigned int timeBetweenChecks, bool* active);
#endif

#undef SRAPI

#endif // DISPLAYS_ADMIN_C_H
