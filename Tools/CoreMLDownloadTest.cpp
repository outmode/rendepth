// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "ModelDownloader.h"

#include <iostream>

int main(int argc, char** argv) {
	if (argc != 3) {
		std::cerr << "Usage: CoreMLDownloadTest <model-directory> <model.onnx>\n";
		return 2;
	}
	std::string error;
	ModelDownloader::Progress progress;
	const auto path = ModelDownloader::ensureCoreMLAvailable(
		argv[1], argv[2], error, &progress);
	if (path.empty()) {
		std::cerr << error << '\n';
		return 1;
	}
	std::cout << path << '\n';
	return 0;
}
