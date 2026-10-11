// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class CoreMLSuperResolutionModel {
public:
	static std::unique_ptr<CoreMLSuperResolutionModel> load(
		const std::filesystem::path& path, std::string& error);
	~CoreMLSuperResolutionModel();
	CoreMLSuperResolutionModel(const CoreMLSuperResolutionModel&) = delete;
	CoreMLSuperResolutionModel& operator=(const CoreMLSuperResolutionModel&) = delete;
	bool predict(const float* input, int width, int height,
		std::vector<float>& output, int& outputWidth, int& outputHeight,
		std::string& error) const;

private:
	CoreMLSuperResolutionModel() = default;
	const void* model_ = nullptr;
	std::string outputName_;
};
