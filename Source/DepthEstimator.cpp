// Copyright (c) 2026 Outmode
//
// SPDX-License-Identifier: MIT

#include "DepthEstimator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

#ifdef RENDEPTH_ENABLE_ONNX_RUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace {

constexpr int lanczosRadius = 3;
constexpr float pi = 3.14159265358979323846f;

float lanczosWeight(float distance) {
	distance = std::abs(distance);
	if (distance >= static_cast<float>(lanczosRadius)) return 0.0f;
	if (distance < std::numeric_limits<float>::epsilon()) return 1.0f;
	const float x = pi * distance;
	return (std::sin(x) / x) *
		(std::sin(x / static_cast<float>(lanczosRadius)) /
			(x / static_cast<float>(lanczosRadius)));
}

}

struct DepthEstimator::State {
#ifdef RENDEPTH_ENABLE_ONNX_RUNTIME
	Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "Rendepth"};
	Ort::SessionOptions sessionOptions;
	std::unique_ptr<Ort::Session> session;
	Ort::RunOptions runOptions;
	std::string inputName;
	std::string outputName;
	int inputWidth = 0;
	int inputHeight = 0;
	bool channelFirst = true;
	bool dynamicSpatial = false;
	bool metric = false;
#endif
	Config config;
};

DepthEstimator::~DepthEstimator() {
	unload();
}

void DepthEstimator::unload() {
	delete state;
	state = nullptr;
	activeProvider = "Unavailable";
}

bool DepthEstimator::load(const Config& config, std::string& error) {
	unload();

#ifndef RENDEPTH_ENABLE_ONNX_RUNTIME
	(void)config;
	error = "ONNX Runtime support is not enabled. Configure RENDEPTH_ONNXRUNTIME_DIR "
		"and rebuild with RENDEPTH_ENABLE_ONNX_RUNTIME=ON.";
	return false;
#else
	if (config.modelPath.empty() || !std::filesystem::is_regular_file(config.modelPath)) {
		error = "Depth model was not found: " + config.modelPath.string();
		return false;
	}

	auto nextState = std::make_unique<State>();
	nextState->config = config;
	if (config.intraOpThreads > 0) {
		nextState->sessionOptions.SetIntraOpNumThreads(static_cast<int>(config.intraOpThreads));
	}
	nextState->sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

	std::string sessionStage = "creating ONNX Runtime session";
	try {
		if (config.provider == Provider::CUDA) {
#ifdef RENDEPTH_ENABLE_CUDA
			OrtCUDAProviderOptions cudaOptions{};
			nextState->sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
#else
			error = "CUDA support is not enabled. Configure with "
				"RENDEPTH_ENABLE_CUDA=ON and rebuild against a CUDA-enabled ONNX Runtime.";
			return false;
#endif
		} else if (config.provider == Provider::ROCM) {
#ifdef RENDEPTH_ENABLE_ROCM
			OrtROCMProviderOptions rocmOptions{};
			nextState->sessionOptions.AppendExecutionProvider_ROCM(rocmOptions);
#else
			error = "ROCm support is not enabled. Configure with "
				"RENDEPTH_ENABLE_ROCM=ON and rebuild against a ROCm-enabled ONNX Runtime.";
			return false;
#endif
		}
		nextState->session = std::make_unique<Ort::Session>(
			nextState->environment, config.modelPath.c_str(), nextState->sessionOptions);
		Ort::AllocatorWithDefaultOptions allocator;
		sessionStage = "reading ONNX input name";
		auto inputName = nextState->session->GetInputNameAllocated(0, allocator);
		nextState->inputName = inputName.get();
		sessionStage = "reading ONNX output metadata";
		const size_t outputCount = nextState->session->GetOutputCount();
		for (size_t outputIndex = 0; outputIndex < outputCount; ++outputIndex) {
			auto outputName = nextState->session->GetOutputNameAllocated(outputIndex, allocator);
			const std::string name = outputName.get();
			if (name == "depth" || nextState->outputName.empty()) nextState->outputName = name;
			if (name == "sky") nextState->metric = true;
		}
		if (nextState->outputName.empty()) {
			error = "DA3 model has no output tensors.";
			return false;
		}

		nextState->channelFirst = true;
		nextState->dynamicSpatial = true;
		nextState->inputHeight = config.processSize;
		nextState->inputWidth = config.processSize;
	} catch (const Ort::Exception& exception) {
		error = exception.what();
		return false;
	} catch (const std::exception& exception) {
		error = "ONNX Runtime session initialization failed while " + sessionStage + ": ";
		error += exception.what();
		return false;
	}

	switch (config.provider) {
		case Provider::CUDA: activeProvider = "CUDA"; break;
		case Provider::ROCM: activeProvider = "ROCm"; break;
		default: activeProvider = "CPU"; break;
	}
	state = nextState.release();
	return true;
#endif
}

bool DepthEstimator::ready() const {
	return state != nullptr;
}

void DepthEstimator::cancel() {
#ifdef RENDEPTH_ENABLE_ONNX_RUNTIME
	if (state != nullptr) state->runOptions.SetTerminate();
#endif
}

const std::string& DepthEstimator::providerName() const {
	return activeProvider;
}

DepthEstimator::Result DepthEstimator::predict(const SDL_Surface* image, std::string& error) {
	Result result;
#ifndef RENDEPTH_ENABLE_ONNX_RUNTIME
	(void)image;
	error = "ONNX Runtime support is not enabled.";
	return result;
#else
	if (!ready() || image == nullptr) {
		error = "Depth estimator is not loaded or the input image is null.";
		return result;
	}

	const int width = state->inputWidth;
	const int height = state->inputHeight;
	int processWidth = width;
	int processHeight = height;
	if (state->dynamicSpatial) {
		processWidth = state->config.processSize;
		processHeight = state->config.processSize;
	}
	if (processWidth <= 0 || processHeight <= 0 ||
		static_cast<size_t>(processWidth) > std::numeric_limits<size_t>::max() /
			static_cast<size_t>(processHeight) ||
		static_cast<size_t>(processWidth) * static_cast<size_t>(processHeight) >
			std::numeric_limits<size_t>::max() / 3) {
		error = "DA3 model produced invalid input dimensions: " +
			std::to_string(processWidth) + "x" + std::to_string(processHeight) + ".";
		return result;
	}
	std::vector<float> input(static_cast<size_t>(3) * static_cast<size_t>(processWidth) *
		static_cast<size_t>(processHeight));
	const auto* inputFormat = SDL_GetPixelFormatDetails(image->format);
	if (inputFormat == nullptr) {
		error = "Input image has an unknown SDL pixel format.";
		return result;
	}
	const float scale = std::min(static_cast<float>(processWidth) / image->w,
		static_cast<float>(processHeight) / image->h);
	const int contentWidth = std::max(1, static_cast<int>(std::lround(image->w * scale)));
	const int contentHeight = std::max(1, static_cast<int>(std::lround(image->h * scale)));
	const int contentOffsetX = (processWidth - contentWidth) / 2;
	const int contentOffsetY = (processHeight - contentHeight) / 2;
	const int bytesPerPixel = SDL_BYTESPERPIXEL(image->format);
	const size_t planeSize = static_cast<size_t>(processWidth) * processHeight;

	for (int y = 0; y < processHeight; ++y) {
		for (int x = 0; x < processWidth; ++x) {
			float red = 0.485f;
			float green = 0.456f;
			float blue = 0.406f;
			if (x >= contentOffsetX && x < contentOffsetX + contentWidth &&
				y >= contentOffsetY && y < contentOffsetY + contentHeight) {
				const float sourceX = (static_cast<float>(x - contentOffsetX) + 0.5f) / scale - 0.5f;
				const float sourceY = (static_cast<float>(y - contentOffsetY) + 0.5f) / scale - 0.5f;
				const int sourceXBase = static_cast<int>(std::floor(sourceX));
				const int sourceYBase = static_cast<int>(std::floor(sourceY));
				float totalWeight = 0.0f;
				float redSum = 0.0f, greenSum = 0.0f, blueSum = 0.0f;

				for (int sampleY = sourceYBase - lanczosRadius + 1;
					sampleY <= sourceYBase + lanczosRadius; ++sampleY) {
					const int clampedY = std::clamp(sampleY, 0, image->h - 1);
					const float yWeight = lanczosWeight(sourceY - sampleY);
					for (int sampleX = sourceXBase - lanczosRadius + 1;
						sampleX <= sourceXBase + lanczosRadius; ++sampleX) {
						const int clampedX = std::clamp(sampleX, 0, image->w - 1);
						const float weight = yWeight * lanczosWeight(sourceX - sampleX);
						if (weight == 0.0f) continue;
						const auto* pixel = static_cast<const Uint8*>(image->pixels) +
							clampedY * image->pitch + clampedX * bytesPerPixel;
						Uint8 sampleRed = 0, sampleGreen = 0, sampleBlue = 0, alpha = 0;
						Uint32 packedPixel = 0;
						std::memcpy(&packedPixel, pixel, std::min(bytesPerPixel, 4));
						SDL_GetRGBA(packedPixel, inputFormat, nullptr,
							&sampleRed, &sampleGreen, &sampleBlue, &alpha);
						redSum += static_cast<float>(sampleRed) / 255.0f * weight;
						greenSum += static_cast<float>(sampleGreen) / 255.0f * weight;
						blueSum += static_cast<float>(sampleBlue) / 255.0f * weight;
						totalWeight += weight;
					}
				}
				if (std::abs(totalWeight) > std::numeric_limits<float>::epsilon()) {
					red = redSum / totalWeight;
					green = greenSum / totalWeight;
					blue = blueSum / totalWeight;
				}
			}

			const size_t offset = static_cast<size_t>(y * processWidth + x);
			const float normalizedRed = (red - 0.485f) / 0.229f;
			const float normalizedGreen = (green - 0.456f) / 0.224f;
			const float normalizedBlue = (blue - 0.406f) / 0.225f;
			if (state->channelFirst) {
				input[offset] = normalizedRed;
				input[planeSize + offset] = normalizedGreen;
				input[2 * planeSize + offset] = normalizedBlue;
			} else {
				input[3 * offset] = normalizedRed;
				input[3 * offset + 1] = normalizedGreen;
				input[3 * offset + 2] = normalizedBlue;
			}
		}
	}

	try {
		state->runOptions.UnsetTerminate();
		Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		std::array<int64_t, 4> inputShape = state->channelFirst
			? std::array<int64_t, 4>{1, 3, processHeight, processWidth}
			: std::array<int64_t, 4>{1, processHeight, processWidth, 3};
		auto inputTensor = Ort::Value::CreateTensor<float>(memoryInfo, input.data(), input.size(),
			inputShape.data(), inputShape.size());
		const char* inputNames[] = {state->inputName.c_str()};
		const char* outputNames[] = {state->outputName.c_str()};
		auto outputs = state->session->Run(state->runOptions, inputNames, &inputTensor, 1,
			outputNames, 1);
		if (outputs.empty() || !outputs[0].IsTensor()) {
			error = "DA3 model did not return a tensor output.";
			return result;
		}
		const auto outputInfo = outputs[0].GetTensorTypeAndShapeInfo();
		const auto outputShape = outputInfo.GetShape();
		if (outputShape.size() == 4 && outputShape[0] == 1 && outputShape[1] == 1) {
			if (outputShape[2] <= 0 || outputShape[3] <= 0 ||
				outputShape[2] > std::numeric_limits<int>::max() ||
				outputShape[3] > std::numeric_limits<int>::max()) {
				error = "DA3 depth output has dynamic or invalid dimensions.";
				return result;
			}
			result.height = static_cast<int>(outputShape[2]);
			result.width = static_cast<int>(outputShape[3]);
		} else if (outputShape.size() == 3 && outputShape[0] == 1) {
			if (outputShape[1] <= 0 || outputShape[2] <= 0 ||
				outputShape[1] > std::numeric_limits<int>::max() ||
				outputShape[2] > std::numeric_limits<int>::max()) {
				error = "DA3 depth output has dynamic or invalid dimensions.";
				return result;
			}
			result.height = static_cast<int>(outputShape[1]);
			result.width = static_cast<int>(outputShape[2]);
		} else {
			error = "DA3 depth output must have shape [1, 1, H, W] or [1, H, W].";
			return result;
		}
		const size_t outputSize = static_cast<size_t>(result.width) *
			static_cast<size_t>(result.height);
		if (outputSize != static_cast<size_t>(result.width) * static_cast<size_t>(result.height)) {
			error = "DA3 depth output dimensions do not match its tensor size.";
			return result;
		}
		const float* outputData = outputs[0].GetTensorData<float>();
		const int cropX0 = std::clamp(static_cast<int>(std::lround(
			static_cast<double>(contentOffsetX) * result.width / processWidth)), 0, result.width - 1);
		const int cropY0 = std::clamp(static_cast<int>(std::lround(
			static_cast<double>(contentOffsetY) * result.height / processHeight)), 0, result.height - 1);
		const int cropX1 = std::clamp(static_cast<int>(std::lround(
			static_cast<double>(contentOffsetX + contentWidth) * result.width / processWidth)), cropX0 + 1, result.width);
		const int cropY1 = std::clamp(static_cast<int>(std::lround(
			static_cast<double>(contentOffsetY + contentHeight) * result.height / processHeight)), cropY0 + 1, result.height);
		const int croppedWidth = cropX1 - cropX0;
		const int croppedHeight = cropY1 - cropY0;
		std::vector<float> croppedValues(static_cast<size_t>(croppedWidth) * croppedHeight);
		for (int y = 0; y < croppedHeight; ++y) {
			std::copy_n(outputData + static_cast<size_t>(cropY0 + y) * result.width + cropX0,
				croppedWidth, croppedValues.data() + static_cast<size_t>(y) * croppedWidth);
		}
		result.width = croppedWidth;
		result.height = croppedHeight;
		result.values = std::move(croppedValues);
		result.metric = state->metric;
		return result;
	} catch (const Ort::Exception& exception) {
		error = exception.what();
		return result;
	} catch (const std::exception& exception) {
		error = "ONNX Runtime inference failed: ";
		error += exception.what();
		return result;
	}
#endif
}
