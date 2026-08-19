// Copyright (c) 2026 Outmode
//
// SPDX-License-Identifier: MIT

#include "DepthEstimator.h"
#include "SDL3_image/SDL_image.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

static bool gpuUpscaleDepth(const std::vector<Uint32>& sourcePixels,
	int sourceWidth, int sourceHeight, int outputWidth, int outputHeight,
	std::vector<Uint32>& outputPixels) {
	SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
	if (device == nullptr) return false;
	SDL_GPUTextureCreateInfo textureInfo{};
	textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
	textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	textureInfo.width = static_cast<Uint32>(sourceWidth);
	textureInfo.height = static_cast<Uint32>(sourceHeight);
	textureInfo.layer_count_or_depth = 1;
	textureInfo.num_levels = 1;
	SDL_GPUTexture* sourceTexture = SDL_CreateGPUTexture(device, &textureInfo);
	textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	textureInfo.width = static_cast<Uint32>(outputWidth);
	textureInfo.height = static_cast<Uint32>(outputHeight);
	SDL_GPUTexture* outputTexture = SDL_CreateGPUTexture(device, &textureInfo);
	SDL_GPUTransferBufferCreateInfo uploadInfo{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = static_cast<Uint32>(sourcePixels.size() * sizeof(Uint32))};
	SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(device, &uploadInfo);
	SDL_GPUTransferBufferCreateInfo downloadInfo{
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
		.size = static_cast<Uint32>(outputWidth * outputHeight * sizeof(Uint32))};
	SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(device, &downloadInfo);
	bool success = sourceTexture != nullptr && outputTexture != nullptr &&
		upload != nullptr && download != nullptr;
	if (success) {
		auto* mapped = static_cast<Uint32*>(SDL_MapGPUTransferBuffer(device, upload, false));
		if (mapped == nullptr) success = false;
		else {
			SDL_memcpy(mapped, sourcePixels.data(), sourcePixels.size() * sizeof(Uint32));
			SDL_UnmapGPUTransferBuffer(device, upload);
		}
	}
	if (success) {
		SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
		SDL_GPUTextureTransferInfo uploadInfoGPU{.transfer_buffer = upload};
		SDL_GPUTextureRegion sourceRegion{.texture = sourceTexture,
			.w = static_cast<Uint32>(sourceWidth), .h = static_cast<Uint32>(sourceHeight), .d = 1};
		SDL_UploadToGPUTexture(copy, &uploadInfoGPU, &sourceRegion, false);
		SDL_EndGPUCopyPass(copy);
		SDL_GPUBlitInfo blit{};
		blit.source = {.texture = sourceTexture, .w = static_cast<Uint32>(sourceWidth),
			.h = static_cast<Uint32>(sourceHeight)};
		blit.destination = {.texture = outputTexture, .w = static_cast<Uint32>(outputWidth),
			.h = static_cast<Uint32>(outputHeight)};
		blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
		blit.filter = SDL_GPU_FILTER_LINEAR;
		SDL_BlitGPUTexture(commands, &blit);
		copy = SDL_BeginGPUCopyPass(commands);
		SDL_GPUTextureTransferInfo downloadInfoGPU{.transfer_buffer = download};
		SDL_GPUTextureRegion outputRegion{.texture = outputTexture,
			.w = static_cast<Uint32>(outputWidth), .h = static_cast<Uint32>(outputHeight), .d = 1};
		SDL_DownloadFromGPUTexture(copy, &outputRegion, &downloadInfoGPU);
		SDL_EndGPUCopyPass(copy);
		SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
		success = fence != nullptr && SDL_WaitForGPUFences(device, true, &fence, 1);
		if (fence != nullptr) SDL_ReleaseGPUFence(device, fence);
		if (success) {
			auto* mapped = static_cast<Uint32*>(SDL_MapGPUTransferBuffer(device, download, false));
			if (mapped == nullptr) success = false;
			else {
				outputPixels.assign(mapped, mapped + outputWidth * outputHeight);
				SDL_UnmapGPUTransferBuffer(device, download);
			}
		}
	}
	if (upload != nullptr) SDL_ReleaseGPUTransferBuffer(device, upload);
	if (download != nullptr) SDL_ReleaseGPUTransferBuffer(device, download);
	if (sourceTexture != nullptr) SDL_ReleaseGPUTexture(device, sourceTexture);
	if (outputTexture != nullptr) SDL_ReleaseGPUTexture(device, outputTexture);
	SDL_DestroyGPUDevice(device);
	return success;
}

static float catmullRomWeight(float distance) {
	const float x = std::abs(distance);
	if (x <= 1.0f) return 1.5f * x * x * x - 2.5f * x * x + 1.0f;
	if (x < 2.0f) return -0.5f * x * x * x + 2.5f * x * x - 4.0f * x + 2.0f;
	return 0.0f;
}

static float sampleCatmullRomDepth(const std::vector<float>& source,
		int sourceWidth, int sourceHeight, float x, float y) {
	const int baseX = static_cast<int>(std::floor(x));
	const int baseY = static_cast<int>(std::floor(y));
	const float fractionX = x - baseX;
	const float fractionY = y - baseY;
	float valueSum = 0.0f;
	float totalWeight = 0.0f;
	for (int sampleY = -1; sampleY <= 2; ++sampleY) {
		const int sourceY = std::clamp(baseY + sampleY, 0, sourceHeight - 1);
		const float yWeight = catmullRomWeight(static_cast<float>(sampleY) - fractionY);
		for (int sampleX = -1; sampleX <= 2; ++sampleX) {
			const int sourceX = std::clamp(baseX + sampleX, 0, sourceWidth - 1);
			const float weight = yWeight * catmullRomWeight(
				static_cast<float>(sampleX) - fractionX);
			if (weight == 0.0f) continue;
			valueSum += source[static_cast<size_t>(sourceY) * sourceWidth + sourceX] * weight;
			totalWeight += weight;
		}
	}
	return std::abs(totalWeight) > std::numeric_limits<float>::epsilon()
		? valueSum / totalWeight : 0.0f;
}

int main(int argc, char** argv) {
	if (argc < 4) {
		std::cerr << "Usage: DepthTest <model.onnx> <input-image> <output-depth.png> "
			"[process-size] [--provider auto|cpu|cuda|rocm] [--upscale cpu|gpu]\n";
		return 2;
	}

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	SDL_Surface* input = IMG_Load(argv[2]);
	if (input == nullptr) {
		std::cerr << "Image load failed: " << SDL_GetError() << '\n';
		SDL_Quit();
		return 1;
	}
	const int sourceWidth = input->w;
	const int sourceHeight = input->h;

	DepthEstimator::Config config;
	config.modelPath = argv[1];
	bool useGpuUpscale = false;
	for (int argument = 4; argument < argc; ++argument) {
		const std::string value = argv[argument];
		if (value == "--provider" && argument + 1 < argc) {
			const std::string provider = argv[++argument];
			if (provider == "auto") config.provider = DepthEstimator::Provider::Auto;
			else if (provider == "cpu") config.provider = DepthEstimator::Provider::CPU;
			else if (provider == "cuda") config.provider = DepthEstimator::Provider::CUDA;
			else if (provider == "rocm") config.provider = DepthEstimator::Provider::ROCM;
			else {
				std::cerr << "Unknown provider: " << provider << '\n';
				return 2;
			}
		} else if (value == "--upscale" && argument + 1 < argc) {
			const std::string upscale = argv[++argument];
			if (upscale == "gpu") useGpuUpscale = true;
			else if (upscale != "cpu") {
				std::cerr << "Unknown upscale mode: " << upscale << '\n';
				return 2;
			}
		} else if (value.rfind("--", 0) == 0) {
			std::cerr << "Unknown option: " << value << '\n';
			return 2;
		} else if (argument == 4) {
			try {
				config.processSize = std::max(14, std::stoi(value));
			} catch (...) {
				std::cerr << "Invalid process size: " << value << '\n';
				return 2;
			}
		} else {
			std::cerr << "Unexpected argument: " << value << '\n';
			return 2;
		}
	}

	DepthEstimator estimator;
	std::string error;
	if (!estimator.load(config, error)) {
		std::cerr << "Model load failed: " << error << '\n';
		SDL_DestroySurface(input);
		SDL_Quit();
		return 1;
	}

	auto depth = estimator.predict(input, error);
	SDL_DestroySurface(input);
	if (!depth.valid()) {
		std::cerr << "Inference failed: " << error << '\n';
		SDL_Quit();
		return 1;
	}

	const auto minMax = std::minmax_element(depth.values.begin(), depth.values.end());
	const float minimum = *minMax.first;
	const float maximum = *minMax.second;
	const float range = maximum - minimum;
	const auto* format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
	if (useGpuUpscale) {
		std::vector<Uint32> lowResolution(static_cast<size_t>(depth.width * depth.height));
		for (int y = 0; y < depth.height; ++y) {
			for (int x = 0; x < depth.width; ++x) {
				const float normalized = range > std::numeric_limits<float>::epsilon()
					? (depth.values[static_cast<size_t>(y * depth.width + x)] - minimum) / range : 0.0f;
				const Uint8 value = static_cast<Uint8>((1.0f -
					std::clamp(normalized, 0.0f, 1.0f)) * 255.0f);
				lowResolution[static_cast<size_t>(y * depth.width + x)] =
					SDL_MapRGBA(format, nullptr, value, value, value, 255);
			}
		}
		std::vector<Uint32> gpuPixels;
		if (!gpuUpscaleDepth(lowResolution, depth.width, depth.height,
			sourceWidth, sourceHeight, gpuPixels)) {
			std::cerr << "GPU upscale failed: " << SDL_GetError()
				<< "; use --upscale cpu to use the fallback.\n";
			SDL_Quit();
			return 1;
		}
		std::vector<Uint32> pixels = std::move(gpuPixels);
		SDL_Surface* output = SDL_CreateSurfaceFrom(sourceWidth, sourceHeight,
			SDL_PIXELFORMAT_RGBA32, pixels.data(), sourceWidth * static_cast<int>(sizeof(Uint32)));
		if (output == nullptr || !IMG_SavePNG(output, argv[3])) {
			std::cerr << "Depth image save failed: " << SDL_GetError() << '\n';
			if (output != nullptr) SDL_DestroySurface(output);
			SDL_Quit();
			return 1;
		}
		std::cout << "Provider: " << estimator.providerName() << '\n'
			<< "Model output: " << depth.width << "x" << depth.height << '\n'
			<< "Export output: " << sourceWidth << "x" << sourceHeight << '\n'
			<< "Upscale: GPU linear blit\n"
			<< "Metric: " << (depth.metric ? "yes" : "no") << '\n'
			<< "Range: " << minimum << " .. " << maximum << '\n'
			<< "Saved: " << argv[3] << '\n';
		SDL_DestroySurface(output);
		SDL_Quit();
		return 0;
	}
	std::vector<Uint32> pixels(static_cast<size_t>(sourceWidth * sourceHeight));
	for (int y = 0; y < sourceHeight; ++y) {
		const float sourceY = sourceHeight > 1
			? static_cast<float>(y) * (depth.height - 1) / (sourceHeight - 1) : 0.0f;
		for (int x = 0; x < sourceWidth; ++x) {
			const float sourceX = sourceWidth > 1
				? static_cast<float>(x) * (depth.width - 1) / (sourceWidth - 1) : 0.0f;
			const float resizedValue = sampleCatmullRomDepth(
				depth.values, depth.width, depth.height, sourceX, sourceY);
			const float normalized = range > std::numeric_limits<float>::epsilon()
				? (resizedValue - minimum) / range : 0.0f;
			const Uint8 value = static_cast<Uint8>((1.0f -
				std::clamp(normalized, 0.0f, 1.0f)) * 255.0f);
			pixels[static_cast<size_t>(y * sourceWidth + x)] = SDL_MapRGBA(format, nullptr, value, value, value, 255);
		}
	}

	SDL_Surface* output = SDL_CreateSurfaceFrom(sourceWidth, sourceHeight,
		SDL_PIXELFORMAT_RGBA32, pixels.data(), sourceWidth * static_cast<int>(sizeof(Uint32)));
	if (output == nullptr || !IMG_SavePNG(output, argv[3])) {
		std::cerr << "Depth image save failed: " << SDL_GetError() << '\n';
		if (output != nullptr) SDL_DestroySurface(output);
		SDL_Quit();
		return 1;
	}

	std::cout << "Provider: " << estimator.providerName() << '\n'
		<< "Model output: " << depth.width << "x" << depth.height << '\n'
		<< "Export output: " << sourceWidth << "x" << sourceHeight << '\n'
		<< "Metric: " << (depth.metric ? "yes" : "no") << '\n'
		<< "Range: " << minimum << " .. " << maximum << '\n'
		<< "Saved: " << argv[3] << '\n';

	SDL_DestroySurface(output);
	SDL_Quit();
	return 0;
}
