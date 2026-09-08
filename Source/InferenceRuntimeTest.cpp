// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "InferenceRuntime.h"
#include "SuperResolution.h"

#include <iostream>
#include <dlfcn.h>

// Each invocation is a fresh process: replacing a live ORT core is forbidden.
int main(int argc, char** argv) {
    if (argc != 7 && argc != 9) {
        std::cerr << "Usage: InferenceRuntimeTest <app-root> <pack-root> <cpu|cuda|rocm> "
            "<cpu|cuda|rocm|unavailable> <status-substring> <installed|missing> [<depth-model> <sr-model>]\n";
        return 2;
    }
    using Provider = DepthEstimator::Provider;
    const std::string request = argv[3];
    const auto backend = request == "cuda" ? Provider::CUDA : request == "rocm" ? Provider::ROCM : Provider::CPU;
    InferenceRuntime::configure(argv[1], argv[2], backend);
    const bool installed = InferenceRuntime::installed(backend);
    if (installed != (std::string(argv[6]) == "installed")) {
        std::cerr << "Incorrect installed-pack status\n";
        return 1;
    }
    std::string error;
    const bool loaded = InferenceRuntime::initialize(Provider::Auto, error);
    if (loaded && InferenceRuntime::provider() != Provider::CPU &&
        !dlsym(RTLD_DEFAULT, "OrtGetApiBase")) {
        std::cerr << "GPU core symbols are hidden from dynamically loaded providers\n";
        return 1;
    }
    if (loaded && argc == 9) {
        DepthEstimator depth;
        DepthEstimator::Config config;
        config.modelPath = argv[7];
        config.processSize = 280;
        config.intraOpThreads = 2;
        if (!depth.load(config, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        SDL_Surface* input = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGB24);
        if (!input) return 1;
        SDL_ClearSurface(input, 0.5f, 0.25f, 0.75f, 1.0f);
        const auto result = depth.predict(input, error);
        if (!result.valid()) {
            std::cerr << error << '\n';
            SDL_DestroySurface(input);
            return 1;
        }
        SuperResolution sr;
        SuperResolution::Config srConfig;
        srConfig.modelPath = argv[8];
        srConfig.intraOpThreads = 2;
        if (!sr.load(srConfig, error)) {
            std::cerr << error << '\n';
            SDL_DestroySurface(input);
            return 1;
        }
        SDL_Surface* output = sr.predict(input, error);
        SDL_DestroySurface(input);
        if (!output) {
            std::cerr << error << '\n';
            return 1;
        }
        SDL_DestroySurface(output);
        std::cout << "Depth: " << depth.providerName() << "; SR: " << sr.providerName() << '\n';
    }
    const auto provider = InferenceRuntime::provider();
    const std::string actual = !loaded ? "unavailable" : provider == Provider::CUDA ? "cuda" :
        provider == Provider::ROCM ? "rocm" : "cpu";
    const std::string status = InferenceRuntime::status();
    std::cout << actual << ": " << status << '\n';
    if (!loaded) std::cout << error << '\n';
    return actual == argv[4] && status.find(argv[5]) != std::string::npos ? 0 : 1;
}
