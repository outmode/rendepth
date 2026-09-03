// Copyright (c) 2026 Outmode
//
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_DEPTH_ESTIMATOR_H
#define RENDEPTH_DEPTH_ESTIMATOR_H

#include "SDL3/SDL.h"
#include <filesystem>
#include <string>
#include <vector>

class DepthEstimator {
public:
	enum class Provider {
		Auto,
		CPU,
		ROCM,
		OpenVINO,
		DirectML,
		CUDA,
		TensorRT,
		MIGraphX
	};

	struct Config {
		std::filesystem::path modelPath;
		Provider provider = Provider::Auto;
		int processSize = 504;
		unsigned int intraOpThreads = 0;
	};

	struct Result {
		std::vector<float> values;
		int width = 0;
		int height = 0;
		bool metric = false;

		bool valid() const {
			return width > 0 && height > 0 &&
				values.size() == static_cast<size_t>(width * height);
		}
	};

	DepthEstimator() = default;
	~DepthEstimator();
	DepthEstimator(const DepthEstimator&) = delete;
	DepthEstimator& operator=(const DepthEstimator&) = delete;

	bool load(const Config& config, std::string& error);
	void cancel();
	void unload();
	bool ready() const;
	const std::string& providerName() const;
	Result predict(const SDL_Surface* image, std::string& error);

private:
	struct State;
	State* state = nullptr;
	std::string activeProvider = "Unavailable";
};

#endif
