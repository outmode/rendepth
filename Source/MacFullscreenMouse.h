// Copyright (c) 2026 Outmode
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#ifdef __APPLE__
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

// SDL's window-local mouse position can stop updating in macOS fullscreen
// spaces. The global position remains current; convert it back to window space.
inline bool getMacFullscreenMousePosition(SDL_Window* window, float& x, float& y) {
	if (window == nullptr ||
		(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0 ||
		SDL_GetKeyboardFocus() != window) return false;
	int windowX = 0, windowY = 0, width = 0, height = 0;
	if (!SDL_GetWindowPosition(window, &windowX, &windowY) ||
		!SDL_GetWindowSize(window, &width, &height)) return false;
	float globalX = 0.0f, globalY = 0.0f;
	SDL_GetGlobalMouseState(&globalX, &globalY);
	x = globalX - windowX;
	y = globalY - windowY;
	return x >= 0.0f && y >= 0.0f && x < width && y < height;
}
#endif
