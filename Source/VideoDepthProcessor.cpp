// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "VideoDepthProcessor.h"

#include "ModelDownloader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace {

// Apply a light separable Gaussian blur to reduce local depth noise.
void applyLightGaussianBlur(std::vector<float>& values, int width, int height) {
	if (width <= 1 || height <= 1 ||
		values.size() != static_cast<size_t>(width) * height) return;

	const std::vector<float> original = values;
	std::vector<float> horizontal(values.size());
	for (int y = 0; y < height; ++y) {
		const size_t row = static_cast<size_t>(y) * width;
		for (int x = 0; x < width; ++x) {
			const int left = std::max(0, x - 1);
			const int right = std::min(width - 1, x + 1);
			horizontal[row + x] = (values[row + left] + values[row + x] * 2.0f +
				values[row + right]) * 0.25f;
		}
	}
	for (int y = 0; y < height; ++y) {
		const size_t top = static_cast<size_t>(std::max(0, y - 1)) * width;
		const size_t row = static_cast<size_t>(y) * width;
		const size_t bottom = static_cast<size_t>(std::min(height - 1, y + 1)) * width;
		for (int x = 0; x < width; ++x)
			values[row + x] = (horizontal[top + x] + horizontal[row + x] * 2.0f +
				horizontal[bottom + x]) * 0.25f;
	}
	constexpr float blurStrength = 0.5f;
	for (size_t index = 0; index < values.size(); ++index)
		values[index] = original[index] + (values[index] - original[index]) * blurStrength;
}

}

// Stop inference before destroying the processor.
VideoDepthProcessor::~VideoDepthProcessor() {
	stop();
}

// Launch a fresh depth worker using the requested model and provider configuration.
bool VideoDepthProcessor::start(const Config& config) {
	stop();
	activeConfig = config;
	if (activeConfig.downloadProgress != nullptr)
		activeConfig.downloadProgress->cancel = false;
	stopRequested = false;
	isRunning = true;
	isReady = false;
	{
		std::lock_guard lock(mutex);
		runtimeError.clear();
	}
	try {
		worker = std::thread(&VideoDepthProcessor::run, this);
	} catch (const std::exception& exception) {
		std::lock_guard lock(mutex);
		runtimeError = std::string("Could not start video depth inference: ") + exception.what();
		isRunning = false;
		return false;
	}
	return true;
}

// Cancel and join inference, unload the model, and clear queued and temporal data.
void VideoDepthProcessor::stop() {
	stopRequested = true;
	if (activeConfig.downloadProgress != nullptr)
		activeConfig.downloadProgress->cancel = true;
	if (isReady) estimator.cancel();
	changed.notify_all();
	if (worker.joinable()) worker.join();
	estimator.unload();
	{
		std::lock_guard lock(mutex);
		pendingFrame.reset();
		completedFrame.reset();
		lastSubmittedTime = -1.0;
		lastSubmittedGeneration = 0;
	}
	clearTemporalState();
	isReady = false;
	isRunning = false;
}

// Discard queued frames and temporal history without unloading the running model.
void VideoDepthProcessor::reset() {
	std::lock_guard lock(mutex);
	pendingFrame.reset();
	completedFrame.reset();
	lastSubmittedTime = -1.0;
	lastSubmittedGeneration = 0;
	clearTemporalState();
}

// Keep the newest eligible frame for inference, rejecting repeated timestamps within a generation.
void VideoDepthProcessor::submit(const std::shared_ptr<const VideoFrame>& frame) {
	if (!isRunning || frame == nullptr || frame->inferenceWidth <= 0 ||
		frame->inferenceHeight <= 0 || frame->inferenceRGBA.size() <
		static_cast<size_t>(frame->inferenceWidth) * frame->inferenceHeight * 4) return;

	std::lock_guard lock(mutex);
	const bool newGeneration = frame->generation != lastSubmittedGeneration;
	if (!newGeneration && lastSubmittedTime >= 0.0 &&
		frame->presentationTime <= lastSubmittedTime) return;
	pendingFrame = frame;
	lastSubmittedTime = frame->presentationTime;
	lastSubmittedGeneration = frame->generation;
	changed.notify_one();
}

// Transfer ownership of the latest completed depth frame to the caller.
std::unique_ptr<VideoDepthFrame> VideoDepthProcessor::takeFrame() {
	std::lock_guard lock(mutex);
	return std::move(completedFrame);
}

// Consume the worker's pending error message.
std::string VideoDepthProcessor::takeError() {
	std::lock_guard lock(mutex);
	std::string result;
	result.swap(runtimeError);
	return result;
}

// Report whether the depth worker is active.
bool VideoDepthProcessor::running() const { return isRunning; }
// Report whether the model has finished loading and can run inference.
bool VideoDepthProcessor::ready() const { return isReady; }

// Clear depth normalization history and scene-change samples.
void VideoDepthProcessor::clearTemporalState() {
	std::lock_guard lock(temporalMutex);
	temporalGeneration = 0;
	temporalWidth = 0;
	temporalHeight = 0;
	smoothedLow = 0.0f;
	smoothedHigh = 1.0f;
	haveRange = false;
	previousLuminance.clear();
}

// Normalize depth with temporally smoothed bounds, resetting history across scene or generation
// changes.
VideoDepthFrame VideoDepthProcessor::stabilize(const DepthEstimator::Result& depth,
		const VideoFrame& source) {
	std::lock_guard temporalLock(temporalMutex);
	VideoDepthFrame result;
	if (!depth.valid()) return result;

	constexpr int sampleColumns = 32;
	constexpr int sampleRows = 18;
	std::vector<float> luminance;
	luminance.reserve(sampleColumns * sampleRows);
	for (int row = 0; row < sampleRows; ++row) {
		const int y = std::min(source.inferenceHeight - 1,
			(row * source.inferenceHeight + source.inferenceHeight / 2) / sampleRows);
		for (int column = 0; column < sampleColumns; ++column) {
			const int x = std::min(source.inferenceWidth - 1,
				(column * source.inferenceWidth + source.inferenceWidth / 2) / sampleColumns);
			const size_t offset = static_cast<size_t>(y * source.inferenceWidth + x) * 4;
			luminance.push_back((source.inferenceRGBA[offset] * 0.2126f +
				source.inferenceRGBA[offset + 1] * 0.7152f +
				source.inferenceRGBA[offset + 2] * 0.0722f) / 255.0f);
		}
	}

	// Reset normalization across seeks, size changes, or abrupt luminance changes rather than carrying
	// old scene history.
	bool sceneCut = source.generation != temporalGeneration || depth.width != temporalWidth ||
		depth.height != temporalHeight || previousLuminance.size() != luminance.size();
	if (!sceneCut) {
		float difference = 0.0f;
		for (size_t index = 0; index < luminance.size(); ++index)
			difference += std::abs(luminance[index] - previousLuminance[index]);
		sceneCut = difference / static_cast<float>(luminance.size()) > 0.16f;
	}
	previousLuminance = std::move(luminance);

	std::vector<float> samples;
	const size_t sampleStep = std::max<size_t>(1, depth.values.size() / 32768);
	samples.reserve((depth.values.size() + sampleStep - 1) / sampleStep);
	for (size_t index = 0; index < depth.values.size(); index += sampleStep)
		if (std::isfinite(depth.values[index])) samples.push_back(depth.values[index]);
	if (samples.empty()) return result;
	// Use percentile bounds to resist isolated depth outliers, then ease those bounds between frames.
	const auto lowIndex = static_cast<size_t>((samples.size() - 1) * 0.02);
	const auto highIndex = static_cast<size_t>((samples.size() - 1) * 0.98);
	std::nth_element(samples.begin(), samples.begin() + lowIndex, samples.end());
	const float currentLow = samples[lowIndex];
	std::nth_element(samples.begin(), samples.begin() + highIndex, samples.end());
	const float currentHigh = samples[highIndex];

	if (sceneCut || !haveRange) {
		smoothedLow = currentLow;
		smoothedHigh = currentHigh;
		haveRange = true;
	} else {
		constexpr float rangeResponse = 0.25f;
		smoothedLow += (currentLow - smoothedLow) * rangeResponse;
		smoothedHigh += (currentHigh - smoothedHigh) * rangeResponse;
	}
	const float range = std::max(smoothedHigh - smoothedLow,
		std::numeric_limits<float>::epsilon());
	std::vector<float> normalized(depth.values.size());
	for (size_t index = 0; index < depth.values.size(); ++index) {
		const float value = std::isfinite(depth.values[index]) ? depth.values[index] : smoothedLow;
		normalized[index] = std::clamp((value - smoothedLow) / range, 0.0f, 1.0f);
	}
	applyLightGaussianBlur(normalized, depth.width, depth.height);
	result.width = depth.width;
	result.height = depth.height;
	result.presentationTime = source.presentationTime;
	result.generation = source.generation;
	result.values.resize(normalized.size());
	for (size_t index = 0; index < normalized.size(); ++index)
		result.values[index] = static_cast<std::uint16_t>(
			std::lround(std::clamp(normalized[index], 0.0f, 1.0f) * 65535.0f));
	temporalGeneration = source.generation;
	temporalWidth = depth.width;
	temporalHeight = depth.height;
	return result;
}

// Load the model and process submitted frames at the configured rate, publishing stabilized depth
// results.
void VideoDepthProcessor::run() {
	std::string error;
	DepthEstimator::Config estimatorConfig;
	estimatorConfig.modelPath = ModelDownloader::ensureAvailable(activeConfig.modelDirectory,
		activeConfig.modelFilename, error, activeConfig.downloadProgress);
	estimatorConfig.provider = activeConfig.provider;
	estimatorConfig.processSize = activeConfig.processSize;
	// Live decoding, browser encoding and rendering share this CPU. ORT's
	// default pool uses every physical core and spins between jobs, which can
	// starve playback even though inference itself runs on a separate thread.
	estimatorConfig.intraOpThreads = activeConfig.intraOpThreads > 0
		? activeConfig.intraOpThreads
		: std::clamp(std::thread::hardware_concurrency() / 4, 1u, 4u);
	estimatorConfig.allowThreadSpinning = false;
	if (estimatorConfig.modelPath.empty() || !estimator.load(estimatorConfig, error)) {
		std::lock_guard lock(mutex);
		runtimeError = error.empty() ? "Could not load the video depth model." : error;
		isRunning = false;
		return;
	}
	SDL_Log("Video depth model loaded: %s at %dx%d using %s provider.",
		estimatorConfig.modelPath.string().c_str(), estimatorConfig.processSize,
		estimatorConfig.processSize, estimator.providerName().c_str());
	isReady = true;
	SDL_Log("Video depth CPU budget: %u threads, spinning off, target %.1f maps/s.",
		estimatorConfig.intraOpThreads, activeConfig.targetFramesPerSecond);
	using InferenceClock = std::chrono::steady_clock;
	auto nextInference = InferenceClock::time_point{};
	const auto inferenceInterval = std::chrono::duration_cast<InferenceClock::duration>(
		std::chrono::duration<double>(activeConfig.targetFramesPerSecond > 0
			? 1.0 / activeConfig.targetFramesPerSecond : 0.0));

	while (!stopRequested) {
		std::shared_ptr<const VideoFrame> source;
		{
			std::unique_lock lock(mutex);
			changed.wait(lock, [this] { return stopRequested || pendingFrame != nullptr; });
			if (stopRequested) break;
			// Leave pendingFrame replaceable while waiting: process the newest
			// input at the configured rate, without accumulating a work queue.
			changed.wait_until(lock, nextInference, [this] { return stopRequested.load(); });
			if (stopRequested) break;
			source = std::move(pendingFrame);
		}
		if (source == nullptr) continue;
		nextInference = InferenceClock::now() + inferenceInterval;
		SDL_Surface* surface = SDL_CreateSurfaceFrom(source->inferenceWidth,
			source->inferenceHeight, SDL_PIXELFORMAT_RGBA32,
			const_cast<std::uint8_t*>(source->inferenceRGBA.data()),
			source->inferenceWidth * 4);
		if (surface == nullptr) {
			std::lock_guard lock(mutex);
			runtimeError = "Could not prepare a video frame for depth inference.";
			continue;
		}
		auto depth = estimator.predict(surface, error);
		SDL_DestroySurface(surface);
		if (stopRequested) break;
		if (!depth.valid()) {
			std::lock_guard lock(mutex);
			runtimeError = error.empty() ? "Video depth inference failed." : error;
			continue;
		}
		auto output = stabilize(depth, *source);
		if (!output.valid()) continue;
		{
			std::lock_guard lock(mutex);
			completedFrame = std::make_unique<VideoDepthFrame>(std::move(output));
		}
	}
	isReady = false;
	isRunning = false;
}
