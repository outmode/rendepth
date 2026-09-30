#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace CubeViCalibration {
struct Optics {
    float interval = 0.0f;
    float obliquity = 0.0f;
    float deviation = 0.0f;
};

// Fallback from the owner's C1 Windows deviceConfig.json backup.
// Calibration is panel-specific; a loaded calibration overrides these values.
inline constexpr Optics c1Defaults{19.6186f, 0.10744f, 11.193443f};

// Accepts decoded vendor JSON or the encrypted deviceConfig.json wrapper.
// Failure leaves the caller's calibration untouched.
bool parse(std::string_view json, Optics& optics, std::string& error);
bool load(const std::filesystem::path& path, Optics& optics, std::string& error);
std::filesystem::path find();
}
