// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
struct Context;
struct TTF_Font;

// A silent, asynchronous disc browser. SDL/GPU work stays on the render thread.
class DiscTitleMenu {
public:
    DiscTitleMenu();
    ~DiscTitleMenu();
    void open(const std::filesystem::path& path, bool autoPlayMainFeature = false);
    void close();
    void shutdown(Context* context);
    bool active() const;
    bool visible() const;
    void pageBy(int delta);
    bool hasPages() const;
    std::shared_ptr<SDL_Surface> backgroundPreview() const;
    std::string hoveredMetadata() const;
    bool handleEvent(const SDL_Event& event, SDL_Window* window);
    void requestSelection(int title);
    void update(Context* context, TTF_Font* font);
    SDL_GPUTexture* texture() const;
    SDL_GPUTexture* depthTexture() const;
    std::optional<int> takeSelection();
    std::string takeError();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
