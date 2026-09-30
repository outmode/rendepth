/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "dx11weaver.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityDirectX
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

#if defined(__GNUC__) || defined(__GNUG__)
#define DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define DEPRECATED __declspec(deprecated)
#else
#define DEPRECATED
#endif

/*!
 * \brief class for backwards compatibility of Weaving in DirectX 11 based SR applications
 *
 * \ingroup API
 */

namespace SR
{
     //DXWeaver is deprecated. Please use DX11Weaver instead.
    class DEPRECATED DIMENCOSR_API DXWeaver : public DX11Weaver
    {
    public:
        DXWeaver(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned width, unsigned height);

        ~DXWeaver();

        /*!
         * \deprecated Calling delete on the returned pointer causes crashing, developers should use the constructor of DX11Weaver directly
         *
         * \brief Constructs a class to be used for weaving an input image of a certain size
         * \param context to connect to
         * \param device interface used to create resources
         * \param deviceContext to be used for rendering commands
         * \param width of the side-by-side image to be weaved together
         * \param height of the side-by-side image to be weaved together
         * \returns pointer to new DXWeaver instance
         */
        static DXWeaver* create(SR::SRContext& context, ID3D11Device* device, ID3D11DeviceContext* deviceContext, unsigned int width, unsigned int height);

        ID3D11RenderTargetView* getFrameBuffer();
        bool canWeave();
        void weave(unsigned width, unsigned height);
    };
}

#undef DIMENCOSR_API
