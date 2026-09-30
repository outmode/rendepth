// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_SUPER_RESOLUTION_H
#define RENDEPTH_SUPER_RESOLUTION_H

#include "DepthEstimator.h"
#include "SDL3/SDL.h"

#include <filesystem>
#include <string>

class SuperResolution {
public:
	SuperResolution() = default;
	struct Config {
		std::filesystem::path modelPath;
		DepthEstimator::Provider provider = DepthEstimator::Provider::Auto;
		unsigned int intraOpThreads = 0;
	};

	~SuperResolution();
	SuperResolution(const SuperResolution&) = delete;
	SuperResolution& operator=(const SuperResolution&) = delete;

	bool load(const Config& config, std::string& error);
	void unload();
	SDL_Surface* predict(const SDL_Surface* image, std::string& error) const;
	const std::string& providerName() const;

private:
	struct State;
	State* state = nullptr;
	std::string activeProvider = "Unavailable";
};

#endif
