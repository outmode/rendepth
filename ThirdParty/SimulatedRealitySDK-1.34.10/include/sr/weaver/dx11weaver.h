/*!
 * Copyright (C) 2025 Leia, Inc.
*/

#pragma once
#include <memory>
#include <d3d11.h>
#include "sr/management/srcontext.h"
#include "sr/weaver/WeaverTypes.h"
#include "sr/weaver/IWeaverBase.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityDirectX
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR
{
    /*!
     * \brief class to be used for weaving in DirectX 11 applications
     *
     * \ingroup API
     */
    class IDX11Weaver1 : public virtual IDestroyable, public virtual IQueryInterface, public virtual IWeaverBase1
    {
    public:

        /*!
         * \brief Sets the stereo view texture that will be used for weaving.
         * \param texture Pointer to the texture
         * \param width Width of the texture
         * \param height Height of the texture
         * \param format Pixel format of the texture
         */
        virtual void setInputViewTexture(ID3D11ShaderResourceView* texture, int width, int height, DXGI_FORMAT format) = 0;

        /*!
         * \brief Sets the D3D11 device context to be used by the weaver.
         */
        virtual void setContext(ID3D11DeviceContext* context) = 0;

    protected:
        IDX11Weaver1() = default;
        virtual ~IDX11Weaver1() = 0;

        IDX11Weaver1(const IDX11Weaver1&) = delete;
        IDX11Weaver1& operator=(const IDX11Weaver1&) = delete;
        IDX11Weaver1(IDX11Weaver1&&) = delete;
        IDX11Weaver1& operator=(IDX11Weaver1&&) = delete;
    };

    /*!
     * \brief Creates a new DirectX 11 weaver.
     *
     * \param Context to connect to
     * \param d3d11Context Pointer to 3D11 immediate context
     * \param window Handle to (optional) output window
     * \param weaver Pointer to created weaver, if successful
     * \returns Success or error code
     *
     * \ingroup API
     */
    DIMENCOSR_API WeaverErrorCode CreateDX11Weaver(SRContext* context, ID3D11DeviceContext* d3d11Context, HWND window, IDX11Weaver1** weaver);



    // The classes below are deprecated

    class DIMENCOSR_API DX11WeaverBase {
    protected:
        class Impl;
        /*!
         * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
         * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
         * Candidate for deprecation
         */
#pragma warning(suppress: 4251)
        std::unique_ptr<Impl> pimpl;

    public:
        DX11WeaverBase();
        ~DX11WeaverBase();

        /*!
         * \brief Returns the buffer that will be used to create a weaved image. This buffer expects a side-by-side image.
         * When a frame buffer is provided by calling setInputFrameBuffer(), the internally created buffer will be released.
         *
         * The buffer identifier can be bound using `OMSetRenderTargets` to start rendering to it.
         * Rendering to the buffer is identical to normal rendering and often starts with calling `ClearRenderTargetView`.
         * \returns ID3D11RenderTargetView identifying a buffer generated with `CreateShaderResourceView`
         */
        ID3D11RenderTargetView* getFrameBuffer();

        /*!
         * \brief Sets the buffer that will be used to create a weaved image. This will release the internally created frame buffer.
         * \param frameBuffer Frame buffer to set
         */
        void setInputFrameBuffer(ID3D11ShaderResourceView* frameBuffer);

        /*!
         * \brief Sets the device context for the weaver to use
         * \param context Device Context to be used for rendering commands
         */
        void setContext(ID3D11DeviceContext* context);

        /*!
         * \brief Sets the window handle of the application window. If the weaver was created using a deprecated constructor, setting window handle has no effect.
         * \param window Handle of the application window
         */
        void setWindowHandle(HWND handle);

        /*!
         * \brief Enables late latching. Note that late latching requires applications to call weave() once per frame, and does not work with deferred contexts.
         */
        void enableLateLatching(bool enable);

        /*!
         * \brief Determines if late latching is enabled.
         */
        bool isLateLatchingEnabled() const;

        /*!
         * \brief Sets the weaving contrast.
         */
        void setContrast(float contrast);

        /*!
         * \brief Gets the current weaving contrast.
         */
        float getContrast() const;

        /*!
         * \brief Sets whether to apply shader sRGB<->Linear color space conversions to the input framebuffer during weaving.
         * \param read Convert from sRGB to Linear color space before weaving.
         * \param write Convert from Linear to sRGB color space after weaving.
         */
        
        void setShaderSRGBConversion(bool read, bool write);

        /*!
         * \brief Sets the anti-crosstalk mode.
         */
        void setACTMode(WeaverACTMode mode);

        /*!
         * \brief Gets the anti-crosstalk mode.
         */
        WeaverACTMode getACTMode() const;

        /*!
         * \brief Sets the anti-crosstalk factor.
         */
        void setCrosstalkStaticFactor(float factor);

        /*!
         * \brief Gets the anti-crosstalk factor.
         */
        float getCrosstalkStaticFactor() const;

        /*!
         * \brief Sets the anti-crosstalk dynamic factor.
         */
        void setCrosstalkDynamicFactor(float factor);

        /*!
         * \brief Gets the anti-crosstalk dynamic factor.
         */
        float getCrosstalkDynamicFactor() const;

        /*!
        *  \deprecated
         * \brief Used to determine if software weaving is possible on this device
         * \returns bool indicating whether weaving can be done. Returns true when software weaving can be performed by DX11Weaver. When false is returned, you can output side-by-side manually or let DX11Weaver handle this.
         */
        [[deprecated("Use the canWeave(...) functions that accept parameters")]]
        bool canWeave();

        /*!
         * \brief Used to determine if software weaving is possible for certain size and visibility to the currently bound framebuffer
         * \param width of the image to be rendered to the bound framebuffer
         * \param height of the image to be rendered to the bound framebuffer
         * \throw std::runtime_error if the window handle (HWND) becomes invalid during the execution of canWeave
         * \returns bool indicating whether weaving can be done. Returns true when software weaving can be performed by DX11Weaver. When false is returned, you can output side-by-side manually or let DX11Weaver handle this.
         */
        bool canWeave(unsigned int width, unsigned int height);

        /*!
         * \brief Used to determine if software weaving is possible for certain size and visibility to the currently bound framebuffer
         * \param width of the image to be rendered to the bound framebuffer
         * \param height of the image to be rendered to the bound framebuffer
         * \param xOffset of the image to be rendered to the bound framebuffer
         * \param yOffset of the image to be rendered to the bound framebuffer
         * \throw std::runtime_error if the window handle (HWND) becomes invalid during the execution of canWeave
         * \returns bool indicating whether weaving can be done. Returns true when software weaving can be performed by DX11Weaver. When false is returned, you can output side-by-side manually or let DX11Weaver handle this.
         */
        bool canWeave(unsigned int width, unsigned int height, unsigned int xOffset, unsigned int yOffset);

        /*!
         * \brief Can be called to render a weaved image of a certain size to the currently bound framebuffer
         * A framebuffer must be set before calling this function.
         * \param width of the image to be rendered to the bound framebuffer
         * \param height of the image to be rendered to the bound framebuffer
         */
        void weave(unsigned int width, unsigned int height);

        /*!
         * \brief Can be called to render a weaved image of a certain size to the currently bound framebuffer
         * A framebuffer must be set before calling this function.
         * \param width of the image to be rendered to the bound framebuffer
         * \param height of the image to be rendered to the bound framebuffer
         * \param xOffset of the image to be rendered to the bound framebuffer
         * \param yOffset of the image to be rendered to the bound framebuffer
         */
        void weave(unsigned int width, unsigned int height, unsigned int xOffset, unsigned int yOffset);
    };

    class [[deprecated("This class is deprecated, please use IDX11Weaver instead.")]] DIMENCOSR_API DX11Weaver : public DX11WeaverBase {
    public:
        /*!
         * \deprecated
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         */
        [[deprecated("For best performance use the PredictingDX11Weaver interface")]]
        DX11Weaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height);

        /*!
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         * \param window Handle of the application window
         */
        [[deprecated("For best performance use the PredictingDX11Weaver interface")]]
        DX11Weaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height, HWND window);

        /*!
         * \deprecated
         * \brief Constructs a class to be used for weaving an input image of a certain size and format
         * \param context to connect to
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         * \param format of the side-by-side image to be weaved together
         */
        [[deprecated("For best performance use the PredictingDX11Weaver interface")]]
        DX11Weaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height, DXGI_FORMAT format);

        /*!
         * \deprecated
         * \brief Constructs a class to be used for weaving an input image of a certain size and format
         * \param context to connect to
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         * \param format of the side-by-side image to be weaved together
         * \param window handle for windowed weaving support
         */
        [[deprecated("For best performance use the PredictingDX11Weaver interface")]]
        DX11Weaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height, DXGI_FORMAT format, HWND window);

        /*!
         * \brief Handles proper destruction of all weaver related classes and buffers
         */
        ~DX11Weaver();
    };

    class [[deprecated("This class is deprecated, please use IDX11Weaver instead.")]] DIMENCOSR_API PredictingDX11Weaver : public DX11WeaverBase {
    public:
        /*!
         * \deprecated
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         */
        [[deprecated("Use non-deprecated constructors")]]
        PredictingDX11Weaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height);

        /*!
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         * \param window Handle of the application window
         */
        PredictingDX11Weaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height, HWND window);

        /*!
         * \deprecated
         * \brief Constructs a class to be used for weaving an input image of a certain size and format
         * \param context to connect to
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         * \param format of the side-by-side image to be weaved together
         */
        [[deprecated("Use non-deprecated constructors")]]
        PredictingDX11Weaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height, DXGI_FORMAT format);

        /*!
         * \deprecated
         * \brief Constructs a class to be used for weaving an input image of a certain size and format
         * \param context to connect to
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         * \param format of the side-by-side image to be weaved together
         * \param window handle for windowed weaving support
         */
        [[deprecated("Use non-deprecated constructors")]]
        PredictingDX11Weaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height, DXGI_FORMAT format, HWND window);

        /*!
         * \brief Handles proper destruction of all weaver related classes and buffers
         */
        ~PredictingDX11Weaver();

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
        void setLatency(uint64_t latency);

        /*!
         * \brief Get the latency that is used by the weaver.
         * If setLatency has been called, the value set by that function will be returned.
         * If setLatencyInFrames has been called, the latency will be calculated based on the refresh rate of the monitor and the application.
         * By default setLatencyInFrames is used when no latency is explicitly set.
         *
         * \returns The latency in microseconds
         */
        uint64_t getLatency();

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
        void setLatencyInFrames(uint64_t latencyInFrames);
    };
}

#undef DIMENCOSR_API
