// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <filesystem>

namespace GettingStarted {
enum class Layout { Single, SBSFull, SBSHalf, RGBD };
enum class Action { None, Dismiss, PreferenceChanged };
inline bool visible = false;
inline bool dontShowAgain = false;
void show();
Action handleEvent(const SDL_Event& event, SDL_Window* window);
SDL_Surface* render(int width, int height, float scale, const std::filesystem::path& assets, bool light = false, Layout layout = Layout::Single);
std::uint64_t revision();
void release();
}
