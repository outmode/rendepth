// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_SCREEN_CAPTURE_H
#define RENDEPTH_SCREEN_CAPTURE_H

#include "VideoPlayer.h"
#include <SDL3/SDL_video.h>
#include <memory>
#include <string>

class ScreenCapture {
public:
	ScreenCapture();
	~ScreenCapture();

	ScreenCapture(const ScreenCapture&) = delete;
	ScreenCapture& operator=(const ScreenCapture&) = delete;

	// Opens the desktop portal and begins capturing the display associated with
	// the Rendepth window. The portal permission dialog may be shown here.
	bool start(SDL_Window* parentWindow, std::string& error);
	// Receive a live video-only WebRTC stream from a private Firefox session.
	bool startBrowser(const std::string& directory, std::string& error, bool prepareDepth = false);
	void stop();
	bool running() const;

	// Non-blocking: returns the newest frame available for the render thread.
	std::shared_ptr<VideoFrame> takeFrame();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

#endif
