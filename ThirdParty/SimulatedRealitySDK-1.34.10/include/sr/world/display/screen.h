/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

/**
 * \Parsing of the parameters is done using iniParser class
 */

#include "sr/world/worldobject.h"
#include "sr/management/srcontext.h"

#include <fstream>

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityDisplays
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {

/*!
 * \brief Class of WorldObject representing the screen in real space
 *
 * \ingroup UpstreamInterface API
 * The origin of both worlds is at the center of the screen
 */
class [[deprecated("Use Display class instead.")]] DIMENCOSR_API Screen : public WorldObject{
    const int resolutionHeight = 1440; // height in pixel count of the resolution that is accepted by the screen
    const int resolutionWidth = 2560; // width in pixel count of the resolution that is accepted by the screen

    // physical resolution <-> native resolution
    const int physicalResolutionHeight = 1440; // height in pixel count of the native resolution of the physical screen itself
    const int physicalResolutionWidth = 2560; // width in pixel count of the native resolution of the physical screen itself

    const float dotPitch_cm = 0.0233f; // Pixel size in cm = dot pitch (why they are the same) 0.00909
    const float physicalSizeHeight = 40; // height of the screen in cm
    const float physicalSizeWidth = 70; // width of the screen in cm
public:
    /**
     * \brief Creates an instance of a Screen class
     *
     * \param context is the environment in which created senses are kept track of
     *
     * \return A pointer to the Screen instance
     */
    static Screen* create(SRContext &context);

    /**
     * \brief Screen Class constructor
     *
     * \param resolutionHeight is the vertical pixel count that is accepted by the screen
     * \param resolutionWidth is the horizontal pixel count that is accepted by the screen
     * \param physicalResolutionHeight is the vertical pixel count of the native resolution of the physical screen itself
     * \param physicalResolutionWidth is the horizontal pixel count of the native resolution of the physical screen itself
     * \param pixelSize is the size of a pixel in cm
     */
    Screen(int resolutionHeight, int resolutionWidth, int physicalResolutionHeight, int physicalResolutionWidth, float pixelSize);

    /**
     * \brief Get the vertical resolution that is accepted by the display
     *
     * \return The vertical resolution that is accepted by the display
     */
    const int getResolutionHeight();

    /**
     * \brief Get the horizontal resolution that is accepted by the display
     *
     * \return The horizontal resolution that is accepted by the display
     */
    const int getResolutionWidth();

    /**
     * \brief Get the vertical native resolution of the physical screen itself
     *
     * \return The vertical native resolution of the screen as an integer
     */
    const int getPhysicalResolutionHeight();

    /**
     * \brief Get the horizontal native resolution of the physical screen itself
     *
     * \return The horizontal native resolution of the screen as an integer
     */
    const int getPhysicalResolutionWidth();

    /**
     * \brief Get the physical height of the screen
     *
     * \return The physical height of the screen as a float number in cm
     */
    const float getPhysicalSizeHeight();

    /**
     * \brief Get the physical width of the screen
     *
     * \return The physical width of the screen as a float number in cm
     */
    const float getPhysicalSizeWidth();

    /**
     * \brief Get the dot pitch (pixel size)
     *
     * \return The dot pitch (pixel size) as a float number in cm
     */
    const float getDotPitch();
};

}

#undef DIMENCOSR_API
