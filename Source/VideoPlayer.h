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
	int inferenceWidth = 0;
	int inferenceHeight = 0;
	double presentationTime = 0.0;
	std::uint64_t generation = 0;
	std::array<std::vector<std::uint8_t>, 3> planes;
	std::vector<std::uint8_t> inferenceRGBA;
};

struct VideoSubtitle {
	enum class Format { Text, Bitmap };

	Format format = Format::Text;
	std::string text;
	int canvasWidth = 0;
	int canvasHeight = 0;
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba;
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
	std::shared_ptr<VideoFrame> takeFrame(bool* preview = nullptr);
	std::string takeError();
	void setAudioBuffering(bool buffering);
	SDL_Surface* takeAlbumArt();
	bool hasAudio() const;
	bool audioOnly() const;
	bool audioReady() const;
	double bufferedAudioDuration() const;
	double audioDeviceLatency() const;
	double audioPlaybackPosition() const;
	void setPresentedPosition(double seconds);
	void setOutputSize(int maxWidth, int maxHeight);
	void setInferenceSize(int maxDimension, double framesPerSecond = 10.0);
	void seek(double seconds, bool fastPreview = false);
	void setPlaying(bool playing);
	void setVolume(double volume);
	void cycleAudioTrack();
	void cycleSubtitleTrack();
	std::string audioLanguage() const;
	std::string subtitleLanguage() const;
	std::shared_ptr<const VideoSubtitle> subtitle() const;
	std::string subtitleText() const;
	bool playing() const;
	bool ready() const;
	bool isBluray() const;
	bool isDvd() const;
	bool hasChapters() const;
	int currentChapter() const;
	int chapterCount() const;
	double chapterTime(int chapterIndex) const;
	void nextChapter();
	void previousChapter();
	void seekChapter(int chapterIndex);
	std::string discTitle() const;
	double position() const;
	double duration() const;
	std::uint64_t generation() const;
	int width() const;
	int height() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

#endif
