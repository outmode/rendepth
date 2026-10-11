// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include "DepthEstimator.h"

#ifdef RENDEPTH_ENABLE_COREML
#include <coreml_provider_factory.h>
#include <onnxruntime_cxx_api.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <sys/utsname.h>

namespace CoreMLInference {
inline DepthEstimator::Provider resolve(DepthEstimator::Provider provider) {
	return provider == DepthEstimator::Provider::Auto
		? DepthEstimator::Provider::CoreML : provider;
}

inline void append(Ort::SessionOptions& options, const std::filesystem::path& modelPath,
	int inputWidth, int inputHeight) {
	// Fixed shapes let Core ML keep larger subgraphs together. MLProgram is
	// substantially faster than the legacy NeuralNetwork format for these models.
	std::unordered_map<std::string, std::string> providerOptions{
		{"ModelFormat", "MLProgram"},
		{"MLComputeUnits", "CPUAndGPU"},
		{"RequireStaticInputShapes", "1"}
	};
	std::error_code error;
	const auto size = std::filesystem::file_size(modelPath, error);
	if (!error) {
		const auto modified = std::filesystem::last_write_time(modelPath, error);
		if (!error) {
			const std::string cacheVersion = OrtGetApiBase()->GetVersionString();
			utsname systemInfo{};
			const std::string osVersion = uname(&systemInfo) == 0 ? systemInfo.release : "unknown";
			const auto cacheDirectory = modelPath.parent_path() / ".RendepthCoreMLCache" /
				(cacheVersion + "-" + osVersion + "-mlprogram-" +
				modelPath.filename().string() + "-" +
				std::to_string(size) + "-" +
				std::to_string(static_cast<long long>(modified.time_since_epoch().count())) + "-" +
				std::to_string(inputWidth) + "x" + std::to_string(inputHeight));
			std::filesystem::create_directories(cacheDirectory, error);
			if (!error) providerOptions["ModelCacheDirectory"] = cacheDirectory.string();
		}
	}
	options.AppendExecutionProvider("CoreML", providerOptions);
}
}
#endif
