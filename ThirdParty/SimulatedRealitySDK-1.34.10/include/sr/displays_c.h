/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#ifndef DISPLAYS_C_H
#define DISPLAYS_C_H

#include "sr/core_c.h"

typedef void* SR_display;
typedef SR_display SR_screen;

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

#if defined(__GNUC__) || defined(__GNUG__)
#define DEPRECATED(X) __attribute__((deprecated))
#elif defined(_MSC_VER)
#define DEPRECATED(X) __declspec(deprecated(X))
#else
#define DEPRECATED(X)
#endif

/*!
 * \brief Creates a functional Display instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_Display ( void* ) which is the address of the C++ SR::Display implementation.
 *  It can be used to provide display data.
 *
 * The display class reads the current display parameters and makes calls to get those parameters
 * available.
 * 
 * This function is deprecated. Please use the createDisplay function instead.
 *
 * \ingroup API_C
 */
SRAPI DEPRECATED("This function is deprecated. Please use the createDisplay function instead") SR_display createScreen(SRContext context);

/*!
 * \brief Creates a functional Display instance
 *
 * \param context is the environment in which created senses are kept track of
 * \return SR_Display ( void* ) which is the address of the C++ SR::Display implementation.
 *  It can be used to provide display data.
 *
 * The display class reads the current display parameters and makes calls to get those parameters
 * available.
 *
 * \ingroup API_C
 */
SRAPI SR_display createDisplay(SRContext context);

/*!
 * \brief Returns resolution height of display instance as seen by user.
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const int ) Display resolution height.
 *
 * \ingroup API_C
 */
SRAPI const int getResolutionHeight(SR_display display);

/*!
 * \brief Returns resolution width of display instance as seen by user.
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const int ) Display resolution width.
 *
 * \ingroup API_C
 */
SRAPI const int getResolutionWidth(SR_display display);

/*!
 * \brief Returns physical resolution height of display instance.
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const int ) Display physical resolution height.
 *
 * \ingroup API_C
 */
SRAPI const int getPhysicalResolutionHeight(SR_display display);

/*!
 * \brief Returns physical resolution width of display instance.
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const int ) Display physical resolution width.
 *
 * \ingroup API_C
 */
SRAPI const int getPhysicalResolutionWidth(SR_display display);

/*!
 * \brief Returns physical display height in cm.
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const float ) Display physical height.
 *
 * \ingroup API_C
 */
SRAPI const float getPhysicalSizeHeight(SR_display display);

/*!
 * \brief Returns physical display width in cm.
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const float ) Display physical width.
 *
 * \ingroup API_C
 */
SRAPI const float getPhysicalSizeWidth(SR_display display);

/*!
 * \brief Returns distance between pixels in cm.
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const float ) Distance between pixels in display.
 *
 * \ingroup API_C
 */
SRAPI const float getDotPitch(SR_display display);

/*!
 * \brief Returns the recommended view texture width
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const int ) Recommended view texture width in pixels.
 *
 * \ingroup API_C
 */
SRAPI const int getRecommendedViewsTextureWidth(SR_display display);

/*!
 * \brief Returns the recommended view texture height
 *
 * \param display ( SR::Display* ) is the display object created through the C API.
 * \return ( const int ) Recommended view texture height in pixels.
 *
 * \ingroup API_C
 */
SRAPI const int getRecommendedViewsTextureHeight(SR_display display);

typedef void* SR_switchableLensHint;

/**
 * \brief Creates a functional SwitchableLensHints instance
 * 
 * Objects created via this function are managed by SR::SRContext and should not be explicitly deleted by 
 * the user. Instead, these objects are automatically cleaned up when SR::SRContext is destroyed. Calling 
 * delete on such objects directly will result in a crash.
 *
 * \param context is the environment in which created senses are kept track of
 * \return pointer to instance of underlying SR::SwitchableLensHint
 *
 * \ingroup API_C
 */
SRAPI SR_switchableLensHint createSwitchableLensHint(SRContext context);

/*
* \brief Checks whether the Lens is currently enabled
* 
* \returns a boolean representing whether the Lens is currently enabled
* \ingroup API_C
*/
SRAPI bool isLensEnabled(SR_switchableLensHint lensHint);

/*
* \brief This function returns true if any of already connected applications enabled the lense, otherwise returns false.
*
* \returns a boolean representing whether any applications have explicitly indicated that they want the lens to be on.
* \ingroup API_C
*/
SRAPI bool isLensEnabledByPreference(SR_switchableLensHint lensHint);

/**
 * Expresses preference to enable the lens such that it affects the light transmitted through it
 *
 * \ingroup API_C
 */
SRAPI void lensEnableHint(SR_switchableLensHint lensHint);

/**
 * Expresses preference to disable the lens to minimize the effect on the light transmitted through it
 *
 * \ingroup API_C
 */
SRAPI void lensDisableHint(SR_switchableLensHint lensHint);

/**
 * \brief Cleans up underlying object instances used to facilitate indicating lens switch preferance
 *
 * \param lensHint ( void* ) provided by the createSwitchableLensHint function.
 *
 * \ingroup API_C
 */
SRAPI void deleteSwitchableLensHint(SR_switchableLensHint lensHint);

#undef SRAPI

#endif // DISPLAYS_C_H
