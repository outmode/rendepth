// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include "DepthEstimator.h"

#ifdef RENDEPTH_ENABLE_COREML
#include <coreml_provider_factory.h>
#include <onnxruntime_cxx_api.h>

namespace CoreMLInference {
inline DepthEstimator::Provider resolve(DepthEstimator::Provider provider) {
	return provider == DepthEstimator::Provider::Auto
		? DepthEstimator::Provider::CoreML : provider;
}

inline void append(Ort::SessionOptions& options) {
	// Select CPU and GPU compute units for Core ML inference.
	Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(
		options, COREML_FLAG_USE_CPU_AND_GPU));
}
}
#endif
