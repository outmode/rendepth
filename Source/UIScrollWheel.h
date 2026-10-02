// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <SDL3/SDL_events.h>

namespace UIScrollWheel {

// Cocoa's wheel delta already follows the macOS natural-scrolling setting.
// Other platforms retain the existing SDL direction normalization.
inline float deltaY(const SDL_MouseWheelEvent& wheel) {
#ifdef __APPLE__
    return wheel.y;
#else
    return wheel.y * (wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
#endif
}

} // namespace UIScrollWheel
