// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_VIDEO_PLAYER_H
#define RENDEPTH_VIDEO_PLAYER_H

#include "SDL3/SDL.h"
#include "AudioWaveform.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct VideoFrame {
	struct MotionVector {
		// Normalized block centers and dimensions, in display orientation.
		float destinationX = 0.0f;
		float destinationY = 0.0f;
		float sourceX = 0.0f;
		float sourceY = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};
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
	std::vector<MotionVector> motionVectors;
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
	bool open(const std::filesystem::path& path, std::string& error, int discTitle = -1);
	void close();
	void update();
	std::shared_ptr<VideoFrame> takeFrame(bool* preview = nullptr);
	std::string takeError();
	void setAudioBuffering(bool buffering);
	SDL_Surface* takeAlbumArt();
	bool hasAudio() const;
	bool audioOnly() const;
	AudioWaveform::Bars audioWaveform() const;
	bool discSource() const;
	bool nativeStereo() const;
	bool audioCd() const;
	bool audioReady() const;
	double bufferedAudioDuration() const;
	double audioDeviceLatency() const;
	double audioPlaybackPosition() const;
	void setPresentedPosition(double seconds);
	void setOutputSize(int maxWidth, int maxHeight);
	void setInferenceSize(int maxDimension, double framesPerSecond = 10.0);
	// Disc sources accept committed seeks only; drag previews never read the disc.
	void seek(double seconds, bool fastPreview = false);
	void setPlaying(bool playing);
	void setVolume(double volume);
	void cycleAudioTrack();
	void cycleSubtitleTrack();
	void resetAudioTrack();
	void resetSubtitleTrack();
	std::string audioLanguage() const;
	std::string subtitleLanguage() const;
	std::shared_ptr<const VideoSubtitle> subtitle() const;
	std::string subtitleText() const;
	bool playing() const;
	bool ready() const;
	bool hasChapters() const;
	int currentChapter() const;
	int chapterCount() const;
	int chapterAtTime(double seconds) const;
	double chapterTime(int chapterIndex) const;
	void nextChapter();
	void previousChapter();
	void seekChapter(int chapterIndex);
	double position() const;
	double duration() const;
	std::uint64_t generation() const;
	int width() const;
	int displayWidth(int decodedWidth, int decodedHeight) const;
	int height() const;
	int rotation() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

#endif
