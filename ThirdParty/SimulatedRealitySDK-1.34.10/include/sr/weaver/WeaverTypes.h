/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

enum WeaverTextureType {
    CorrectionA = 0,
    CorrectionB = 1,
};

enum WeaverErrorCode {
    WeaverSuccess = 0,
    WeaverTextureNotFound = 1,
    WeaverTextureFailedToLoad = 2,
    WeaverTextureUnknownPixelFormat = 3,
};

enum class WeaverACTMode {
    Off = 0,
    Static = 1,
    Dynamic = 2
};

enum class BehaviorWhenNotTracking
{
    Default,            ///< Default behavior is to weave as usual
    ShowLeft,           ///< Show the left view
    ShowLeftWithShader, ///< Show the left view with a shader that indicates to the user that there is no tracking
};

struct FLOAT2 {
    float x, y;
#ifdef __cplusplus
    FLOAT2(float x = 0, float y = 0) : x(x), y(y) {}
    FLOAT2 operator/(const float   a) const { return { x / a  , y / a }; }
    FLOAT2 operator+(const FLOAT2& a) const { return { x + a.x, y + a.y }; }
    FLOAT2 operator-(const FLOAT2& a) const { return { x - a.x, y - a.y }; }
    FLOAT2 operator*(const FLOAT2& a) const { return { x * a.x, y * a.y }; }
#endif
};

struct FLOAT3 {
    float x, y, z;
#ifdef __cplusplus
    FLOAT3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
    FLOAT3 operator/(const float a) const { return {x / a, y / a, z / a}; }
    FLOAT3 operator*(const float a) const { return {x * a, y * a, z * a}; }
#endif
};

struct FLOAT4 {
    float x, y, z, w;
#ifdef __cplusplus
    FLOAT4(float x = 0, float y = 0, float z = 0, float w = 0) : x(x), y(y), z(z), w(w) {}
    FLOAT4(FLOAT3 v) : x(v.x), y(v.y), z(v.z), w(0.0) {}
    FLOAT4(FLOAT2 v) : x(v.x), y(v.y), z(0.0), w(0.0) {}

    FLOAT4 operator/(const float a) const { return {x / a, y / a, z / a, w / a}; }
    FLOAT4 operator+(const FLOAT4& a) const { return {x + a.x, y + a.y, z + a.z, w + a.w}; }
    FLOAT4 operator-(const FLOAT4& a) const { return {x - a.x, y - a.y, z - a.z, w - a.w}; }
    FLOAT4 operator*(const FLOAT4& a) const { return {x * a.x, y * a.y, z * a.z, w * a.w}; }
#endif
};
