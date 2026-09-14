#pragma once
#include <SDL3/SDL.h>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace NativeDisplayIdentity {
inline std::string hardwareId(SDL_DisplayID display) {
#ifdef _WIN32
    const auto monitor = static_cast<HMONITOR>(SDL_GetPointerProperty(
        SDL_GetDisplayProperties(display), SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER, nullptr));
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return {};
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD index = 0; EnumDisplayDevicesW(info.szDevice, index, &device, 0); ++index) {
        if (!(device.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
        const int size = WideCharToMultiByte(CP_UTF8, 0, device.DeviceID, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1) return {};
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, device.DeviceID, -1, result.data(), size, nullptr, nullptr);
        result.pop_back();
        return result;
    }
#endif
    return {};
}

inline bool isCubeViC1(const std::string& hardware, int width, int height) {
    // Observed on the connected C1. Do not match arbitrary "C1" substrings
    // or infer the vendor from resolution alone.
    return hardware.rfind("MONITOR\\OPC1155\\", 0) == 0 && width == 1440 && height == 2560;
}
}
