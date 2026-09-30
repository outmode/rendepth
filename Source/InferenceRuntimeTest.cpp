// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "InferenceRuntime.h"
#include "SuperResolution.h"

#include <iostream>
#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

// Each invocation is a fresh process: replacing a live ORT core is forbidden.
int main(int argc, char** argv) {
    if (argc != 7 && argc != 9 && argc != 10) {
        std::cerr << "Usage: InferenceRuntimeTest <app-root> <pack-root> <cpu|cuda|rocm|directml> "
            "<cpu|cuda|rocm|directml|unavailable> <status-substring> <installed|missing> [<depth-model> <sr-model|-> [<depth-size>]]\n";
        return 2;
    }
    using Provider = DepthEstimator::Provider;
    const std::string request = argv[3];
    const auto backend = request == "cuda" ? Provider::CUDA : request == "rocm" ? Provider::ROCM :
        request == "directml" ? Provider::DirectML : Provider::CPU;
    InferenceRuntime::configure(argv[1], argv[2], backend);
    const bool installed = InferenceRuntime::installed(backend);
    if (installed != (std::string(argv[6]) == "installed")) {
        std::cerr << "Incorrect installed-pack status\n";
        return 1;
    }
    std::string error;
    const bool loaded = InferenceRuntime::initialize(Provider::Auto, error);
#ifdef _WIN32
    const auto initialProvider = InferenceRuntime::provider();
    const auto expectedBin = initialProvider == Provider::CPU ?
        std::filesystem::path(argv[1]) / "Runtimes/cpu/bin" :
        std::filesystem::path(argv[2]) / (initialProvider == Provider::CUDA ? "cuda/bin" : "directml/bin");
    auto moduleInPack = [&](const wchar_t* name) {
        HMODULE module = GetModuleHandleW(name);
        if (!module) return true; // Some optional dependencies are loaded lazily.
        wchar_t path[32768]{};
        if (!GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)))) return false;
        std::error_code ec;
        return std::filesystem::equivalent(std::filesystem::path(path).parent_path(), expectedBin, ec) && !ec;
    };
    if (loaded && !moduleInPack(L"onnxruntime.dll")) {
        std::cerr << "ORT loaded outside the selected pack\n";
        return 1;
    }
    if (loaded) {
        InferenceRuntime::configure("ignored-after-initialization", "ignored", Provider::CPU);
        if (!InferenceRuntime::initialize(Provider::CPU, error) || InferenceRuntime::provider() != initialProvider) {
            std::cerr << "A live runtime was replaced\n";
            return 1;
        }
    }
#endif
#ifndef _WIN32
    if (loaded && InferenceRuntime::provider() != Provider::CPU &&
        !dlsym(RTLD_DEFAULT, "OrtGetApiBase")) {
        std::cerr << "GPU core symbols are hidden from dynamically loaded providers\n";
        return 1;
    }
#endif
    if (loaded && argc >= 9) {
        DepthEstimator depth;
        DepthEstimator::Config config;
        config.modelPath = argv[7];
        config.processSize = argc == 10 ? std::stoi(argv[9]) : 280;
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
        std::cout << "Depth: " << depth.providerName();
        if (std::string(argv[8]) != "-") {
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
            if (!output) {
                std::cerr << error << '\n';
                SDL_DestroySurface(input);
                return 1;
            }
            SDL_DestroySurface(output);
            std::cout << "; SR: " << sr.providerName();
        }
        SDL_DestroySurface(input);
        std::cout << '\n';
    }
    const auto provider = InferenceRuntime::provider();
#ifdef _WIN32
    for (const wchar_t* name : {L"onnxruntime.dll", L"onnxruntime_providers_cuda.dll",
        L"onnxruntime_providers_shared.dll", L"DirectML.dll", L"cublas64_12.dll",
        L"cublas64_13.dll", L"cudnn64_9.dll"}) {
        if (loaded && !moduleInPack(name)) {
            std::cerr << "A runtime dependency loaded outside the selected pack\n";
            return 1;
        }
    }
#endif
    const std::string actual = !loaded ? "unavailable" : provider == Provider::CUDA ? "cuda" :
        provider == Provider::ROCM ? "rocm" : provider == Provider::DirectML ? "directml" : "cpu";
    const std::string status = InferenceRuntime::status();
    std::cout << actual << ": " << status << '\n';
    if (!loaded) std::cout << error << '\n';
    return actual == argv[4] && status.find(argv[5]) != std::string::npos ? 0 : 1;
}
