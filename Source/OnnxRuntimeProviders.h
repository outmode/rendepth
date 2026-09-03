// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#ifndef RENDEPTH_ONNX_RUNTIME_PROVIDERS_H
#define RENDEPTH_ONNX_RUNTIME_PROVIDERS_H

#include "DepthEstimator.h"
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace RendepthOnnx {

inline void registerProviderLibraries(Ort::Env& environment) {
	std::vector<std::filesystem::path> directories;
	if (const char* configured = std::getenv("RENDEPTH_ONNXRUNTIME_EP_DIR"); configured != nullptr)
		directories.emplace_back(configured);
	std::error_code error;
	const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
	if (!error) {
		directories.push_back(executable.parent_path());
		directories.push_back(executable.parent_path() / "../Library");
	}
	directories.emplace_back("Library");
	const std::vector<std::pair<const char*, const char*>> providers = {
		{"CUDA", "libonnxruntime_providers_cuda.so"},
		{"ROCM", "libonnxruntime_providers_rocm.so"},
		{"OpenVINO", "libonnxruntime_providers_openvino.so"}
	};
	for (const auto& [registrationName, filename] : providers) {
		bool registered = false;
		for (const auto& directory : directories) {
			const auto path = directory / filename;
			if (!std::filesystem::is_regular_file(path, error)) continue;
			try {
				environment.RegisterExecutionProviderLibrary(registrationName, path.string());
				registered = true;
				break;
			} catch (const Ort::Exception&) {
				// A provider can be present but unusable because its vendor runtime
				// is absent. Session creation will try the next provider.
			}
		}
		if (!registered) {
			try {
				// Also allow the platform dynamic loader to resolve providers
				// installed in its standard library paths.
				environment.RegisterExecutionProviderLibrary(registrationName, filename);
			} catch (const Ort::Exception&) {
				// The provider is optional; session creation will fall back.
			}
		}
	}
}

inline std::vector<DepthEstimator::Provider> candidates(DepthEstimator::Provider requested) {
	std::vector<DepthEstimator::Provider> result;
	const auto add = [&result](DepthEstimator::Provider provider) {
		if (std::find(result.begin(), result.end(), provider) == result.end()) result.push_back(provider);
	};
	if (requested != DepthEstimator::Provider::Auto && requested != DepthEstimator::Provider::CPU)
		add(requested);
	if (requested == DepthEstimator::Provider::Auto) {
		add(DepthEstimator::Provider::CUDA);
		add(DepthEstimator::Provider::ROCM);
		add(DepthEstimator::Provider::OpenVINO);
	}
	if (requested == DepthEstimator::Provider::CUDA) add(DepthEstimator::Provider::ROCM);
	if (requested == DepthEstimator::Provider::ROCM) add(DepthEstimator::Provider::CUDA);
	if (requested != DepthEstimator::Provider::OpenVINO) add(DepthEstimator::Provider::OpenVINO);
	add(DepthEstimator::Provider::CPU);
	return result;
}

inline void append(Ort::SessionOptions& options, DepthEstimator::Provider provider) {
	if (provider == DepthEstimator::Provider::CUDA) {
		OrtCUDAProviderOptions providerOptions{};
		options.AppendExecutionProvider_CUDA(providerOptions);
	} else if (provider == DepthEstimator::Provider::ROCM) {
		OrtROCMProviderOptions providerOptions{};
		options.AppendExecutionProvider_ROCM(providerOptions);
	} else if (provider == DepthEstimator::Provider::OpenVINO) {
		OrtOpenVINOProviderOptions providerOptions{};
		providerOptions.device_type = "GPU_FP32";
		options.AppendExecutionProvider_OpenVINO(providerOptions);
	}
}

inline const char* name(DepthEstimator::Provider provider) {
	switch (provider) {
		case DepthEstimator::Provider::CUDA: return "CUDA";
		case DepthEstimator::Provider::ROCM: return "ROCm";
		case DepthEstimator::Provider::OpenVINO: return "OpenVINO";
		default: return "CPU";
	}
}

}

#endif
