#pragma once

// Calibration is per model, so only one display from each calibration family
// can be used. The two families also impose the two-output performance limit.
class NativeDisplaySelection {
public:
    bool add(bool cubeViC1) {
        bool& selected = cubeViC1 ? cubeViSelected : lookingGlassSelected;
        if (selected) return false;
        selected = true;
        return true;
    }
private:
    bool cubeViSelected = false;
    bool lookingGlassSelected = false;
};
