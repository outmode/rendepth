// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <optional>
struct Context;
struct TTF_Font;

// A silent, asynchronous disc browser. SDL/GPU work stays on the render thread.
class DiscTitleMenu {
public:
    DiscTitleMenu();
    ~DiscTitleMenu();
    void open(const std::filesystem::path& path);
    void close();
    void shutdown(Context* context);
    bool visible() const;
    bool handleEvent(const SDL_Event& event, SDL_Window* window);
    void update(Context* context, TTF_Font* font);
    SDL_GPUTexture* texture() const;
    std::optional<int> takeSelection();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
