// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "SuperResolution.h"
#include "SDL3_image/SDL_image.h"

#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

static bool isImagePath(const std::filesystem::path& path) {
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
		extension == ".bmp" || extension == ".tga";
}

static bool processImage(SuperResolution& estimator,
	const std::filesystem::path& inputPath, const std::filesystem::path& outputPath,
	int repeats, const std::string& modelName) {
	SDL_Surface* input = IMG_Load(inputPath.string().c_str());
	if (input == nullptr) {
		std::cerr << inputPath << ": image load failed: " << SDL_GetError() << '\n';
		return false;
	}
	SDL_Surface* output = nullptr;
	std::string error;
	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < repeats; ++i) {
		SDL_DestroySurface(output);
		output = estimator.predict(input, error);
		if (output == nullptr) {
			std::cerr << inputPath << ": inference failed: " << error << '\n';
			SDL_DestroySurface(input);
			return false;
		}
	}
	const auto elapsed = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count() / repeats;
	const bool saved = IMG_Save(output, outputPath.string().c_str());
	if (!saved)
		std::cerr << outputPath << ": image save failed: " << SDL_GetError() << '\n';
	else
		std::cout << inputPath << " -> " << outputPath << " model=" << modelName
			<< " provider="
			<< estimator.providerName() << " input=" << input->w << 'x' << input->h
			<< " output=" << output->w << 'x' << output->h
			<< " average_ms=" << elapsed << '\n';
	SDL_DestroySurface(output);
	SDL_DestroySurface(input);
	return saved;
}

int main(int argc, char** argv) {
	if (argc < 4) {
		std::cerr << "Usage: SuperTest <model.onnx> <input-file-or-directory> "
			"<output-file-or-directory> "
			"[--provider auto|cpu|cuda|rocm] [--repeat N] [--name rfdn|ecbsr]\n";
		return 2;
	}
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	SuperResolution::Config config;
	config.modelPath = argv[1];
	std::string modelName = config.modelPath.stem().string();
	int repeats = 1;
	for (int i = 4; i < argc; ++i) {
		const std::string option = argv[i];
		if (option == "--provider" && i + 1 < argc) {
			const std::string provider = argv[++i];
			if (provider == "cpu" || provider == "auto") config.provider = DepthEstimator::Provider::CPU;
			else if (provider == "cuda") config.provider = DepthEstimator::Provider::CUDA;
			else if (provider == "rocm") config.provider = DepthEstimator::Provider::ROCM;
			else { std::cerr << "Unknown provider: " << provider << '\n'; return 2; }
		} else if (option == "--repeat" && i + 1 < argc) {
			repeats = std::max(1, std::stoi(argv[++i]));
		} else if (option == "--name" && i + 1 < argc) {
			modelName = argv[++i];
		} else {
			std::cerr << "Unknown option: " << option << '\n';
			return 2;
		}
	}
	SuperResolution estimator;
	std::string error;
	if (!estimator.load(config, error)) {
		std::cerr << "Model load failed: " << error << '\n';
		SDL_Quit(); return 1;
	}
	const std::filesystem::path inputPath = argv[2];
	const std::filesystem::path outputPath = argv[3];
	std::error_code filesystemError;
	if (std::filesystem::is_directory(inputPath, filesystemError)) {
		if (!std::filesystem::create_directories(outputPath, filesystemError) && filesystemError) {
			std::cerr << "Could not create output directory " << outputPath << ": "
				<< filesystemError.message() << '\n';
			SDL_Quit(); return 1;
		}
		std::vector<std::filesystem::path> inputs;
		for (const auto& entry : std::filesystem::directory_iterator(inputPath, filesystemError)) {
			if (filesystemError) break;
			if (entry.is_regular_file() && isImagePath(entry.path()))
				inputs.push_back(entry.path());
		}
		if (filesystemError) {
			std::cerr << "Could not enumerate input directory: " << filesystemError.message() << '\n';
			SDL_Quit(); return 1;
		}
		std::sort(inputs.begin(), inputs.end());
		if (inputs.empty()) {
			std::cerr << "No supported images found in " << inputPath << '\n';
			SDL_Quit(); return 1;
		}
		int failures = 0;
		for (const auto& imagePath : inputs) {
			const auto destination = outputPath / imagePath.filename();
			if (!processImage(estimator, imagePath, destination, repeats, modelName)) ++failures;
		}
		SDL_Quit();
		return failures == 0 ? 0 : 1;
	}
	if (filesystemError || !std::filesystem::is_regular_file(inputPath, filesystemError)) {
		std::cerr << "Input is not a regular file or directory: " << inputPath << '\n';
		SDL_Quit(); return 1;
	}
	if (!outputPath.parent_path().empty())
		std::filesystem::create_directories(outputPath.parent_path(), filesystemError);
	const bool success = processImage(estimator, inputPath, outputPath, repeats, modelName);
	SDL_Quit();
	return success ? 0 : 1;
}
