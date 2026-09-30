/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

// Windows include files.
#ifdef WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#else
#    define HWND void*
#endif
#include "sr/utility/commoninterfaces.h"

namespace SR
{


     /*!
     * \brief Weaver base class for all graphics APIs.
     *
     * \ingroup API
     */
    class IWeaverBase1: virtual IQueryInterface
    {
    public:

        /*!
         * \brief Sets the new window handle of the window that will present the backbuffer.
         * Note: the backbuffer size should match the client size of the window or the user will see artifacts.
         * \param handle Handle to the window that will present the backbuffer
         */
        virtual void setWindowHandle(HWND handle) = 0;

        /*!
         * \brief Performs weaving from the provided stereo texture to the current render-target
         *        using currently setup viewport and scissor rect
         */
        virtual void weave() = 0;

        /*!
         * \brief Enables late latching. Note that late latching requires applications to call weave() once per frame.
         * When enabled, the user's position will be updated for frames still in flight, reducing weaving latency
         * This feature might not be available for all graphics API's
         * \param enable Whether to turn late latching on
         */
        virtual void enableLateLatching(bool enable) = 0;

        /*!
         * \brief Determines if late latching is currently enabled.
         * \returns Whether late latching is enabled.
         */
        virtual bool isLateLatchingEnabled() const = 0;


        /*!
        * \brief Sets whether to apply shader sRGB/Linear color space conversions during weaving.
        * When input is already linear or set for hardware conversion, set read to false.
        * When output is treated as linear (set for hardware conversion), set write to false.
        * \param read Convert from sRGB to Linear color space after sampling the input texture.
        * \param write Convert from Linear to sRGB color space before writing into output.
        */
        virtual void setShaderSRGBConversion(bool read, bool write) = 0;

        /*!
         * \brief Set the latency to match the expected duration of the full rendering pipeline in number of frames.
         * The latency in time is calculated using these number of frames based on the refresh rate of the monitor that the application is running on, this will be dynamically updated when the window changes monitor.
         * For this it requires the weaver to be given a valid window handle of the running application.
         *
         * The eye positions should be predicted to the timepoint at which the frame is visible to the user
         * Internally the prediction is already taking care of all other latency, only the rendering pipeline latency is application dependent
         * A low latency app would have 1 framebuffer latency, the generated frame will be presented at next v-sync)
         * When using v-sync, the driver adds at least 1 buffer latency, and maybe the windows display manager also adds a buffer latency.
         *
         * \param latencyInFrames The expected number of frames before presenting the current generated frame to the user.
         */
        virtual void setLatencyInFrames(uint64_t latencyInFrames) = 0;

        /*!
         * \brief Set the latency to match the expected duration of the full rendering pipeline
         *
         * The eye positions should be predicted to the timepoint at which the frame is visible to the user
         * Internally the prediction is already taking care of all other latency, only the rendering pipeline latency is application dependent
         * A low latency app would have 1 framebuffer latency, so 16666 microseconds (the generated frame will be presented at next v-sync)
         * When using v-sync, the driver adds at least 1 buffer latency, and maybe the windows display manager also adds a buffer latency.
         * Typically, the latency is n * 1000*1000/framerate microseconds
         *
         * \param latency The latency from the moment when weave() is called until presenting the current frame to the user, in microseconds
         */
        virtual void setLatency(uint64_t latency) = 0;

        /*!
         * \brief Get the latency that is used by the weaver.
         * If setLatency has been called, the value set by that function will be returned.
         * If setLatencyInFrames has been called, the latency will be calculated based on the refresh rate of the monitor and the application.
         * By default setLatencyInFrames is used when no latency is explicitly set.
         *
         * \returns The latency in microseconds
         */
        virtual uint64_t getLatency() const = 0;

        /*!
         * \brief Gets the tracked eye positions. Intended to be used by virtual cameras
         * Note: the latency set by setLatency() is also used for predicting these eye positions.
         * Note: The returned positions might not be fully compensating for all latencies.
         *
         * \param left 3D vector of the left eye position.
         * \param right 3D vector of the right eye position.
         */
        virtual void getPredictedEyePositions(float left[3], float right[3]) = 0;

    protected:
        IWeaverBase1() = default;
        virtual ~IWeaverBase1() = default;

        IWeaverBase1(const IWeaverBase1&) = delete;
        IWeaverBase1& operator=(const IWeaverBase1&) = delete;
        IWeaverBase1(IWeaverBase1&&) = delete;
        IWeaverBase1& operator=(IWeaverBase1&&) = delete;
    };
}
