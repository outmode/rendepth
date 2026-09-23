// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "GettingStarted.h"
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Keep regression checks active in Release builds as well as Debug builds.
static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s: %s\n", message, SDL_GetError()); std::exit(1); }
}

// Drive keyboard interaction through the same event handler used by the app.
static GettingStarted::Action key(SDL_Window* window, SDL_Keycode code) {
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.key = code;
    return GettingStarted::handleEvent(event, window);
}

// Exercise responsive rendering, scroll bounds, footer interaction, and reopening without a GPU.
int main(int argc, char** argv) {
    require(argc >= 2, "Pass the Assets directory and optionally a preview directory");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    require(SDL_Init(SDL_INIT_VIDEO) && TTF_Init(), "Initialize SDL and fonts");
    auto* window = SDL_CreateWindow("Guide test", 512, 768, SDL_WINDOW_HIDDEN);
    require(window != nullptr, "Create test window");
    GettingStarted::show();
    auto* surface = GettingStarted::render(512, 768, 1, argv[1]);
    require(surface != nullptr, "Render narrow guide");
    const auto revision = GettingStarted::revision();
    require(GettingStarted::render(512, 768, 1, argv[1]) == surface &&
        GettingStarted::revision() == revision, "Reuse unchanged rendering");
    std::vector<unsigned char> first(static_cast<unsigned char*>(surface->pixels),
        static_cast<unsigned char*>(surface->pixels) + surface->pitch * surface->h);
    if (argc > 2) require(IMG_SavePNG(surface, (std::filesystem::path(argv[2]) / "guide-narrow.png").string().c_str()), "Save preview");
    key(window, SDLK_END);
    surface = GettingStarted::render(512, 768, 1, argv[1]);
    require(surface && std::memcmp(first.data(), surface->pixels, first.size()) != 0, "Scroll narrow content");
    const int footer = 682 * surface->pitch;
    require(std::memcmp(first.data() + footer, static_cast<unsigned char*>(surface->pixels) + footer,
        first.size() - footer) == 0, "Keep footer fixed when scrolling");
    key(window, SDLK_HOME);
    surface = GettingStarted::render(512, 768, 1, argv[1]);
    require(surface && std::memcmp(first.data(), surface->pixels, first.size()) == 0, "Return to top");
    key(window, SDLK_TAB);
    require(key(window, SDLK_SPACE) == GettingStarted::Action::PreferenceChanged && GettingStarted::dontShowAgain,
        "Toggle preference using keyboard");
    require(key(window, SDLK_ESCAPE) == GettingStarted::Action::Dismiss && !GettingStarted::visible, "Dismiss guide");
    GettingStarted::show();
    require(GettingStarted::dontShowAgain, "Reopening preserves preference");
    SDL_Event repeatedF1{}; repeatedF1.type = SDL_EVENT_KEY_DOWN;
    repeatedF1.key.key = SDLK_F1; repeatedF1.key.repeat = true;
    require(GettingStarted::handleEvent(repeatedF1, window) == GettingStarted::Action::None && GettingStarted::visible,
        "Ignore F1 key repeat");
    require(key(window, SDLK_F1) == GettingStarted::Action::Dismiss && !GettingStarted::visible,
        "F1 closes the guide");
    GettingStarted::show();
    SDL_Event click{}; click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.button = SDL_BUTTON_LEFT; click.button.x = 60; click.button.y = 725;
    GettingStarted::handleEvent(click, window);
    click.type = SDL_EVENT_MOUSE_BUTTON_UP;
    require(GettingStarted::handleEvent(click, window) == GettingStarted::Action::PreferenceChanged &&
        !GettingStarted::dontShowAgain, "Toggle preference by clicking label");
    for (const auto size : {SDL_Point{1280, 900}, SDL_Point{2560, 1800}}) {
        surface = GettingStarted::render(size.x, size.y, size.x / 1280.0f, argv[1]);
        require(surface != nullptr, "Render normal and high DPI guide");
        if (argc > 2) require(IMG_SavePNG(surface, (std::filesystem::path(argv[2]) /
            (size.x == 1280 ? "guide-normal.png" : "guide-hidpi.png")).string().c_str()), "Save preview");
    }
    surface = GettingStarted::render(1280, 900, 1, argv[1]);
    require(surface != nullptr, "Render centering reference");
    std::vector<unsigned char> centered(static_cast<unsigned char*>(surface->pixels),
        static_cast<unsigned char*>(surface->pixels) + surface->pitch * 814);
    surface = GettingStarted::render(1280, 1100, 1, argv[1]);
    require(surface && std::memcmp(centered.data(), static_cast<unsigned char*>(surface->pixels) +
        surface->pitch * 100, centered.size()) == 0, "Center guide vertically when height increases");
    const auto darkRevision = GettingStarted::revision();
    surface = GettingStarted::render(1280, 1100, 1, argv[1], true);
    require(surface && GettingStarted::revision() > darkRevision, "Rebuild cached page when theme changes");
    Uint8 r, g, b, a;
    require(SDL_ReadSurfacePixel(surface, 0, 0, &r, &g, &b, &a) && r == 238 && g == r && b == r,
        "Use neutral light gray background");
    if (argc > 2) require(IMG_SavePNG(surface, (std::filesystem::path(argv[2]) / "guide-light.png").string().c_str()), "Save light preview");
    surface = GettingStarted::render(1280, 1100, 1, argv[1]);
    require(surface && SDL_ReadSurfacePixel(surface, 0, 0, &r, &g, &b, &a) && r == 20 && g == r && b == r,
        "Restore neutral dark gray background");
    require(SDL_SetWindowSize(window, 1280, 900), "Resize input test window");
    for (const auto layout : {GettingStarted::Layout::SBSFull, GettingStarted::Layout::SBSHalf,
                              GettingStarted::Layout::RGBD}) {
        GettingStarted::show();
        const int pageWidth = layout == GettingStarted::Layout::SBSFull ? 640 : 1280;
        surface = GettingStarted::render(pageWidth, 900, 1, argv[1]);
        require(surface != nullptr, "Render unpacked reference");
        auto* reference = SDL_CreateSurface(640, 900, SDL_PIXELFORMAT_RGBA32);
        require(reference && SDL_BlitSurfaceScaled(surface, nullptr, reference, nullptr, SDL_SCALEMODE_LINEAR),
            "Build reference eye image");
        surface = GettingStarted::render(1280, 900, 1, argv[1], false, layout);
        require(surface != nullptr, "Render packed guide");
        for (int y = 0; y < 900; ++y) {
            auto* row = static_cast<unsigned char*>(surface->pixels) + surface->pitch * y;
            require(std::memcmp(row, static_cast<unsigned char*>(reference->pixels) + reference->pitch * y, 640 * 4) == 0,
                "Match native full SBS or horizontally squeezed half SBS reference");
            if (layout != GettingStarted::Layout::RGBD)
                require(std::memcmp(row, row + 640 * 4, 640 * 4) == 0, "Match both stereo eyes");
        }
        SDL_DestroySurface(reference);
        const auto packedRevision = GettingStarted::revision();
        require(GettingStarted::render(1280, 900, 1, argv[1], false, layout) == surface &&
            GettingStarted::revision() == packedRevision, "Cache packed guide");
        if (layout == GettingStarted::Layout::RGBD) {
            require(SDL_ReadSurfacePixel(surface, 700, 100, &r, &g, &b, &a) && r == 192 && g == r && b == r,
                "Render flat light gray depth panel");
            require(SDL_ReadSurfacePixel(surface, 1240, 857, &r, &g, &b, &a) && r == 255 && g == r && b == r,
                "Render white dismissal block in depth panel");
            require(SDL_ReadSurfacePixel(surface, 654, 847, &r, &g, &b, &a) && r == 255 && g == r && b == r,
                "Render white depth checkbox");
            int bodyWhite = 0, labelWhite = 0;
            for (int y = 0; y < 900; ++y) for (int x = 640; x < 1280; ++x) {
                SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);
                require(r == g && g == b && r >= 191,
                    "Keep depth content white on gray, including antialiased edges");
                if (r >= 250 && y < 800) ++bodyWhite;
                if (r >= 250 && y > 840 && x > 670 && x < 760) ++labelWhite;
            }
            require(bodyWhite > 500 && labelWhite > 10, "Include white guide content and preference label in depth panel");
            const bool oldPreference = GettingStarted::dontShowAgain;
            click.type = SDL_EVENT_MOUSE_BUTTON_DOWN; click.button.x = 680; click.button.y = 857;
            GettingStarted::handleEvent(click, window);
            click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            require(GettingStarted::handleEvent(click, window) == GettingStarted::Action::PreferenceChanged &&
                GettingStarted::dontShowAgain != oldPreference, "Toggle checkbox from depth panel");
            surface = GettingStarted::render(1280, 900, 1, argv[1], false, layout);
            require(surface != nullptr, "Render updated depth checkbox");
        }
        if (argc > 2) {
            const char* name = layout == GettingStarted::Layout::SBSFull ? "guide-sbs-full.png" :
                layout == GettingStarted::Layout::SBSHalf ? "guide-sbs-half.png" : "guide-rgbd.png";
            require(IMG_SavePNG(surface, (std::filesystem::path(argv[2]) / name).string().c_str()), "Save packed preview");
        }
        // Both eye copies (including the RGBD white block) must dismiss at their displayed position.
        for (int eye = 0; eye < 2; ++eye) {
            GettingStarted::show();
            click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            click.button.x = eye * 640 + (pageWidth - 84) * 640.0f / pageWidth;
            click.button.y = 857;
            GettingStarted::handleEvent(click, window);
            click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            require(GettingStarted::handleEvent(click, window) == GettingStarted::Action::Dismiss,
                "Click packed dismissal button");
        }
    }
    GettingStarted::release();
    require(!GettingStarted::render(512, 768, 1, "/missing-assets"), "Handle missing assets");
    require(GettingStarted::render(512, 768, 1, argv[1]) != nullptr, "Recover after missing assets");
    GettingStarted::release(); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();
    std::puts("GettingStartedTest passed");
}
