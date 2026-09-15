#pragma once

namespace LookingGlassCalibration {
// Fallback copied from the owner's mounted LKG Go LKG_calibration/visual.json.
// Panel-specific optical values; a loaded calibration takes precedence.
// Keep vendor field names so the normal parser handles view direction and flags.
inline constexpr char goDefaults[] = R"({
    "pitch": 80.74475016919882,
    "slope": -6.6587766801719095,
    "center": 0.47708486403293315,
    "viewCone": 54.0,
    "invView": 1,
    "DPI": 491,
    "screenW": 1440,
    "screenH": 2560,
    "flipImageX": 0,
    "flipImageY": 0,
    "flipSubp": 0
})";
}
