/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "sr/types.h"
#include "sr/management/srcontext.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityDisplays
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

/*!
 * \brief Type to avoid the direct use of HWND and the required inclusion of Windows.h in this file
 */
typedef void* SR_HWND;

namespace SR {

/*!
 * \brief Interface class to represent the application window in the SR system

 * \ingroup UpstreamInterface API
 */

class DIMENCOSR_API [[deprecated("Use class Window2 and window2.h")]]
    Window {
public:
    /*!
     * \brief Create a Window class compatible with the current operating system (Windows)
     * \param context is the context of the application
     * \param window is the handle to the window
     * \return pointer to SR::Window class to represent an application window in the SR system
     *
     * The pointer returned by this function should be deleted by the application when it wants the memory cleaned up
     */
    static Window* create(SRContext &context, SR_HWND window);

    /*!
     * \brief Get the handle used by the operating system associated with the window
     * \return a SR_HWND window handle
     *
     * The returned SR_HWND handle can be cast to HWND and used in Windows API functions
     */
    virtual SR_HWND getHandle() = 0;

    /*!
     * \brief Get the location of the window with respect to the primary display
     * \return a rectangle representing the window location in the full display configuration
     */
    virtual SR_recti getLocation() = 0;

    /**
     * \brief Determine whether a window is (partially) visible
     *
     * The provided offset and size of the weaving surface will be clipped to the window size.
     * Any checks about whether or not the window is occluded will be limited to the bounds of the window.
     * Windows spawned as children of the windowHandle will be ignored as they are considered part of the main weaving window.
     *
     * \param windowHandle is a handle to the window as used in windows API functions
     * \param xOffset to where the weaving surface starts (could be 0 for all the way on the left side of the window)
     * \param yOffset to where the weaving surface starts (could be 0 for all the way on the top side of the window)
     * \param width of the weaving surface
     * \param height of the weaving surface
     * \return true if the window is (partially) visible
     * \throw std::runtime_error if windowHandle is not a valid window handle (HWND)
     */
    virtual bool isWindowPartVisible(unsigned int xOffset, unsigned int yOffset, unsigned int width, unsigned int height) = 0;
};

}

#undef DIMENCOSR_API
