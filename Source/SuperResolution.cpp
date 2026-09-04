// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "SuperResolution.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#ifdef RENDEPTH_ENABLE_ONNX_RUNTIME
#include <onnxruntime_cxx_api.h>
#endif

struct SuperResolution::State {
#ifdef RENDEPTH_ENABLE_ONNX_RUNTIME
	Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "RendepthSuperResolution"};
	Ort::SessionOptions sessionOptions;
	std::unique_ptr<Ort::Session> session;
	std::string inputName;
	std::string outputName;
#endif
};

SuperResolution::~SuperResolution() {
	unload();
}

void SuperResolution::unload() {
	delete state;
	state = nullptr;
	activeProvider = "Unavailable";
}

bool SuperResolution::load(const Config& config, std::string& error) {
	unload();
#ifndef RENDEPTH_ENABLE_ONNX_RUNTIME
	(void)config;
	error = "ONNX Runtime support is not enabled.";
	return false;
#else
	if (config.modelPath.empty() || !std::filesystem::is_regular_file(config.modelPath)) {
		error = "SR model was not found: " + config.modelPath.string();
		return false;
	}
	auto nextState = std::make_unique<State>();
	if (config.intraOpThreads > 0)
		nextState->sessionOptions.SetIntraOpNumThreads(static_cast<int>(config.intraOpThreads));
	nextState->sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	try {
		if (config.provider == DepthEstimator::Provider::CUDA) {
#ifdef RENDEPTH_ENABLE_CUDA
			OrtCUDAProviderOptions options{};
			nextState->sessionOptions.AppendExecutionProvider_CUDA(options);
#else
			error = "CUDA support is not enabled.";
			return false;
#endif
		} else if (config.provider == DepthEstimator::Provider::ROCM) {
#ifdef RENDEPTH_ENABLE_ROCM
			OrtROCMProviderOptions options{};
			nextState->sessionOptions.AppendExecutionProvider_ROCM(options);
#else
			error = "ROCm support is not enabled.";
			return false;
#endif
		}
		nextState->session = std::make_unique<Ort::Session>(
			nextState->environment, config.modelPath.c_str(), nextState->sessionOptions);
		Ort::AllocatorWithDefaultOptions allocator;
		auto inputName = nextState->session->GetInputNameAllocated(0, allocator);
		nextState->inputName = inputName.get();
		auto outputName = nextState->session->GetOutputNameAllocated(0, allocator);
		nextState->outputName = outputName.get();
	} catch (const Ort::Exception& exception) {
		error = exception.what();
		return false;
	}
	switch (config.provider) {
		case DepthEstimator::Provider::CUDA: activeProvider = "CUDA"; break;
		case DepthEstimator::Provider::ROCM: activeProvider = "ROCm"; break;
		default: activeProvider = "CPU"; break;
	}
	state = nextState.release();
	return true;
#endif
}

const std::string& SuperResolution::providerName() const {
	return activeProvider;
}

SDL_Surface* SuperResolution::predict(const SDL_Surface* image, std::string& error) const {
#ifndef RENDEPTH_ENABLE_ONNX_RUNTIME
	(void)image;
	error = "ONNX Runtime support is not enabled.";
	return nullptr;
#else
	if (state == nullptr || image == nullptr) {
		error = "SR model is not loaded or input image is null.";
		return nullptr;
	}
	const auto* format = SDL_GetPixelFormatDetails(image->format);
	const int bytesPerPixel = SDL_BYTESPERPIXEL(image->format);
	if (format == nullptr || bytesPerPixel <= 0) {
		error = "Input image has an unknown pixel format.";
		return nullptr;
	}
	// RRDB/pixel-shuffle exports require spatial dimensions divisible by four.
	// Pad by extending the edge pixels, then crop the result back to the exact
	// scale of the original image. This keeps arbitrary source dimensions valid
	// without introducing a visible border into the neural inference.
	constexpr int modelDimensionMultiple = 4;
	const int modelWidth = (image->w + modelDimensionMultiple - 1) /
		modelDimensionMultiple * modelDimensionMultiple;
	const int modelHeight = (image->h + modelDimensionMultiple - 1) /
		modelDimensionMultiple * modelDimensionMultiple;
	const size_t planeSize = static_cast<size_t>(modelWidth) * modelHeight;
	std::vector<float> input(planeSize * 3);
	for (int y = 0; y < modelHeight; ++y) {
		for (int x = 0; x < modelWidth; ++x) {
			const int sourceX = std::min(x, image->w - 1);
			const int sourceY = std::min(y, image->h - 1);
			const auto* pixel = static_cast<const Uint8*>(image->pixels) +
				static_cast<size_t>(sourceY) * image->pitch + sourceX * bytesPerPixel;
			Uint32 packed = 0;
			std::memcpy(&packed, pixel, std::min(bytesPerPixel, 4));
			Uint8 red = 0, green = 0, blue = 0, alpha = 255;
			SDL_GetRGBA(packed, format, nullptr, &red, &green, &blue, &alpha);
			const size_t index = static_cast<size_t>(y) * modelWidth + x;
			input[index] = red / 255.0f;
			input[planeSize + index] = green / 255.0f;
			input[2 * planeSize + index] = blue / 255.0f;
		}
	}
	const std::array<int64_t, 4> shape{1, 3, modelHeight, modelWidth};
	try {
		Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
			OrtArenaAllocator, OrtMemTypeDefault);
		Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memoryInfo,
			input.data(), input.size(), shape.data(), shape.size());
		const char* inputNames[] = { state->inputName.c_str() };
		const char* outputNames[] = { state->outputName.c_str() };
		auto outputs = state->session->Run(Ort::RunOptions{nullptr}, inputNames,
			&inputTensor, 1, outputNames, 1);
		if (outputs.empty() || !outputs[0].IsTensor()) {
			error = "SR model returned no tensor output.";
			return nullptr;
		}
		const auto typeInfo = outputs[0].GetTensorTypeAndShapeInfo();
		const auto outputShape = typeInfo.GetShape();
		if (outputShape.size() != 4 || outputShape[0] != 1 || outputShape[1] != 3 ||
			outputShape[2] <= 0 || outputShape[3] <= 0) {
			error = "SR model returned an unsupported output shape.";
			return nullptr;
		}
		const int modelOutputHeight = static_cast<int>(outputShape[2]);
		const int modelOutputWidth = static_cast<int>(outputShape[3]);
		const double scaleX = static_cast<double>(modelOutputWidth) / modelWidth;
		const double scaleY = static_cast<double>(modelOutputHeight) / modelHeight;
		const int outputWidth = std::max(1, static_cast<int>(std::lround(image->w * scaleX)));
		const int outputHeight = std::max(1, static_cast<int>(std::lround(image->h * scaleY)));
		const auto elementType = typeInfo.GetElementType();
		if (elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
			elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
			error = "SR model output must use float32 or float16 values.";
			return nullptr;
		}
		const float* values = elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
			? outputs[0].GetTensorData<float>() : nullptr;
		const Ort::Float16_t* values16 = elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
			? outputs[0].GetTensorData<Ort::Float16_t>() : nullptr;
		const auto readValue = [values, values16](size_t index) {
			return values != nullptr ? values[index] : values16[index].ToFloat();
		};
		SDL_Surface* result = SDL_CreateSurface(outputWidth, outputHeight,
			SDL_PIXELFORMAT_RGBA32);
		if (result == nullptr) {
			error = SDL_GetError();
			return nullptr;
		}
		const auto* outputFormat = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
		const size_t modelOutputPlane = static_cast<size_t>(modelOutputWidth) * modelOutputHeight;
		for (int y = 0; y < outputHeight; ++y) {
			auto* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(result->pixels) +
				static_cast<size_t>(y) * result->pitch);
			for (int x = 0; x < outputWidth; ++x) {
				// The model output is already at the requested integer scale. The
				// desired image is the top-left crop corresponding to the unpadded
				// source dimensions.
				const int modelX = std::min(modelOutputWidth - 1, x);
				const int modelY = std::min(modelOutputHeight - 1, y);
				const size_t index = static_cast<size_t>(modelY) * modelOutputWidth + modelX;
				const auto toByte = [](float value) -> Uint8 {
					return static_cast<Uint8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
				};
				row[x] = SDL_MapRGBA(outputFormat, nullptr, toByte(readValue(index)),
					toByte(readValue(modelOutputPlane + index)),
					toByte(readValue(2 * modelOutputPlane + index)), 255);
			}
		}
		return result;
	} catch (const Ort::Exception& exception) {
		error = exception.what();
		return nullptr;
	}
#endif
}
