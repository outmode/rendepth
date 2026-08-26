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
			error = "Depth model has no output tensors.";
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
		error = "Depth model produced invalid input dimensions: " +
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
	const auto channelValue = [](Uint32 packed, Uint32 mask, Uint8 shift,
		Uint8 bits) {
		if (mask == 0 || bits == 0) return 0.0f;
		const Uint32 value = (packed & mask) >> shift;
		const Uint32 maximum = (1u << bits) - 1u;
		return static_cast<float>(value) / static_cast<float>(maximum);
	};
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
				const float sourceX = std::clamp(
					(static_cast<float>(x - contentOffsetX) + 0.5f) / scale - 0.5f,
					0.0f, static_cast<float>(image->w - 1));
				const float sourceY = std::clamp(
					(static_cast<float>(y - contentOffsetY) + 0.5f) / scale - 0.5f,
					0.0f, static_cast<float>(image->h - 1));
				const int x0 = static_cast<int>(sourceX);
				const int y0 = static_cast<int>(sourceY);
				const int x1 = std::min(image->w - 1, x0 + 1);
				const int y1 = std::min(image->h - 1, y0 + 1);
				const float fx = sourceX - static_cast<float>(x0);
				const float fy = sourceY - static_cast<float>(y0);
				auto readPixel = [&](int pixelX, int pixelY) {
					std::array<float, 3> sample{};
					const auto* pixel = static_cast<const Uint8*>(image->pixels) +
						pixelY * image->pitch + pixelX * bytesPerPixel;
					Uint32 packedPixel = 0;
					std::memcpy(&packedPixel, pixel, std::min(bytesPerPixel, 4));
					sample[0] = channelValue(packedPixel, inputFormat->Rmask,
						inputFormat->Rshift, inputFormat->Rbits);
					sample[1] = channelValue(packedPixel, inputFormat->Gmask,
						inputFormat->Gshift, inputFormat->Gbits);
					sample[2] = channelValue(packedPixel, inputFormat->Bmask,
						inputFormat->Bshift, inputFormat->Bbits);
					return sample;
				};
				const auto topLeft = readPixel(x0, y0);
				const auto topRight = readPixel(x1, y0);
				const auto bottomLeft = readPixel(x0, y1);
				const auto bottomRight = readPixel(x1, y1);
				auto bilinear = [fx, fy](float topLeftValue, float topRightValue,
					float bottomLeftValue, float bottomRightValue) {
					const float top = topLeftValue + (topRightValue - topLeftValue) * fx;
					const float bottom = bottomLeftValue +
						(bottomRightValue - bottomLeftValue) * fx;
					return top + (bottom - top) * fy;
				};
				red = bilinear(topLeft[0], topRight[0], bottomLeft[0], bottomRight[0]);
				green = bilinear(topLeft[1], topRight[1], bottomLeft[1], bottomRight[1]);
				blue = bilinear(topLeft[2], topRight[2], bottomLeft[2], bottomRight[2]);
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
			error = "Depth model did not return a tensor output.";
			return result;
		}
		const auto outputInfo = outputs[0].GetTensorTypeAndShapeInfo();
		const auto outputShape = outputInfo.GetShape();
		if (outputShape.size() == 4 && outputShape[0] == 1 && outputShape[1] == 1) {
			if (outputShape[2] <= 0 || outputShape[3] <= 0 ||
				outputShape[2] > std::numeric_limits<int>::max() ||
				outputShape[3] > std::numeric_limits<int>::max()) {
				error = "Depth output has dynamic or invalid dimensions.";
				return result;
			}
			result.height = static_cast<int>(outputShape[2]);
			result.width = static_cast<int>(outputShape[3]);
		} else if (outputShape.size() == 3 && outputShape[0] == 1) {
			if (outputShape[1] <= 0 || outputShape[2] <= 0 ||
				outputShape[1] > std::numeric_limits<int>::max() ||
				outputShape[2] > std::numeric_limits<int>::max()) {
				error = "Depth output has dynamic or invalid dimensions.";
				return result;
			}
			result.height = static_cast<int>(outputShape[1]);
			result.width = static_cast<int>(outputShape[2]);
		} else {
			error = "Depth output must have shape [1, 1, H, W] or [1, H, W].";
			return result;
		}
		const size_t outputSize = static_cast<size_t>(result.width) *
			static_cast<size_t>(result.height);
		if (outputSize != static_cast<size_t>(result.width) * static_cast<size_t>(result.height)) {
			error = "Depth output dimensions do not match its tensor size.";
			return result;
		}
		const float* outputData = outputs[0].GetTensorData<float>();
		// Crop by output-pixel centers rather than rounding the rectangle
		// boundaries. This prevents a letterbox/border row from being included
		// when the model output and input sizes do not map exactly.
		const auto firstPixelAtOrInside = [](int boundary, int outputSize,
			int processSize) {
			return static_cast<int>(std::ceil(
				static_cast<double>(boundary) * outputSize / processSize - 0.5));
		};
		const int cropX0 = std::clamp(firstPixelAtOrInside(
			contentOffsetX, result.width, processWidth), 0, result.width - 1);
		const int cropY0 = std::clamp(firstPixelAtOrInside(
			contentOffsetY, result.height, processHeight), 0, result.height - 1);
		const int cropX1 = std::clamp(firstPixelAtOrInside(
			contentOffsetX + contentWidth, result.width, processWidth), cropX0 + 1, result.width);
		const int cropY1 = std::clamp(firstPixelAtOrInside(
			contentOffsetY + contentHeight, result.height, processHeight), cropY0 + 1, result.height);
		const int croppedWidth = cropX1 - cropX0;
		const int croppedHeight = cropY1 - cropY0;
		std::vector<float> croppedValues(static_cast<size_t>(croppedWidth) * croppedHeight);
		for (int y = 0; y < croppedHeight; ++y) {
			std::copy_n(outputData + static_cast<size_t>(cropY0 + y) * result.width + cropX0,
				croppedWidth, croppedValues.data() + static_cast<size_t>(y) * croppedWidth);
		}
		// Depth models can emit extreme-valued pixels immediately inside the letterbox
		// crop. Reuse the nearest interior pixels as a two-pixel gutter on every
		// side without changing the output dimensions.
		constexpr int gutter = 2;
		if (croppedWidth > gutter * 2 && croppedHeight > gutter * 2) {
			const int lastInteriorX = croppedWidth - gutter - 1;
			const int lastInteriorY = croppedHeight - gutter - 1;
			for (int y = 0; y < croppedHeight; ++y) {
				const int sourceY = std::clamp(y, gutter, lastInteriorY);
				for (int x = 0; x < croppedWidth; ++x) {
					const int sourceX = std::clamp(x, gutter, lastInteriorX);
					if (sourceX != x || sourceY != y)
						croppedValues[static_cast<size_t>(y) * croppedWidth + x] =
							croppedValues[static_cast<size_t>(sourceY) * croppedWidth + sourceX];
				}
			}
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
