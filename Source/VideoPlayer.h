// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_VIDEO_PLAYER_H
#define RENDEPTH_VIDEO_PLAYER_H

#include "SDL3/SDL.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct VideoFrame {
	enum class Format { RGBA, NV12, YUV420P };
	enum class ColorSpace { BT601, BT709, BT2020 };

	Format format = Format::RGBA;
	ColorSpace colorSpace = ColorSpace::BT709;
	bool fullRange = false;
	int width = 0;
	int height = 0;
	int outputWidth = 0;
	int outputHeight = 0;
	std::array<std::vector<std::uint8_t>, 3> planes;
};

class VideoPlayer {
public:
	VideoPlayer();
	~VideoPlayer();
	VideoPlayer(const VideoPlayer&) = delete;
	VideoPlayer& operator=(const VideoPlayer&) = delete;

	static bool supported(const std::filesystem::path& path);
	bool open(const std::filesystem::path& path, std::string& error);
	void close();
	void update();
	std::unique_ptr<VideoFrame> takeFrame(bool* preview = nullptr);
	std::string takeError();
	void setOutputSize(int maxWidth, int maxHeight);
	void seek(double seconds, bool fastPreview = false);
	void setPlaying(bool playing);
	void setVolume(double volume);
	void cycleAudioTrack();
	void cycleSubtitleTrack();
	std::string audioLanguage() const;
	std::string subtitleLanguage() const;
	std::string subtitleText() const;
	bool playing() const;
	bool ready() const;
	double position() const;
	double duration() const;
	int width() const;
	int height() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

#endif
