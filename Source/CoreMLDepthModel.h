// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Native Core ML backend for fixed-size Depth Anything V2 image and video models.
class CoreMLDepthModel {
public:
	static std::unique_ptr<CoreMLDepthModel> load(const std::filesystem::path& path,
		int inputSize, std::string& error);
	~CoreMLDepthModel();
	CoreMLDepthModel(const CoreMLDepthModel&) = delete;
	CoreMLDepthModel& operator=(const CoreMLDepthModel&) = delete;

	bool predict(const float* input, size_t count, std::vector<float>& output,
		int& width, int& height, std::string& error) const;

private:
	CoreMLDepthModel() = default;
	const void* model_ = nullptr;
	std::string outputName_;
	int inputSize_ = 0;
};
