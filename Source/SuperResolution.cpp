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
#ifdef RENDEPTH_ENABLE_COREML
#include "CoreMLInference.h"
#include "CoreMLSuperResolutionModel.h"
#endif
#ifdef RENDEPTH_DYNAMIC_ONNX_RUNTIME
#include "InferenceRuntime.h"
#endif
#endif

struct SuperResolution::State {
#ifdef RENDEPTH_ENABLE_ONNX_RUNTIME
	Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "RendepthSuperResolution"};
	Ort::SessionOptions sessionOptions;
	std::unique_ptr<Ort::Session> session;
	std::string inputName;
	std::string outputName;
	int inputWidth = 0;
	int inputHeight = 0;
#ifdef RENDEPTH_ENABLE_COREML
	std::unique_ptr<CoreMLSuperResolutionModel> nativeCoreML;
#endif
#endif
};

// Release the model session when the upscaler is destroyed.
SuperResolution::~SuperResolution() {
	unload();
}

// Unload the super-resolution model and clear its provider label.
void SuperResolution::unload() {
	delete state;
	state = nullptr;
	activeProvider = "Unavailable";
}

// Load the upscaling model and validate its tensor layout and execution provider.
bool SuperResolution::load(const Config& config, std::string& error) {
	unload();
	error.clear();
#ifndef RENDEPTH_ENABLE_ONNX_RUNTIME
	(void)config;
	error = "ONNX Runtime support is not enabled.";
	return false;
#else
	#ifndef RENDEPTH_DYNAMIC_ONNX_RUNTIME
	const std::string runtimeVersion = OrtGetApiBase()->GetVersionString();
	#ifdef RENDEPTH_ENABLE_COREML
	constexpr const char* requiredVersion = "1.30.";
	#else
	constexpr const char* requiredVersion = "1.22.";
	#endif
	if (runtimeVersion.rfind(requiredVersion, 0) != 0) {
		error = "Rendepth requires ONNX Runtime " + std::string(requiredVersion) + "x; loaded " + runtimeVersion + ".";
		return false;
	}
	#endif
	const bool nativeModelPath = config.provider == DepthEstimator::Provider::CoreML &&
		config.modelPath.extension() == ".mlmodelc" &&
		std::filesystem::is_directory(config.modelPath);
	if (config.modelPath.empty() ||
		(!nativeModelPath && !std::filesystem::is_regular_file(config.modelPath))) {
		error = "SR model was not found: " + config.modelPath.string();
		return false;
	}
#ifdef RENDEPTH_DYNAMIC_ONNX_RUNTIME
	if (!InferenceRuntime::initialize(config.provider, error)) return false;
	const auto provider = config.provider == DepthEstimator::Provider::CPU
		? DepthEstimator::Provider::CPU : InferenceRuntime::provider();
#else
#ifdef RENDEPTH_ENABLE_COREML
	const auto provider = CoreMLInference::resolve(config.provider);
#else
	const auto provider = config.provider;
#endif
#endif
	std::unique_ptr<State> nextState;
	try {
		nextState = std::make_unique<State>();
#ifdef RENDEPTH_ENABLE_COREML
		if (provider == DepthEstimator::Provider::CoreML &&
			config.inputWidth >= 16 && config.inputWidth <= 720 &&
			config.inputHeight >= 16 && config.inputHeight <= 720) {
			auto nativePath = config.modelPath;
			if (!nativeModelPath) nativePath.replace_extension(".mlmodelc");
			if (std::filesystem::is_directory(nativePath))
				nextState->nativeCoreML = CoreMLSuperResolutionModel::load(nativePath, error);
			else error = "compiled model was not found";
			if (nextState->nativeCoreML) {
				activeProvider = "Native Core ML (CPU + GPU)";
				state = nextState.release();
				return true;
			}
			SDL_Log("Native Core ML super-resolution model unavailable (%s); using CPU: %s",
				nativePath.string().c_str(), error.c_str());
			if (nativeModelPath) return false;
			nextState.reset();
			auto cpuConfig = config;
			cpuConfig.provider = DepthEstimator::Provider::CPU;
			return load(cpuConfig, error);
		}
#endif
		if (config.intraOpThreads > 0)
			nextState->sessionOptions.SetIntraOpNumThreads(static_cast<int>(config.intraOpThreads));
		nextState->sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		if (provider == DepthEstimator::Provider::CoreML) {
#ifdef RENDEPTH_ENABLE_COREML
			if (config.inputWidth > 0 && config.inputHeight > 0) {
				nextState->sessionOptions.AddFreeDimensionOverrideByName("height", config.inputHeight);
				nextState->sessionOptions.AddFreeDimensionOverrideByName("width", config.inputWidth);
				CoreMLInference::append(nextState->sessionOptions, config.modelPath,
					config.inputWidth, config.inputHeight);
				nextState->inputWidth = config.inputWidth;
				nextState->inputHeight = config.inputHeight;
			} else {
				Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(
					nextState->sessionOptions, COREML_FLAG_USE_CPU_AND_GPU));
			}
#else
			error = "Core ML requires a macOS build with Core ML support.";
			return false;
#endif
		} else if (provider == DepthEstimator::Provider::CUDA) {
#if defined(RENDEPTH_ENABLE_CUDA) || defined(RENDEPTH_DYNAMIC_ONNX_RUNTIME)
			OrtCUDAProviderOptions options{};
			nextState->sessionOptions.AppendExecutionProvider_CUDA(options);
#else
			error = "CUDA support is not enabled.";
			return false;
#endif
		} else if (provider == DepthEstimator::Provider::DirectML) {
#if defined(_WIN32) && defined(RENDEPTH_DYNAMIC_ONNX_RUNTIME)
			InferenceRuntime::appendDirectML(nextState->sessionOptions);
#else
			error = "DirectML requires a Windows runtime pack.";
			return false;
#endif
		} else if (provider == DepthEstimator::Provider::ROCM) {
#if defined(RENDEPTH_ENABLE_ROCM) || defined(RENDEPTH_DYNAMIC_ONNX_RUNTIME)
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
#ifdef RENDEPTH_ENABLE_COREML
		if (provider == DepthEstimator::Provider::CoreML) {
			SDL_Log("Core ML super resolution model unavailable; using CPU: %s", error.c_str());
			nextState.reset();
			auto cpuConfig = config;
			cpuConfig.provider = DepthEstimator::Provider::CPU;
			return load(cpuConfig, error);
		}
#endif
#ifdef RENDEPTH_DYNAMIC_ONNX_RUNTIME
		if (provider != DepthEstimator::Provider::CPU) {
			InferenceRuntime::useCpuFallback(error);
			nextState.reset();
			auto cpuConfig = config;
			cpuConfig.provider = DepthEstimator::Provider::CPU;
			return load(cpuConfig, error);
		}
#endif
		return false;
	}
	switch (provider) {
		case DepthEstimator::Provider::CoreML: activeProvider = "Core ML (CPU + GPU)"; break;
		case DepthEstimator::Provider::CUDA: activeProvider = "CUDA"; break;
		case DepthEstimator::Provider::ROCM: activeProvider = "ROCm"; break;
		case DepthEstimator::Provider::DirectML: activeProvider = "DirectML"; break;
		default: activeProvider = "CPU"; break;
	}
	state = nextState.release();
	return true;
#endif
}

// Return the label for the execution provider actually in use.
const std::string& SuperResolution::providerName() const {
	return activeProvider;
}

// Convert image pixels to model input and return the inferred enlarged image as an RGBA surface.
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
	if ((state->inputWidth > 0 && modelWidth != state->inputWidth) ||
		(state->inputHeight > 0 && modelHeight != state->inputHeight)) {
		error = "Super-resolution input size differs from the loaded Core ML model.";
		return nullptr;
	}
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
		std::vector<float> outputValues;
		std::vector<Ort::Value> ortOutputs;
		const float* ortValues = nullptr;
		const Ort::Float16_t* ortValues16 = nullptr;
		int modelOutputHeight = 0;
		int modelOutputWidth = 0;
#ifdef RENDEPTH_ENABLE_COREML
		if (state->nativeCoreML) {
			if (!state->nativeCoreML->predict(input.data(), modelWidth, modelHeight,
				outputValues, modelOutputWidth, modelOutputHeight, error)) return nullptr;
		} else
#endif
		{
			Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
				OrtArenaAllocator, OrtMemTypeDefault);
			Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memoryInfo,
				input.data(), input.size(), shape.data(), shape.size());
			const char* inputNames[] = { state->inputName.c_str() };
			const char* outputNames[] = { state->outputName.c_str() };
			ortOutputs = state->session->Run(Ort::RunOptions{nullptr}, inputNames,
				&inputTensor, 1, outputNames, 1);
			if (ortOutputs.empty() || !ortOutputs[0].IsTensor()) {
				error = "SR model returned no tensor output.";
				return nullptr;
			}
			const auto typeInfo = ortOutputs[0].GetTensorTypeAndShapeInfo();
			const auto outputShape = typeInfo.GetShape();
			if (outputShape.size() != 4 || outputShape[0] != 1 || outputShape[1] != 3 ||
				outputShape[2] <= 0 || outputShape[3] <= 0) {
				error = "SR model returned an unsupported output shape.";
				return nullptr;
			}
			modelOutputHeight = static_cast<int>(outputShape[2]);
			modelOutputWidth = static_cast<int>(outputShape[3]);
			const auto elementType = typeInfo.GetElementType();
			if (elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
				ortValues = ortOutputs[0].GetTensorData<float>();
			} else if (elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
				ortValues16 = ortOutputs[0].GetTensorData<Ort::Float16_t>();
			} else {
				error = "SR model output must use float32 or float16 values.";
				return nullptr;
			}
		}
		const double scaleX = static_cast<double>(modelOutputWidth) / modelWidth;
		const double scaleY = static_cast<double>(modelOutputHeight) / modelHeight;
		const int outputWidth = std::max(1, static_cast<int>(std::lround(image->w * scaleX)));
		const int outputHeight = std::max(1, static_cast<int>(std::lround(image->h * scaleY)));
		SDL_Surface* result = SDL_CreateSurface(outputWidth, outputHeight,
			SDL_PIXELFORMAT_RGBA32);
		if (result == nullptr) {
			error = SDL_GetError();
			return nullptr;
		}
		const auto* outputFormat = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
		const size_t modelOutputPlane = static_cast<size_t>(modelOutputWidth) * modelOutputHeight;
		const auto readValue = [&outputValues, ortValues, ortValues16](size_t index) {
			return ortValues != nullptr ? ortValues[index] :
				ortValues16 != nullptr ? ortValues16[index].ToFloat() : outputValues[index];
		};
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
