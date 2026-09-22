// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "InferenceRuntime.h"

#include <dlfcn.h>
#include <cstdlib>
#include <mutex>
#include <onnxruntime_cxx_api.h>

namespace {
using Provider = InferenceRuntime::Provider;
std::mutex runtimeMutex;
std::filesystem::path applicationRoot;
std::filesystem::path packsRoot;
Provider preference = Provider::CPU;
Provider selected = Provider::CPU;
bool attempted = false;
bool loaded = false;
std::string runtimeStatus = "Not initialized";
std::string loadError;

// Fill unset application and user runtime-pack directories from platform defaults.
void defaultPaths() {
    if (applicationRoot.empty()) {
        const char* base = SDL_GetBasePath();
        if (base) applicationRoot = std::filesystem::path(base).parent_path().parent_path();
    }
    if (packsRoot.empty()) {
        const char* home = std::getenv("HOME");
        if (home && *home) packsRoot = std::filesystem::path(home) / ".Rendepth/Runtimes";
    }
}

// Resolve the shared library path for the requested CPU or GPU runtime pack.
std::filesystem::path libraryPath(Provider backend) {
    if (backend == Provider::CPU)
        return applicationRoot / "Runtimes/cpu/lib/libonnxruntime.so.1";
    if (packsRoot.empty()) return {};
    return packsRoot / (backend == Provider::CUDA ? "cuda" : "rocm") /
        "lib/libonnxruntime.so.1";
}

// Check that the runtime core and required provider libraries exist together.
bool completePack(Provider backend) {
    std::error_code error;
    const auto core = libraryPath(backend);
    if (core.empty() || !std::filesystem::is_regular_file(core, error)) return false;
    if (backend == Provider::CPU) return true;
    const auto library = backend == Provider::CUDA ?
        "libonnxruntime_providers_cuda.so" : "libonnxruntime_providers_rocm.so";
    return std::filesystem::is_regular_file(core.parent_path() / library, error) &&
        std::filesystem::is_regular_file(core.parent_path() / "libonnxruntime_providers_shared.so", error);
}

// Load the selected runtime library and bind its API after checking provider availability.
bool load(Provider backend, std::string& error) {
    const auto path = libraryPath(backend);
    if (path.empty()) {
        error = "GPU runtime directory is unavailable.";
        return false;
    }
    if (backend != Provider::CPU && !completePack(backend)) {
        error = "GPU runtime pack is missing or incomplete: " + path.parent_path().string();
        return false;
    }
    // An absolute path keeps the CPU package independent of any ORT installed
    // on the host. Pack libraries must carry their own $ORIGIN dependency paths.
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        error = "Could not load " + path.string() + ": " + dlerror();
        return false;
    }
    using GetApiBase = const OrtApiBase* (ORT_API_CALL*)();
    auto getApiBase = reinterpret_cast<GetApiBase>(dlsym(handle, "OrtGetApiBase"));
    const OrtApiBase* base = getApiBase ? getApiBase() : nullptr;
    const OrtApi* api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) {
        error = "Incompatible ONNX Runtime in " + path.string() +
            " (requires API " + std::to_string(ORT_API_VERSION) + ").";
        dlclose(handle);
        return false;
    }
    if (backend != Provider::CPU) {
        // Some distro providers resolve support-library symbols through the
        // core's dependency scope, as they did when ORT was directly linked.
        // Promote only after validating the API, before ORT loads a provider.
        void* global = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
        if (!global) {
            error = "Could not expose GPU runtime dependencies: " + std::string(dlerror());
            dlclose(handle);
            return false;
        }
        dlclose(global); // Keep the original handle and its promoted scope.
    }
    // Never dlclose a successfully initialized core: session objects and ORT's
    // own worker threads may outlive other static application objects.
    Ort::InitApi(api);
    selected = backend;
    loaded = true;
    runtimeStatus = std::string(backend == Provider::CPU ? "CPU" :
        backend == Provider::CUDA ? "CUDA" : "ROCm") +
        " runtime " + base->GetVersionString();
    return true;
}
}

namespace InferenceRuntime {
// Set runtime roots and the preferred provider before the first initialization attempt.
void configure(const std::filesystem::path& appRoot,
    const std::filesystem::path& packRoot, Provider preferred) {
    std::lock_guard lock(runtimeMutex);
    if (attempted) return;
    applicationRoot = appRoot;
    packsRoot = packRoot;
    preference = preferred;
}

// Initialize the runtime once, falling back to CPU if the requested GPU runtime cannot load.
bool initialize(Provider requested, std::string& error) {
    std::lock_guard lock(runtimeMutex);
    if (attempted) {
        if (!loaded) error = loadError;
        return loaded;
    }
    attempted = true;
    defaultPaths();
    const auto backend = requested == Provider::Auto ? preference : requested;
    if (backend != Provider::CPU && backend != Provider::CUDA && backend != Provider::ROCM) {
        loadError = "Unsupported inference backend.";
    } else if (load(backend, loadError)) {
        SDL_Log("%s", runtimeStatus.c_str());
        return true;
    }
    if (backend != Provider::CPU) {
        SDL_Log("GPU runtime unavailable; using CPU: %s", loadError.c_str());
        if (load(Provider::CPU, loadError)) {
            runtimeStatus = "CPU fallback: GPU runtime unavailable\n" + loadError;
            return true;
        }
    }
    runtimeStatus = "Inference unavailable: CPU runtime could not load";
    error = loadError;
    return false;
}

// Return the selected inference backend under the runtime lock.
Provider provider() {
    std::lock_guard lock(runtimeMutex);
    return selected;
}

// Check whether a complete runtime pack is installed for the requested backend.
bool installed(Provider backend) {
    std::lock_guard lock(runtimeMutex);
    defaultPaths();
    return completePack(backend);
}

// Return the user directory where optional runtime packs are discovered.
std::filesystem::path packDirectory() {
    std::lock_guard lock(runtimeMutex);
    defaultPaths();
    return packsRoot;
}

// Return the current runtime status for display in the application.
std::string status() {
    std::lock_guard lock(runtimeMutex);
    return runtimeStatus;
}

// Record a session-level CPU fallback and its failure reason.
void useCpuFallback(const std::string& reason) {
    std::lock_guard lock(runtimeMutex);
    selected = Provider::CPU;
    runtimeStatus = "CPU fallback: GPU inference failed\n" + reason;
    SDL_Log("GPU session failed; retrying with CPU: %s", reason.c_str());
}
}
