// Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT
#pragma once
#include "VideoPlayer.h"
#include <memory>
#include <string>

// A video-only WebRTC receiver. Navigation replaces the peer within the same session;
// media is decoded on GStreamer's threads and consumed through a newest-frame slot.
class BrowserStream {
public:
	BrowserStream();
	~BrowserStream();
	bool start(const std::string& directory, std::string& error, bool prepareDepth = false);
	void stop();
	bool running() const;
	std::shared_ptr<VideoFrame> takeFrame();
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
