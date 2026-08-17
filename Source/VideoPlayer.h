// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_VIDEO_PLAYER_H
#define RENDEPTH_VIDEO_PLAYER_H

#include "SDL3/SDL.h"
#include <filesystem>
#include <memory>
#include <string>

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
	SDL_Surface* takeFrame(bool* preview = nullptr);
	std::string takeError();
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
