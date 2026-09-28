/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <memory>
#include <d3d12.h>
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
     * \brief class to be used for weaving in DirectX 12 applications
     *
     * \ingroup API
     */
    class IDX12Weaver1 : public virtual IDestroyable, public virtual IQueryInterface, public virtual IWeaverBase1
    {
    public:

        // Note: It is required to set the command list, the viewport and the scissor rect before calling weave() or it will throw an exception
        //virtual void weave() = 0;

        /*!
         * \brief Sets the stereo view texture that will be used for weaving.
         * \param texture Pointer to the texture
         * \param width Width of the texture
         * \param height Height of the texture
         * \param format Pixel format of the texture
         */
        virtual void setInputViewTexture(ID3D12Resource* texture, int width, int height, DXGI_FORMAT format) = 0;

        /*!
         * \brief Sets the format of the buffer to which the weaver renders to.
         */
        virtual void setOutputFormat(DXGI_FORMAT format) = 0;

        /*!
         * \brief Set the commandlist to add the weaving commands to. This is required before calling weave().
         */
        virtual void setCommandList(ID3D12GraphicsCommandList* commandList) = 0;

        /*!
         * \brief Set the viewport to use for the weaving commands. This is required before calling weave().
         */
        virtual void setViewport(D3D12_VIEWPORT viewport) = 0;

        /*!
         * \brief Set the scissor rect to use for the weaving commands. This is required before calling weave().
         */
        virtual void setScissorRect(D3D12_RECT scissorRect) = 0;

    protected:
        IDX12Weaver1() = default;
        virtual ~IDX12Weaver1() = 0;

        IDX12Weaver1(const IDX12Weaver1&) = delete;
        IDX12Weaver1& operator=(const IDX12Weaver1&) = delete;
        IDX12Weaver1(IDX12Weaver1&&) = delete;
        IDX12Weaver1& operator=(IDX12Weaver1&&) = delete;
    };

    /*!
     * \brief Creates a new DirectX 12 weaver.
     *
     * \param Context to connect to
     * \param d3d12Device D3D12 device interface
     * \param window Handle to (optional) output window
     * \param weaver Pointer to created weaver, if successful
     * \returns Success or error code
     * 
     * \ingroup API
     */
    DIMENCOSR_API WeaverErrorCode CreateDX12Weaver(SRContext* context, ID3D12Device* d3d12Device, HWND window, IDX12Weaver1** weaver);



    // The classes below are deprecated

    class DIMENCOSR_API DX12WeaverBase {
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
        DX12WeaverBase();
        ~DX12WeaverBase();

        /*!
         * \brief Returns the input buffer of the weaver.
         * When weave(...) is called on this object, this buffer should contain a side-by-side view that will be used as input for the weaver.
         *
         * The buffer identifier can be bound using `OMSetRenderTargets` to start rendering to it.
         * Rendering to the buffer is identical to normal rendering and often starts with calling `ClearRenderTargetView`.
         * \returns Pointer to the frame buffer resource that is currently set as input buffer.
         */
        ID3D12Resource* getFrameBuffer();

        /*!
         * \brief Sets the input buffer of the weaver.
         * When weave(...) is called on this object, this buffer should contain a side-by-side view that will be used as input for the weaver.
         *
         * \param frameBuffer The frame buffer resource that will be used as input buffer after this function call. A reference to this resource will be maintained by the object until this function is called again or until the object is destroyed.
         * \throw std::exception if `framebuffer` is a texture resource created with a typeless format
         */
        void setInputFrameBuffer(ID3D12Resource* frameBuffer);

        /*!
         * \brief Sets the input buffer of the weaver.
         *  This overload of the function should be used to specify the fully qualified format via `bufferViewFormat` when the `framebuffer` is a texture created with a typeless format 
         * When weave(...) is called on this object, this buffer should contain a side-by-side view that will be used as input for the weaver.
         *
         * \param frameBuffer The frame buffer resource that will be used as input buffer after this function call. A reference to this resource will be maintained by the object until this function is called again or until the object is destroyed.
         * \param bufferViewFormat The fully qualified shader resource view format for the input frame buffer. This specification is required for textures created with typeless format. It can also be used to cast an already specified fully qualified format to another.
         * \throw std::exception if `framebuffer` is a texture resource created with a typeless format and `bufferViewFormat`'s value is DXGI_FORMAT_UNKNOWN
         */
        void setInputFrameBuffer(ID3D12Resource* frameBuffer, DXGI_FORMAT bufferViewFormat);

        /*!
         * \brief Sets the output buffer of the weaver.
         * After weave(...) has been called on this object, the weaved image will be written to this buffer.
         *
         * \param frameBuffer The frame buffer resource that will be used as output buffer after this function call. No reference to the output framebuffer is kept by the weaver object.
         */
        void setOutputFrameBuffer(ID3D12Resource* frameBuffer);

        /*!
         * \brief Sets the command list for the weaver to use. Must be set before the weave() function can be called.
         * \param commandList To add the weaver rendering commands to
         */
        void setCommandList(ID3D12GraphicsCommandList* commandList);

        /*!
         * \brief Sets the window handle of the application window. If the weaver was created using a deprecated constructor, setting window handle has no effect.
         * \param window Handle of the application window
         */
        void setWindowHandle(HWND handle);

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
         * \deprecated
         * \brief Used to determine if software weaving is possible on this device.
         * Always returns false if the input or output buffer is not set.
         * \returns bool indicating whether weaving can be done. Returns true when software weaving can be performed by DX12Weaver. When false is returned, you can output side-by-side manually or let DX12Weaver handle this if the input and output buffers are set.
         */
        [[deprecated("Use the canWeave(...) functions that accept parameters")]]
        bool canWeave();

        /*!
         * \brief Used to determine if software weaving is possible for certain size and visibility to the currently bound framebuffer.
         * Always returns false if the input or output buffer is not set.
         * \param width of the image to be rendered to the bound framebuffer
         * \param height of the image to be rendered to the bound framebuffer
         * \throw std::runtime_error if the window handle (HWND) becomes invalid during the execution of canWeave
         * \returns bool indicating whether weaving can be done. Returns true when software weaving can be performed by DX12Weaver. When false is returned, you can output side-by-side manually or let DX12Weaver handle this if the input and output buffers are set.
         */
        bool canWeave(unsigned int width, unsigned int height);

        /*!
         * \brief Used to determine if software weaving is possible for certain size and visibility to the currently bound framebuffer.
         * Always returns false if the input or output buffer is not set.
         * \param width of the image to be rendered to the bound framebuffer
         * \param height of the image to be rendered to the bound framebuffer
         * \param xOffset of the image to be rendered to the bound framebuffer
         * \param yOffset of the image to be rendered to the bound framebuffer
         * \throw std::runtime_error if the window handle (HWND) becomes invalid during the execution of canWeave
         * \returns bool indicating whether weaving can be done. Returns true when software weaving can be performed by DX12Weaver. When false is returned, you can output side-by-side manually or let DX12Weaver handle this if the input and output buffers are set.
         */
        bool canWeave(unsigned int width, unsigned int height, unsigned int xOffset, unsigned int yOffset);

        /*!
         * \brief Can be called to render a weaved image of `inputFramebuffer` provided to `DX12Weaver::DX12Weaver(...)` (Must be D3D12_RESOURCE_STATE_UNORDERED_ACCESS) to the currently bound rendertarget (Must be D3D12_RESOURCE_STATE_RENDER_TARGET)
         *  A commandlist must be set before calling this function or weaving will not be executed.
         * \param width of the image to be rendered to the bound rendertarget
         * \param height of the image to be rendered to the bound rendertarget
         */
        void weave(unsigned int width, unsigned int height);

        /*!
         * \brief Can be called to render a weaved image of `inputFramebuffer` provided to `DX12Weaver::DX12Weaver(...)` (Must be D3D12_RESOURCE_STATE_UNORDERED_ACCESS) to the currently bound rendertarget (Must be D3D12_RESOURCE_STATE_RENDER_TARGET)
         *  A commandlist must be set before calling this function or weaving will not be executed.
         * \param width of the image to be rendered to the bound rendertarget
         * \param height of the image to be rendered to the bound rendertarget
         * \param xOffset of the image to be rendered to the bound rendertarget
         * \param yOffset of the image to be rendered to the bound rendertarget
         */
        void weave(unsigned int width, unsigned int height, unsigned int xOffset, unsigned int yOffset);

        /*!
        *  \deprecated
         * \brief Can be called to render a weaved image of `inputFramebuffer` provided to `DX12Weaver::DX12Weaver(...)` (Must be D3D12_RESOURCE_STATE_UNORDERED_ACCESS) to the currently bound rendertarget (Must be D3D12_RESOURCE_STATE_RENDER_TARGET)
         * \param commandList to add the weaver rendering commands to
         * \param width of the image to be rendered to the bound rendertarget
         * \param height of the image to be rendered to the bound rendertarget
         */
        [[deprecated("Please set the commandlist with setCommandList() and use non-deprecated weave function")]]
        void weave(ID3D12GraphicsCommandList* commandList, unsigned int width, unsigned int height);

        /*!
         * \deprecated
         * \brief Can be called to render a weaved image of `inputFramebuffer` provided to `DX12Weaver::DX12Weaver(...)` (Must be D3D12_RESOURCE_STATE_UNORDERED_ACCESS) to the currently bound rendertarget (Must be D3D12_RESOURCE_STATE_RENDER_TARGET)
         * \param commandList to add the weaver rendering commands to
         * \param width of the image to be rendered to the bound rendertarget
         * \param height of the image to be rendered to the bound rendertarget
         * \param xOffset of the image to be rendered to the bound rendertarget
         * \param yOffset of the image to be rendered to the bound rendertarget
         */
        [[deprecated("Please set the commandlist with setCommandList() and use non-deprecated weave function")]]
        void weave(ID3D12GraphicsCommandList* commandList, unsigned int width, unsigned int height, unsigned int xOffset, unsigned int yOffset);
    };

    class [[deprecated("This class is deprecated, please use IDX12Weaver instead.")]] DIMENCOSR_API DX12Weaver : public DX12WeaverBase {
    public:
        /*!
         * \deprecated
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param commandAllocator used for command list generation during setup
         * \param commandQueue used for command list execution during setup
         * \param inputFramebuffer A pointer to a frame buffer resource containing the side-by-side image to be weaved together. A reference to this resource will be maintained by the object until a different input buffer is set by calling setInputFrameBuffer or until the object is destroyed.
         * \param outputFramebuffer A pointer to a frame buffer resource where the output of the weaver will be written to (usually the backbuffer). No reference to the output framebuffer is kept by the weaver object.
         * \throw std::exception if `inputFramebuffer` is a texture resource created with a typeless format
         */
        [[deprecated("For best performance use the PredictingDX12Weaver interface")]]
        DX12Weaver(SR::SRContext& context, ID3D12Device* device, ID3D12CommandAllocator* commandAllocator, ID3D12CommandQueue* commandQueue, ID3D12Resource* inputFramebuffer, ID3D12Resource* outputFramebuffer);

        /*!
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param commandAllocator used for command list generation during setup
         * \param commandQueue used for command list execution during setup
         * \param inputFramebuffer A pointer to a frame buffer resource containing the side-by-side image to be weaved together. A reference to this resource will be maintained by the object until a different input buffer is set by calling setInputFrameBuffer or until the object is destroyed.
         * \param outputFramebuffer A pointer to a frame buffer resource where the output of the weaver will be written to (usually the backbuffer). No reference to the output framebuffer is kept by the weaver object.
         * \param window Handle of the application window
         * \throw std::exception if `inputFramebuffer` is a texture resource created with a typeless format
         */
        [[deprecated("For best performance use the PredictingDX12Weaver interface")]]
        DX12Weaver(SR::SRContext& context, ID3D12Device* device, ID3D12CommandAllocator* commandAllocator, ID3D12CommandQueue* commandQueue, ID3D12Resource* inputFramebuffer, ID3D12Resource* outputFramebuffer, HWND window);

        /*!
         * \brief Constructs a class to be used for weaving an input image of a certain size
         *  This constructor should be used to specify the fully qualified format via `inputBufferViewFormat` when the `inputFramebuffer` is a texture created with a typeless format 
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param commandAllocator used for command list generation during setup
         * \param commandQueue used for command list execution during setup
         * \param inputFramebuffer A pointer to a frame buffer resource containing the side-by-side image to be weaved together. A reference to this resource will be maintained by the object until a different input buffer is set by calling setInputFrameBuffer or until the object is destroyed.
         * \param outputFramebuffer A pointer to a frame buffer resource where the output of the weaver will be written to (usually the backbuffer). No reference to the output framebuffer is kept by the weaver object.
         * \param window Handle of the application window
         * \param inputBufferViewFormat The fully qualified shader resource view format for the input frame buffer. This specification is required for textures created with typeless format. It can also be used to cast an already specified fully qualified format to another.
         * \throw std::exception if `inputFramebuffer` is a texture resource created with a typeless format and `inputBufferViewFormat`'s value is DXGI_FORMAT_UNKNOWN
         */
        [[deprecated("For best performance use the PredictingDX12Weaver interface")]]
        DX12Weaver(SR::SRContext& context, ID3D12Device* device, ID3D12CommandAllocator* commandAllocator, ID3D12CommandQueue* commandQueue, ID3D12Resource* inputFramebuffer, ID3D12Resource* outputFramebuffer, HWND window, DXGI_FORMAT inputBufferViewFormat);

        /*!
         * \brief Handles proper destruction of all weaver related classes, buffers and references.
         */
        ~DX12Weaver();
    };

    class [[deprecated("This class is deprecated, please use IDX12Weaver instead.")]] DIMENCOSR_API PredictingDX12Weaver : public DX12WeaverBase {
    public:
        /*!
         * \deprecated
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param commandAllocator used for command list generation during setup
         * \param commandQueue used for command list execution during setup
         * \param inputFramebuffer A pointer to a frame buffer resource containing the side-by-side image to be weaved together. A reference to this resource will be maintained by the object until a different input buffer is set by calling setInputFrameBuffer or until the object is destroyed.
         * \param outputFramebuffer A pointer to a frame buffer resource where the output of the weaver will be written to (usually the backbuffer). No reference to the output framebuffer is kept by the weaver object.
         * \throw std::exception if `inputFramebuffer` is a texture resource created with a typeless format
         */
        [[deprecated("Use non-deprecated constructors")]]
        PredictingDX12Weaver(SR::SRContext& context, ID3D12Device* device, ID3D12CommandAllocator* commandAllocator, ID3D12CommandQueue* commandQueue, ID3D12Resource* inputFramebuffer, ID3D12Resource* outputFramebuffer);

        /*!
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param commandAllocator used for command list generation during setup
         * \param commandQueue used for command list execution during setup
         * \param inputFramebuffer A pointer to a frame buffer resource containing the side-by-side image to be weaved together. A reference to this resource will be maintained by the object until a different input buffer is set by calling setInputFrameBuffer or until the object is destroyed.
         * \param outputFramebuffer A pointer to a frame buffer resource where the output of the weaver will be written to (usually the backbuffer). No reference to the output framebuffer is kept by the weaver object.
         * \param window Handle of the application window
         * \throw std::exception if `inputFramebuffer` is a texture resource created with a typeless format
         */
        PredictingDX12Weaver(SR::SRContext& context, ID3D12Device* device, ID3D12CommandAllocator* commandAllocator, ID3D12CommandQueue* commandQueue, ID3D12Resource* inputFramebuffer, ID3D12Resource* outputFramebuffer, HWND window);

        /*!
         * \brief Constructs a class to be used for weaving an input image of a certain size
         *  This constructor should be used to specify the fully qualified format via `inputBufferViewFormat` when the `inputFramebuffer` is a texture created with a typeless format 
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param commandAllocator used for command list generation during setup
         * \param commandQueue used for command list execution during setup
         * \param inputFramebuffer A pointer to a frame buffer resource containing the side-by-side image to be weaved together. A reference to this resource will be maintained by the object until a different input buffer is set by calling setInputFrameBuffer or until the object is destroyed.
         * \param outputFramebuffer A pointer to a frame buffer resource where the output of the weaver will be written to (usually the backbuffer). No reference to the output framebuffer is kept by the weaver object.
         * \param window Handle of the application window
         * \param inputBufferViewFormat The fully qualified shader resource view format for the input frame buffer. This specification is required for textures created with typeless format. It can also be used to cast an already specified fully qualified format to another.
         * \throw std::exception if `inputFramebuffer` is a texture resource created with a typeless format and `inputBufferViewFormat`'s value is DXGI_FORMAT_UNKNOWN
         */
        PredictingDX12Weaver(SR::SRContext& context, ID3D12Device* device, ID3D12CommandAllocator* commandAllocator, ID3D12CommandQueue* commandQueue, ID3D12Resource* inputFramebuffer, ID3D12Resource* outputFramebuffer, HWND window, DXGI_FORMAT inputBufferViewFormat);

        /*!
         * \brief Constructs a class to be used for weaving an input image of a certain size
         *  This constructor should be used to specify the fully qualified format via `inputBufferViewFormat` when the `inputFramebuffer` is a texture created with a typeless format
         * \param context to connect to, needs to be valid for the lifetime of the weaver object.
         * \param device interface used to create resources
         * \param commandAllocator used for command list generation during setup
         * \param commandQueue used for command list execution during setup
         * \param inputFramebuffer A pointer to a frame buffer resource containing the side-by-side image to be weaved together. A reference to this resource will be maintained by the object until a different input buffer is set by calling setInputFrameBuffer or until the object is destroyed.
         * \param outputFramebuffer A pointer to a frame buffer resource where the output of the weaver will be written to (usually the backbuffer). No reference to the output framebuffer is kept by the weaver object.
         * \param window Handle of the application window
         * \param inputBufferViewFormat The fully qualified shader resource view format for the input frame buffer. This specification is required for textures created with typeless format. It can also be used to cast an already specified fully qualified format to another.
         * \param outputFramebufferViewFormat The fully qualified shader resource view format for the output frame buffer view.
         * \throw std::exception if `inputFramebuffer` is a texture resource created with a typeless format and `inputBufferViewFormat`'s value is DXGI_FORMAT_UNKNOWN
         */
        PredictingDX12Weaver(SR::SRContext& context, ID3D12Device* device, ID3D12CommandAllocator* commandAllocator, ID3D12CommandQueue* commandQueue, ID3D12Resource* inputFramebuffer, ID3D12Resource* outputFramebuffer, HWND window, DXGI_FORMAT inputBufferViewFormat, DXGI_FORMAT outputFramebufferViewFormat);

        /*!
         * \brief Handles proper destruction of all weaver related classes, buffers and references.
         */
        ~PredictingDX12Weaver();

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
