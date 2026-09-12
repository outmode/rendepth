// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_VIDEO_DEPTH_PROCESSOR_H
#define RENDEPTH_VIDEO_DEPTH_PROCESSOR_H

#include "DepthEstimator.h"
#include "VideoPlayer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct VideoDepthFrame {
	std::vector<std::uint16_t> values;
	int width = 0;
	int height = 0;
	double presentationTime = 0.0;
	std::uint64_t generation = 0;

	bool valid() const {
		return width > 0 && height > 0 &&
			values.size() == static_cast<size_t>(width * height);
	}
};

class VideoDepthProcessor {
public:
	struct Config {
		std::filesystem::path modelDirectory;
		std::string modelFilename;
		DepthEstimator::Provider provider = DepthEstimator::Provider::Auto;
		int processSize = 560;
		unsigned int intraOpThreads = 0;
		double targetFramesPerSecond = 10.0;
	};

	VideoDepthProcessor() = default;
	~VideoDepthProcessor();
	VideoDepthProcessor(const VideoDepthProcessor&) = delete;
	VideoDepthProcessor& operator=(const VideoDepthProcessor&) = delete;

	bool start(const Config& config);
	void stop();
	void reset();
	void submit(const std::shared_ptr<const VideoFrame>& frame);
	std::unique_ptr<VideoDepthFrame> takeFrame();
	std::string takeError();
	bool running() const;
	bool ready() const;

private:
	void run();
	VideoDepthFrame stabilize(const DepthEstimator::Result& depth,
		const VideoFrame& source);
	void clearTemporalState();

	DepthEstimator estimator;
	Config activeConfig;
	std::thread worker;
	mutable std::mutex mutex;
	std::mutex temporalMutex;
	std::condition_variable changed;
	std::shared_ptr<const VideoFrame> pendingFrame;
	std::unique_ptr<VideoDepthFrame> completedFrame;
	std::string runtimeError;
	std::atomic<bool> stopRequested{false};
	std::atomic<bool> isRunning{false};
	std::atomic<bool> isReady{false};
	double lastSubmittedTime = -1.0;
	std::uint64_t lastSubmittedGeneration = 0;
	std::uint64_t temporalGeneration = 0;
	int temporalWidth = 0;
	int temporalHeight = 0;
	float smoothedLow = 0.0f;
	float smoothedHigh = 1.0f;
	bool haveRange = false;
	std::vector<float> previousLuminance;
};

#endif
