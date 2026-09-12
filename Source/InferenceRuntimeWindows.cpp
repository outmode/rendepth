// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "InferenceRuntime.h"
#include <onnxruntime_cxx_api.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <mutex>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace {
using Provider = InferenceRuntime::Provider;
using Microsoft::WRL::ComPtr;
using AppendDML = OrtStatus* (ORT_API_CALL*)(OrtSessionOptions*, int);
std::mutex runtimeMutex;
std::filesystem::path applicationRoot, packsRoot;
Provider preference = Provider::CPU, selected = Provider::CPU;
bool attempted = false, loaded = false;
std::string runtimeStatus = "Not initialized", loadError;
HMODULE runtimeModule = nullptr;
AppendDML appendDML = nullptr;
int dmlDevice = -1;

const char* backendName(Provider backend) {
    switch (backend) {
    case Provider::CPU: return "CPU";
    case Provider::CUDA: return "CUDA";
    case Provider::DirectML: return "DirectML";
    default: return "Unsupported";
    }
}

void defaultPaths() {
    if (applicationRoot.empty()) {
        if (const char* base = SDL_GetBasePath())
            applicationRoot = std::filesystem::path(base).parent_path().parent_path();
    }
    if (packsRoot.empty()) {
        if (const wchar_t* home = _wgetenv(L"USERPROFILE"))
            packsRoot = std::filesystem::path(home) / L".Rendepth/Runtimes";
    }
}

std::filesystem::path libraryPath(Provider backend) {
    if (backend == Provider::CPU) return applicationRoot / "Runtimes/cpu/bin/onnxruntime.dll";
    if (packsRoot.empty()) return {};
    if (backend == Provider::CUDA) return packsRoot / "cuda/bin/onnxruntime.dll";
    if (backend == Provider::DirectML) return packsRoot / "directml/bin/onnxruntime.dll";
    return {};
}

bool regularFile(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

bool completePack(Provider backend) {
    const auto core = libraryPath(backend);
    if (core.empty() || !regularFile(core)) return false;
    if (backend == Provider::CPU) return true;
    if (backend == Provider::DirectML) return regularFile(core.parent_path() / "DirectML.dll");
    return regularFile(core.parent_path() / "onnxruntime_providers_cuda.dll") &&
        regularFile(core.parent_path() / "onnxruntime_providers_shared.dll");
}

std::string windowsError() {
    const DWORD code = GetLastError();
    char* message = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
        reinterpret_cast<char*>(&message), 0, nullptr);
    std::string result = "Windows error " + std::to_string(code);
    if (message) { result += ": "; result += message; LocalFree(message); }
    return result;
}

bool load(Provider backend, std::string& error) {
    const auto path = libraryPath(backend);
    if (!completePack(backend)) {
        error = std::string(backendName(backend)) + " runtime pack is missing or incomplete: " + path.string();
        return false;
    }
    // ORT is never statically imported. Refuse a foreign/preloaded core rather
    // than bind provider factories or DLL dependencies to a different version.
    if (GetModuleHandleW(L"onnxruntime.dll")) {
        error = "An ONNX Runtime DLL was already loaded outside the pack loader. Restart Rendepth.";
        return false;
    }
    const auto directory = std::filesystem::absolute(path).parent_path();
    const auto cookie = AddDllDirectory(directory.c_str());
    if (!cookie) { error = windowsError(); return false; }
    HMODULE module = LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    auto fail = [&](const std::string& reason) {
        error = reason;
        if (module) FreeLibrary(module);
        RemoveDllDirectory(cookie);
        return false;
    };
    if (!module) return fail("Could not load " + path.string() + ": " + windowsError());
    using GetApiBase = const OrtApiBase* (ORT_API_CALL*)();
    const auto getApiBase = reinterpret_cast<GetApiBase>(GetProcAddress(module, "OrtGetApiBase"));
    const OrtApiBase* base = getApiBase ? getApiBase() : nullptr;
    const OrtApi* api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) return fail("Incompatible ONNX Runtime pack (requires API " + std::to_string(ORT_API_VERSION) + ").");
    AppendDML factory = nullptr;
    if (backend == Provider::DirectML) {
        // This compatibility export is part of the ORT DirectML C API. Resolve
        // it only from this core, never from a statically linked second ORT.
        factory = reinterpret_cast<AppendDML>(GetProcAddress(module, "OrtSessionOptionsAppendExecutionProvider_DML"));
        if (!factory) return fail("The selected runtime does not provide DirectML.");
    }
    char** providers = nullptr;
    int count = 0;
    if (OrtStatus* failure = api->GetAvailableProviders(&providers, &count)) {
        const std::string reason = api->GetErrorMessage(failure);
        api->ReleaseStatus(failure);
        return fail(reason);
    }
    const std::string required = backend == Provider::CUDA ? "CUDAExecutionProvider" :
        backend == Provider::DirectML ? "DmlExecutionProvider" : "CPUExecutionProvider";
    bool available = false;
    for (int i = 0; i < count; ++i) if (required == providers[i]) available = true;
    if (OrtStatus* failure = api->ReleaseAvailableProviders(providers, count)) api->ReleaseStatus(failure);
    if (!available) return fail("The selected runtime does not provide " + required + ".");
    // Keep both the core and DLL directory alive for delayed provider loads
    // and all ORT objects, including static objects destroyed during exit.
    Ort::InitApi(api);
    runtimeModule = module;
    appendDML = factory;
    selected = backend;
    loaded = true;
    runtimeStatus = std::string(backendName(backend)) + " runtime " + base->GetVersionString();
    return true;
}

int selectDirectMLAdapter() {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        throw Ort::Exception("Could not enumerate DirectML GPUs", ORT_FAIL);
    // Optional explicit DXGI index for multi-GPU installations and validation.
    if (const wchar_t* value = _wgetenv(L"RENDEPTH_DIRECTML_DEVICE")) {
        wchar_t* end = nullptr;
        const long index = std::wcstol(value, &end, 10);
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 desc{};
        if (end == value || *end != L'\0' || index < 0 || index > 255 ||
            FAILED(factory->EnumAdapters1(static_cast<UINT>(index), &adapter)) ||
            FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ||
            FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
            throw Ort::Exception("RENDEPTH_DIRECTML_DEVICE does not name a hardware DirectX 12 adapter", ORT_FAIL);
        char name[512]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
        runtimeStatus += "\nGPU: " + std::string(name);
        return static_cast<int>(index);
    }
    // DML's device index uses EnumAdapters1 order, not preference order.
    for (UINT rank = 0; ; ++rank) {
        ComPtr<IDXGIAdapter1> preferred;
        if (FAILED(factory->EnumAdapterByGpuPreference(rank, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&preferred)))) break;
        DXGI_ADAPTER_DESC1 wanted{};
        if (FAILED(preferred->GetDesc1(&wanted)) || (wanted.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        if (FAILED(D3D12CreateDevice(preferred.Get(), D3D_FEATURE_LEVEL_11_0,
            __uuidof(ID3D12Device), nullptr))) continue;
        for (UINT index = 0; ; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(factory->EnumAdapters1(index, &adapter))) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
                desc.AdapterLuid.LowPart == wanted.AdapterLuid.LowPart &&
                desc.AdapterLuid.HighPart == wanted.AdapterLuid.HighPart) {
                char name[512]{};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
                runtimeStatus += "\nGPU: " + std::string(name);
                return static_cast<int>(index);
            }
        }
    }
    throw Ort::Exception("No hardware DirectX 12 GPU is available for DirectML", ORT_FAIL);
}
}

namespace InferenceRuntime {
void configure(const std::filesystem::path& appRoot, const std::filesystem::path& packRoot, Provider preferred) {
    std::lock_guard lock(runtimeMutex);
    if (attempted) return;
    applicationRoot = appRoot;
    packsRoot = packRoot;
    preference = preferred;
}

bool initialize(Provider requested, std::string& error) {
    std::lock_guard lock(runtimeMutex);
    error.clear();
    if (attempted) { if (!loaded) error = loadError; return loaded; }
    attempted = true;
    defaultPaths();
    // Also applies to later LoadLibrary calls made internally by ORT/CUDA.
    // Keep the application's normal DLL directory, but exclude CWD and PATH.
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
        loadError = windowsError();
    } else {
        const auto backend = requested == Provider::Auto ? preference : requested;
        if (backend != Provider::CPU && backend != Provider::CUDA && backend != Provider::DirectML)
            loadError = "Unsupported Windows inference backend.";
        else if (load(backend, loadError)) return true;
        const auto gpuError = loadError;
        if (backend != Provider::CPU && load(Provider::CPU, loadError)) {
            runtimeStatus = "CPU fallback: GPU runtime unavailable\n" + gpuError;
            return true;
        }
    }
    runtimeStatus = "Inference unavailable: CPU runtime could not load";
    error = loadError;
    return false;
}

Provider provider() { std::lock_guard lock(runtimeMutex); return selected; }
bool installed(Provider backend) { std::lock_guard lock(runtimeMutex); defaultPaths(); return completePack(backend); }
std::filesystem::path packDirectory() { std::lock_guard lock(runtimeMutex); defaultPaths(); return packsRoot; }
std::string status() { std::lock_guard lock(runtimeMutex); return runtimeStatus; }
void useCpuFallback(const std::string& reason) {
    std::lock_guard lock(runtimeMutex);
    selected = Provider::CPU;
    runtimeStatus = "CPU fallback: GPU inference failed\n" + reason;
    SDL_Log("%s", runtimeStatus.c_str());
}

void appendDirectML(OrtSessionOptions* options) {
    std::lock_guard lock(runtimeMutex);
    if (!loaded || !runtimeModule || !appendDML)
        throw Ort::Exception("The loaded runtime does not provide DirectML", ORT_FAIL);
    if (dmlDevice < 0) dmlDevice = selectDirectMLAdapter();
    Ort::ThrowOnError(Ort::GetApi().DisableMemPattern(options));
    Ort::ThrowOnError(Ort::GetApi().SetSessionExecutionMode(options, ORT_SEQUENTIAL));
    Ort::ThrowOnError(appendDML(options, dmlDevice));
}
}
