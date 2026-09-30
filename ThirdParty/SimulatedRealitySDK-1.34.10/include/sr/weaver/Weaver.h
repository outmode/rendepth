/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "WeaverTypes.h"
#include <string>
#include <vector>

#ifdef WIN32
#   ifdef COMPILING_DLL_DimencoWeaving
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

/*!
 * \brief Checks at compile time whether a class implements the all common weaver interface functions.
 * \param WEAVER_CLASS class identifier
 */
template <class T>
void isWeaverClass() {

    static_assert(std::is_member_function_pointer<decltype(static_cast<bool (T::*)(unsigned int, unsigned int)>(&T::canWeave))>::value,
        "This weaver class does not implement the canWeave(unsigned int, unsigned int) function of the weaver interface");
    static_assert(std::is_member_function_pointer<decltype(static_cast<bool (T::*)(unsigned int, unsigned int, unsigned int, unsigned int)>(&T::canWeave))>::value,
        "This weaver class does not implement the canWeave(unsigned int, unsigned int, unsigned int, unsigned int) function of the weaver interface");
    static_assert(std::is_member_function_pointer<decltype(static_cast<void (T::*)(unsigned int, unsigned int)>(&T::weave))>::value,
        "This weaver class does not implement the weave(unsigned int, unsigned int) function of the weaver interface");
    static_assert(std::is_member_function_pointer<decltype(static_cast<void (T::*)(unsigned int, unsigned int, unsigned int, unsigned int)>(&T::weave))>::value,
        "This weaver class does not implement the weave(unsigned int, unsigned int, unsigned int, unsigned int) function of the weaver interface");
}

namespace Dimenco {

class DIMENCOSR_API Weaver {

public:
    /**
     * @brief Set global shader parameters needed for weaving.
     * @param userPos Centered location between the left-and right eye.
     */
    static void SetGlobalParameters(FLOAT3 userPos);

    /**
     * @brief Set global shader parameters needed for weaving, using a different .ini file.
     * @param configPath Custom path for loading shader parameters.
     * @param userPos Centered location between the left-and right eye.
     */
    static void SetGlobalParameters(const char* configPath, FLOAT3 userPos);

    /**
     * @brief Reconfigures global weaver parameters.
     */
    static void ReconfigureWeaver();

    /**
     * @brief Sets the device serial numbers of attached devices to be used when loading calibration data.
     */
    static void SetDeviceSerialNumbers(const std::vector<std::string> &deviceSerialNumbers);

    /**
     * @brief Sets the install path for Simulated Reality to be used when configuring the weaver.
     */
    static void SetInstallPath(const std::string &installPath);

    /**
     * @brief Fill the vertex attributes needed for weaving.
     * @param[in] TextureRes Size of the back buffer.
     * @param[in] ViewPos position of view
     * @param[in] Position Vertex position.
     * @param[out] phases
     * @param[out] dxy
     * @param[out] ScreenPos
     * @param[out] WeaverVars
     */
    static void FillAttributes(const FLOAT2& TextureRes, const FLOAT2& ViewPos, const FLOAT4& Position, FLOAT4& phases, FLOAT4& dxy, FLOAT2& ScreenPos, FLOAT2& WeaverVars);

    /**
     * @brief Fill the vertex attributes needed for weaving. Assume fullscreen window
     * @param[in] TextureRes Size of the back buffer.
     * @param[in] Position Vertex position.
     * @param[out] phases
     * @param[out] dxy
     * @param[out] ScreenPos
     * @param[out] WeaverVars
     */
    static void FillAttributes(const FLOAT2& TextureRes, const FLOAT4& Position, FLOAT4& phases, FLOAT4& dxy, FLOAT2& ScreenPos, FLOAT2& WeaverVars);

    /**
     * @brief Load a weaver texture.
     * @param[in] type Weaver texture type.
     * @param[out] out_pTexture Pointer to a texture.
     * @param[out] out_width Width in pixels.
     * @param[out] out_height Height in pixels.
     * @param[out] out_channels Amount of channels.
     * @param[out] out_bitsPerPixel Bits per pixel.
     * @return Error code
     */
    static WeaverErrorCode LoadTexture(WeaverTextureType type, unsigned char** out_pTexture, int* out_width, int* out_height, int* out_channels, int* out_bitsPerPixel);

    /**
     * @brief Load a weaver texture.
     * @param[in] type Weaver texture type.
     * @param[out] out_pTexture Pointer to a texture.
     * @param[out] out_width Width in pixels.
     * @param[out] out_height Height in pixels.
     * @param[out] out_channels Amount of channels.
     * @param[out] out_bitsPerPixel Bits per pixel.
     * @param[in] flipVertical True to flip the texture vertically
     * @return Error code
     */
    static WeaverErrorCode LoadTexture(WeaverTextureType type, unsigned char** out_pTexture, int* out_width, int* out_height, int* out_channels, int* out_bitsPerPixel, bool flipVertical);

    /**
     * @brief Unloads a texture.
     * @param in_pTexture Pointer to a texture.
     */
    static void UnLoadCorrectionTexture(unsigned char** in_pTexture);

    static float GetPattern();
    static float GetXTalkFactor();
    static float GetXTalkDynamicFactor();
    static float GetFilterWidth();
    static float GetFilterSlope();
    static bool GetLateLatchingForceOn();
    static bool GetLateLatchingForceOff();

    /*!
     * \brief Returns slant as a coëfficiënt
     */
    static float GetSlant();

    /*!
     * \brief Returns pitch in x direction in pixels
     */
    static float GetPx();

    /*!
     * \brief Returns the refractive index N
     */
    static float GetN();

    /*!
     * \brief Returns D over N in millimeters
     */
    static float GetDoN();

    /*!
     * \brief Returns the behavior when the weaver is not tracking a user.
     */
    static BehaviorWhenNotTracking GetBehaviorWhenNotTracking();

private:
    static void _SetGlobalParameters(FLOAT3 userPos);
};

}

#undef DIMENCOSR_API
