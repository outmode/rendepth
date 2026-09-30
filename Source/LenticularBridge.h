// Copyright (c) 2026 Outmode. Licensed under the MIT license.
#pragma once

#if defined(_WIN32) && defined(_MSC_VER)
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

// The SR runtime is supplied by the monitor software. No ReShade component is used.
namespace LenticularBridge {
bool supported(SDL_GPUDevice* device, SDL_Window* window);
bool weave(SDL_GPUCommandBuffer* command, SDL_Window* window,
           SDL_GPUTexture* sbs, SDL_GPUTexture* backbuffer,
           unsigned width, unsigned height);
void stop();
}
#endif
