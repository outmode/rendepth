// Copyright (c) 2026 Outmode. Licensed under the MIT license.
#if defined(_WIN32) && defined(_MSC_VER)
#include "LenticularBridge.h"

#include <SDL3/SDL.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>
#include <cstdlib>

#include "sr/management/srcontext.h"
#include "sr/sense/display/switchablehint.h"
#include "sr/weaver/dx12weaver.h"

static_assert(SDL_MAJOR_VERSION == 3 && SDL_MINOR_VERSION == 4 && SDL_MICRO_VERSION == 16,
              "Review the private SDL D3D12 prefixes when updating SDL");

namespace {
// SDL has no public D3D12 GPU interop API. These prefixes match the SDL GPU
// D3D12 backend vendored with this project. Keep all private-layout access here.
constexpr int kSamplers = 16, kStorageTextures = 8, kStorageBuffers = 8;
constexpr int kComputeTextures = 8, kComputeBuffers = 8, kColorTargets = 8;
struct Pass { SDL_GPUCommandBuffer* command; bool inProgress; };
struct ComputePass {
    SDL_GPUCommandBuffer* command; bool inProgress;
    SDL_GPUComputePipeline* pipeline;
    bool sampler[kSamplers], readTexture[kStorageTextures], readBuffer[kStorageBuffers];
    bool writeTexture[kComputeTextures], writeBuffer[kComputeBuffers];
};
struct RenderPass {
    SDL_GPUCommandBuffer* command; bool inProgress;
    SDL_GPUTexture* colorTargets[kColorTargets]; Uint32 targetCount;
    SDL_GPUTexture* depthTarget;
    SDL_GPUGraphicsPipeline* pipeline;
    bool vertexSampler[kSamplers], vertexTexture[kStorageTextures], vertexBuffer[kStorageBuffers];
    bool fragmentSampler[kSamplers], fragmentTexture[kStorageTextures], fragmentBuffer[kStorageBuffers];
};
struct CommandHeader {
    SDL_GPUDevice* device;
    RenderPass render;
    ComputePass compute;
    Pass copy;
    bool acquired, submitted, ignoreValidation;
};
struct D3D12CommandPrefix {
    CommandHeader header;
    void* renderer;
    ID3D12CommandAllocator* allocator;
    ID3D12GraphicsCommandList* list;
};
struct D3D12TexturePrefix {
    void* container;
    Uint32 index;
    void* subresources;
    Uint32 subresourceCount;
    ID3D12Resource* resource;
};
struct D3D12TextureContainerPrefix {
    SDL_GPUTextureCreateInfo info;
    D3D12TexturePrefix* active;
};

struct ContextDeleter {
    void operator()(SR::SRContext* value) const {
        SR::SRContext::deleteSRContext(value);
    }
};
// Resolve the delayed SDK import only when destroying an existing context,
// never during global initialization on machines without the runtime.
std::unique_ptr<SR::SRContext, ContextDeleter> context;
SR::PredictingDX12Weaver* weaver = nullptr;
SR::SwitchableLensHint* lens = nullptr; // Owned by the SRContext.
ID3D12Device* device = nullptr;
ID3D12CommandAllocator* setupAllocator = nullptr;
ID3D12CommandQueue* setupQueue = nullptr;
ID3D12Resource* inputCopy = nullptr;
ID3D12DescriptorHeap* rtvHeap = nullptr;
SDL_Window* activeWindow = nullptr;
bool failureLogged = false;
bool weaveFailed = false;
unsigned successfulFrames = 0;
HMODULE coreModule = nullptr, directxModule = nullptr, displaysModule = nullptr, opencvModule = nullptr;
DLL_DIRECTORY_COOKIE runtimeDirectoryCookie = nullptr;
bool runtimeUnavailable = false;

// LeiaSR records its install root here, including when it is installed outside
// Program Files. Its installer uses the 32-bit registry view on 64-bit Windows.
void addRegisteredRuntime(std::vector<std::filesystem::path>& candidates, DWORD view) {
    constexpr wchar_t key[] = L"SOFTWARE\\Leia, Inc.\\Simulated Reality Platform";
    DWORD bytes = 0;
    const DWORD flags = RRF_RT_REG_SZ | view;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, nullptr, flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) return;
    std::wstring root(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, nullptr, flags, nullptr, root.data(), &bytes) != ERROR_SUCCESS)
        return;
    root.resize(root.find(L'\0'));
    if (!root.empty()) candidates.emplace_back(std::filesystem::path(root) / L"Platform/bin");
}

// InferenceRuntime removes PATH from the process DLL search order. Locate the
// monitor software ourselves, then allow its DLLs and their dependencies from
// that one directory without changing the process-wide search policy.
bool loadRuntime() {
    if (coreModule && directxModule && displaysModule && opencvModule) return true;
    if (runtimeUnavailable) return false;
#ifdef _WIN64
    constexpr wchar_t coreDll[] = L"SimulatedRealityCore.dll";
    constexpr wchar_t directxDll[] = L"SimulatedRealityDirectX.dll";
    constexpr wchar_t displaysDll[] = L"SimulatedRealityDisplays.dll";
#else
    constexpr wchar_t coreDll[] = L"SimulatedRealityCore32.dll";
    constexpr wchar_t directxDll[] = L"SimulatedRealityDirectX32.dll";
    constexpr wchar_t displaysDll[] = L"SimulatedRealityDisplays32.dll";
#endif
    constexpr wchar_t opencvDll[] = L"opencv_world343.dll";
    std::vector<std::filesystem::path> candidates;
    if (const wchar_t* override = _wgetenv(L"RENDEPTH_SR_RUNTIME_DIR"))
        candidates.emplace_back(override);
    addRegisteredRuntime(candidates, RRF_SUBKEY_WOW6432KEY);
    addRegisteredRuntime(candidates, RRF_SUBKEY_WOW6464KEY);
    if (const wchar_t* programFiles = _wgetenv(L"ProgramFiles")) {
        candidates.emplace_back(std::filesystem::path(programFiles) / L"LeiaSR/Platform/bin");
        candidates.emplace_back(std::filesystem::path(programFiles) / L"Acer/SpatialLabs/Platform/bin");
    }
    if (const wchar_t* programFilesX86 = _wgetenv(L"ProgramFiles(x86)"))
        candidates.emplace_back(std::filesystem::path(programFilesX86) / L"Simulated Reality/Platform/bin");
    if (const wchar_t* path = _wgetenv(L"PATH")) {
        std::wstring_view remaining(path);
        while (!remaining.empty()) {
            const auto separator = remaining.find(L';');
            const auto entry = remaining.substr(0, separator);
            if (!entry.empty()) candidates.emplace_back(entry);
            if (separator == std::wstring_view::npos) break;
            remaining.remove_prefix(separator + 1);
        }
    }
    for (const auto& candidate : candidates) {
        std::error_code error;
        const auto directory = std::filesystem::absolute(candidate, error);
        if (error || !std::filesystem::is_regular_file(directory / coreDll, error) ||
            !std::filesystem::is_regular_file(directory / directxDll, error) ||
            !std::filesystem::is_regular_file(directory / displaysDll, error) ||
            !std::filesystem::is_regular_file(directory / opencvDll, error)) continue;
        const auto cookie = AddDllDirectory(directory.c_str());
        if (!cookie) continue;
        constexpr DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS;
        HMODULE opencv = LoadLibraryExW((directory / opencvDll).c_str(), nullptr, flags);
        HMODULE core = opencv ? LoadLibraryExW((directory / coreDll).c_str(), nullptr, flags) : nullptr;
        HMODULE directx = core ? LoadLibraryExW((directory / directxDll).c_str(), nullptr, flags) : nullptr;
        HMODULE displays = directx ? LoadLibraryExW((directory / displaysDll).c_str(), nullptr, flags) : nullptr;
        if (opencv && core && directx && displays) {
            opencvModule = opencv;
            coreModule = core;
            directxModule = directx;
            displaysModule = displays;
            runtimeDirectoryCookie = cookie; // Needed by the SDK's delayed loads.
            SDL_Log("SR lenticular: loaded monitor runtime from %ls", directory.c_str());
            return true;
        }
        const DWORD loadError = GetLastError();
        SDL_Log("SR lenticular: could not load runtime from %ls (Win32 error %lu)",
                directory.c_str(), loadError);
        if (displays) FreeLibrary(displays);
        if (directx) FreeLibrary(directx);
        if (core) FreeLibrary(core);
        if (opencv) FreeLibrary(opencv);
        RemoveDllDirectory(cookie);
    }
    SDL_Log("SR lenticular: no usable monitor runtime found; presenting SBS Half");
    runtimeUnavailable = true;
    return false;
}

// The MSVC delay-load helper raises a structured exception for a missing DLL
// or export. /EHsc's C++ catch below does not handle those exceptions.
int handleMissingDelayedImport(unsigned code) {
    return code == 0xC06D007E || code == 0xC06D007F
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

SR::SRContext* createContext() {
    __try {
        return SR::SRContext::create(SR::SRContext::NetworkMode::NonBlockingClientMode);
    } __except (handleMissingDelayedImport(GetExceptionCode())) {
        SDL_Log("SR lenticular: missing delayed SDK DLL or export (exception 0x%08X)",
                static_cast<unsigned>(GetExceptionCode()));
        return nullptr;
    }
}

void release() {
    if (activeWindow) SDL_Log("SR lenticular: fullscreen output disengaged");
    if (lens) {
        try { lens->disable(); } catch (const std::exception&) { }
    }
    lens = nullptr;
    delete weaver;
    weaver = nullptr;
    context.reset();
    if (inputCopy) inputCopy->Release();
    inputCopy = nullptr;
    if (setupQueue) setupQueue->Release();
    setupQueue = nullptr;
    if (setupAllocator) setupAllocator->Release();
    setupAllocator = nullptr;
    if (rtvHeap) rtvHeap->Release();
    rtvHeap = nullptr;
    if (device) device->Release();
    device = nullptr;
    activeWindow = nullptr;
    successfulFrames = 0;
}

bool initialize(SDL_Window* window, ID3D12Resource* source, ID3D12Resource* backbuffer) {
    if (weaver && activeWindow == window) return true;
    release();
    // The imports are delayed: machines without monitor software still launch.
    if (!loadRuntime()) return false;
    const HRESULT deviceResult = backbuffer->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(deviceResult)) {
        SDL_Log("SR lenticular: backbuffer D3D12 device unavailable (HRESULT 0x%08X)",
                static_cast<unsigned>(deviceResult));
        return false;
    }
    try {
        // Let the runtime discover compatible hardware. Fail promptly if its
        // service is unavailable instead of waiting for a connection.
        context.reset(createContext());
        if (!context) {
            SDL_Log("SR lenticular: SRContext::create returned null");
            release();
            return false;
        }
        const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        const auto sourceDesc = source->GetDesc();
        auto copyDesc = sourceDesc;
        copyDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (copyDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            copyDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        else if (copyDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            copyDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        HRESULT resourceResult = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                IID_PPV_ARGS(&setupAllocator));
        if (SUCCEEDED(resourceResult))
            resourceResult = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&setupQueue));
        if (SUCCEEDED(resourceResult))
            resourceResult = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &copyDesc,
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&inputCopy));
        if (FAILED(resourceResult)) {
            SDL_Log("SR lenticular: could not create weaver input or setup queue (HRESULT 0x%08X)",
                    static_cast<unsigned>(resourceResult));
            release();
            return false;
        }
        lens = SR::SwitchableLensHint::create(*context);
        weaver = new SR::PredictingDX12Weaver(*context, device, setupAllocator,
            setupQueue, inputCopy, backbuffer, hwnd, copyDesc.Format,
            backbuffer->GetDesc().Format);
        weaver->setInputFrameBuffer(inputCopy, copyDesc.Format);
        context->initialize();
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = 1;
        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvHeap)))) {
            SDL_Log("SR lenticular: could not create D3D12 RTV heap");
            release();
            return false;
        }
        weaver->setLatencyInFrames(1);
        if (lens) lens->enable();
        SDL_Log("SR lenticular: weaver ready, lens hint %s", lens ? "enabled" : "unavailable");
        activeWindow = window;
        return true;
    } catch (const std::exception& e) {
        SDL_Log("SR lenticular initialization failed: %s", e.what());
        release();
        return false;
    } catch (...) {
        SDL_Log("SR lenticular initialization failed with an unknown exception");
        release();
        return false;
    }
}
}

bool LenticularBridge::supported(SDL_GPUDevice* gpu, SDL_Window* window) {
    if (!gpu || !window) return false;
    const char* driver = SDL_GetGPUDeviceDriver(gpu);
    return driver && std::string_view(driver) == "direct3d12" &&
           !failureLogged && !weaveFailed && loadRuntime();
}

bool LenticularBridge::weave(SDL_GPUCommandBuffer* command, SDL_Window* window,
                             SDL_GPUTexture* sbs, SDL_GPUTexture* backbuffer,
                             unsigned width, unsigned height) {
    if (weaveFailed || failureLogged) return false;
    auto* texture = sbs ? reinterpret_cast<D3D12TextureContainerPrefix*>(sbs)->active : nullptr;
    auto* target = backbuffer ? reinterpret_cast<D3D12TextureContainerPrefix*>(backbuffer)->active : nullptr;
    if (!command || !texture || !target || !texture->resource || !target->resource ||
        !initialize(window, texture->resource, target->resource)) {
        release();
        if (!failureLogged) {
            SDL_Log("SR lenticular output unavailable; presenting SBS Half instead");
            failureLogged = true;
        }
        return false;
    }
    auto* list = reinterpret_cast<D3D12CommandPrefix*>(command)->list;
    if (!list) {
        release();
        failureLogged = true;
        SDL_Log("SR lenticular command list unavailable; presenting SBS Half instead");
        return false;
    }
    const auto inputDesc = texture->resource->GetDesc();
    const auto outputDesc = target->resource->GetDesc();
    try {
        auto sourceFormat = inputDesc.Format;
        if (sourceFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            sourceFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        else if (sourceFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            sourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (inputDesc.Width != inputCopy->GetDesc().Width ||
            inputDesc.Height != inputCopy->GetDesc().Height ||
            sourceFormat != inputCopy->GetDesc().Format) {
            release();
            SDL_Log("SR lenticular: input size or format changed; reinitializing on next frame");
            return false;
        }
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for (auto& barrier : barriers) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        barriers[0].Transition.pResource = texture->resource;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Transition.pResource = inputCopy;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(2, barriers);
        list->CopyResource(inputCopy, texture->resource);
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        // The weaver samples this texture after the copy. Keep the copy-to-read
        // barrier on the same command list so it cannot see partially copied rows.
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        list->ResourceBarrier(2, barriers);
        device->CreateRenderTargetView(target->resource, nullptr,
                                       rtvHeap->GetCPUDescriptorHandleForHeapStart());
        const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width),
                                      static_cast<float>(height), 0, 1};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(width),
                                 static_cast<LONG>(height)};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        weaver->setOutputFrameBuffer(target->resource);
        weaver->setCommandList(list);
        if (successfulFrames == 0) {
            const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(
                SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
            RECT client{};
            GetClientRect(hwnd, &client);
            SDL_Log("SR lenticular: input %llux%u fmt=%u output %llux%u fmt=%u client=%ldx%ld canWeave=%d",
                static_cast<unsigned long long>(inputDesc.Width), inputDesc.Height,
                static_cast<unsigned>(inputDesc.Format),
                static_cast<unsigned long long>(outputDesc.Width), outputDesc.Height,
                static_cast<unsigned>(outputDesc.Format), client.right - client.left,
                client.bottom - client.top, weaver->canWeave(width, height));
        }
        weaver->weave(width, height);
        if ((++successfulFrames == 10 || successfulFrames == 300) && lens)
            SDL_Log("SR lenticular: %u frames woven; lens active=%d preference=%d",
                    successfulFrames, lens->isEnabled(), lens->isEnabledByPreference());
        return true;
    } catch (const std::exception& e) {
        SDL_Log("SR lenticular weave failed: %s", e.what());
        // The command list may already refer to weaver-owned GPU resources.
        // Keep them alive until fullscreen exits, after SDL's frame fence.
        if (lens) {
            try { lens->disable(); } catch (const std::exception&) { }
        }
        weaveFailed = true;
        return false;
    } catch (...) {
        SDL_Log("SR lenticular weave failed with an unknown exception");
        if (lens) {
            try { lens->disable(); } catch (...) { }
        }
        weaveFailed = true;
        return false;
    }
}

void LenticularBridge::stop() {
    release();
    failureLogged = false;
    weaveFailed = false;
    runtimeUnavailable = false;
}
#endif
