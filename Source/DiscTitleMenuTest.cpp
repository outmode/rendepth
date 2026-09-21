// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
// Integration test: DiscTitleMenuTest <Blu-ray folder/device> [preview.png]
// Uses the real disc reader/decoder and a dummy window; captures the menu's
// CPU-composited surface instead of uploading it to a GPU.
#include "DiscTitleMenu.h"
#include "BlurayReader.h"
#include "VideoPlayer.h"
#include "Core.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

static SDL_Surface* snapshot = nullptr;
int Core::uploadTexture(Context*, SDL_Surface* surface, SDL_GPUTexture**, const std::string&) {
    SDL_DestroySurface(snapshot);
    snapshot = SDL_DuplicateSurface(surface);
    return snapshot ? 0 : -1;
}
SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string&) { return surface; }
static bool waitForFrame(VideoPlayer& player, double minimum = 0.0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < end) {
        player.update();
        if (auto frame = player.takeFrame(); frame && frame->presentationTime >= minimum && !frame->planes[0].empty()) return true;
        if (!player.takeError().empty()) return false;
        SDL_Delay(10);
    }
    return false;
}
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: DiscTitleMenuTest <Blu-ray source> [preview.png]\n"; return 1; }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) || !TTF_Init()) return 1;
    Context context{};
    context.window = SDL_CreateWindow("Disc title test",1280,800,0);
    auto* font = TTF_OpenFont("Assets/Lato.ttf",20);
    if (!context.window || !font) return 1;
    DiscTitleMenu menu;
    menu.open(std::filesystem::path(argv[1]) / "missing-disc-source");
    if (menu.visible()) { std::cerr << "Disc browser appeared before loading\n"; return 21; }
    std::string scanError;
    const auto failureEnd = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (scanError.empty() && std::chrono::steady_clock::now() < failureEnd) {
        menu.update(&context,font);
        if (menu.visible() || snapshot) { std::cerr << "Failed scan displayed a loading page\n"; return 22; }
        scanError = menu.takeError();
        SDL_Delay(10);
    }
    if (scanError.empty() || !menu.takeError().empty()) { std::cerr << "Missing or repeated disc error\n"; return 23; }
    if (std::string(argv[1]) == "--failure-only") {
        menu.shutdown(&context);
        TTF_CloseFont(font); SDL_DestroyWindow(context.window); TTF_Quit(); SDL_Quit();
        std::cout << "PASS: disc scan stays hidden and reports failure once\n";
        return 0;
    }
    std::vector<BlurayTitle> titles;
    int defaultTitle = -1;
    {
        BlurayReader reader; std::string error;
        if (!reader.open(argv[1], error)) { std::cerr << error << '\n'; return 1; }
        titles = reader.titles();
        defaultTitle = reader.activeTitle();
    }
    std::stable_sort(titles.begin(),titles.end(),[](const auto& a,const auto& b) { return a.duration > b.duration; });
    if (titles.size() < 2) { std::cerr << "Test requires at least two titles\n"; return 1; }
    const auto mainFeature = std::find_if(titles.begin(), titles.end(), [](const auto& title) {
        return title.mainFeatureCandidate;
    });
    int expectedAutomatic = mainFeature != titles.end() ? mainFeature->index : defaultTitle;
#ifdef RENDEPTH_ENABLE_MVC
    const auto preferred = std::find_if(titles.begin(), titles.end(), [&](const auto& title) {
        return title.index == expectedAutomatic;
    });
    if (preferred != titles.end() && !preferred->mvc) {
        const auto stereo = std::find_if(titles.begin(), titles.end(), [&](const auto& title) {
            return title.mvc && std::abs(title.duration - preferred->duration) < 1.0 &&
                title.chapterCount == preferred->chapterCount;
        });
        if (stereo != titles.end()) expectedAutomatic = stereo->index;
    }
#endif
    menu.open(argv[1], true);
    std::optional<int> automatic;
    const auto automaticEnd = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!automatic && std::chrono::steady_clock::now() < automaticEnd) {
        menu.update(&context, font);
        if (menu.visible() || snapshot) { std::cerr << "Autoplay displayed the title browser\n"; return 24; }
        if (auto error = menu.takeError(); !error.empty()) { std::cerr << error << '\n'; return 25; }
        automatic = menu.takeSelection();
        SDL_Delay(10);
    }
    if (!automatic || *automatic != expectedAutomatic || menu.takeSelection()) {
        std::cerr << "Wrong or repeated automatic main feature\n"; return 26;
    }
    std::cout << "Automatic main feature: title index " << *automatic << '\n';
    {
        VideoPlayer automaticPlayer;
        std::string error;
        if (!automaticPlayer.open(argv[1], error, *automatic) || !waitForFrame(automaticPlayer)) {
            std::cerr << "Automatic main feature did not play: " << error << '\n'; return 27;
        }
    }
    // Opening Track Selection explicitly must still wait for the user.
    menu.open(argv[1]);
    if (menu.visible()) { std::cerr << "Disc browser appeared before loading\n"; return 21; }
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(argc > 2 ? 15 : 3);
    while (std::chrono::steady_clock::now() < end) {
        menu.update(&context,font);
        if (menu.takeSelection()) { std::cerr << "Unexpected autoplay\n"; return 2; }
        SDL_Delay(20);
    }
    if (!snapshot || !menu.visible()) return 3;
    Uint8 red, green, blue, alpha;
    SDL_ReadSurfacePixel(snapshot, 0, 0, &red, &green, &blue, &alpha);
    if (alpha != 0) { std::cerr << "Disc browser obscures the app background\n"; return 18; }
    if (argc > 2) IMG_SavePNG(snapshot,argv[2]);
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.key = SDLK_RIGHT;
    if (menu.handleEvent(event,context.window)) return 19; // App navigation owns arrow keys.
    event.key.key = SDLK_F;
    if (menu.handleEvent(event,context.window)) return 20; // Fullscreen shortcut passes through.
    event.key.key = SDLK_TAB; menu.handleEvent(event,context.window);
    if (menu.takeSelection()) return 4; // Moving focus must not play.
    event.key.key = SDLK_RETURN; menu.handleEvent(event,context.window);
    std::optional<int> selected;
    const auto selectionEnd = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!selected && std::chrono::steady_clock::now() < selectionEnd) {
        selected = menu.takeSelection(); SDL_Delay(10);
    }
    if (!selected || *selected != titles[1].index || menu.visible()) { std::cerr << "Wrong selected title\n"; return 5; }
    VideoPlayer player; std::string error;
    if (!player.open(argv[1],error,*selected)) { std::cerr << error << '\n'; return 6; }
    if (std::abs(player.duration()-titles[1].duration) > 2 || !waitForFrame(player)) return 7;
    if (titles[1].audioTrackCount > 0) {
        const auto audioEnd = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (player.bufferedAudioDuration() <= 0 && std::chrono::steady_clock::now() < audioEnd) {
            player.update(); player.takeFrame(); SDL_Delay(10);
        }
        if (player.bufferedAudioDuration() <= 0) { std::cerr << "No decoded disc audio\n"; return 12; }
    }
    if (!player.discSource() || player.chapterCount() != static_cast<int>(titles[1].chapters.size())) return 13;
    for (int i = 0; i < player.chapterCount(); ++i)
        if (player.chapterTime(i) != titles[1].chapters[i].startTime) return 17;
    // Pointer movement must not enqueue disc seeks, invalidate frames, or clear
    // buffers. Only the final release commits the destination, even after a burst.
    for (double requested : {10.0, 90.0, 30.0, 120.0}) {
        const bool wasPlaying = player.playing();
        player.setPlaying(false);
        const auto generation = player.generation();
        for (int i = 0; i < 500; ++i)
            player.seek((i % 100) * player.duration() / 100.0, true);
        if (player.generation() != generation || player.playing()) {
            std::cerr << "Disc drag issued a preview seek\n"; return 14;
        }
        const double target = std::min(requested, titles[1].duration / 2);
        player.seek(target);
        player.setPlaying(wasPlaying);
        if (player.generation() != generation + 1 || !waitForFrame(player,target-0.1)) return 15;
        // Exercise both a playing drag and a paused drag on the next release.
        player.setPlaying(!wasPlaying);
    }
    player.close();
    if (player.discSource() || player.chapterCount() != 0) return 16;
    // Exercise pointer selection after the keyboard path.
    menu.open(argv[1]);
    const auto pointerEnd = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < pointerEnd) { menu.update(&context,font); SDL_Delay(20); }
    event = {}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 150; event.button.y = 200;
    menu.handleEvent(event,context.window);
    selected.reset();
    const auto clickEnd = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!selected && std::chrono::steady_clock::now() < clickEnd) { selected = menu.takeSelection(); SDL_Delay(10); }
    if (!selected || *selected != titles.front().index) return 10;
    // Narrow layout must keep paging and hit testing aligned.
    SDL_SetWindowSize(context.window, 600, 450);
    menu.open(argv[1]);
    const auto narrowEnd = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < narrowEnd) { menu.update(&context,font); SDL_Delay(20); }
    event = {}; event.type = SDL_EVENT_KEY_DOWN; event.key.key = SDLK_PAGEDOWN;
    menu.handleEvent(event,context.window); menu.update(&context,font);
    if (argc > 2) IMG_SavePNG(snapshot,(std::string(argv[2])+".narrow.png").c_str());
    event.key.key = SDLK_RETURN; menu.handleEvent(event,context.window);
    selected.reset();
    const auto narrowSelectionEnd = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!selected && std::chrono::steady_clock::now() < narrowSelectionEnd) { selected = menu.takeSelection(); SDL_Delay(10); }
    if (!selected || *selected != titles[1].index) return 11;
    menu.open(argv[1]);
    event.key.key = SDLK_ESCAPE;
    if (menu.handleEvent(event,context.window) || menu.visible() || menu.takeSelection()) return 9;
    // Reopening while a canceled scan is winding down must not publish old results.
    menu.open(argv[1]); menu.close();
    menu.shutdown(&context);
    SDL_DestroySurface(snapshot); TTF_CloseFont(font);
    SDL_DestroyWindow(context.window); TTF_Quit(); SDL_Quit();
    std::cout << "PASS: main-feature autoplay, manual title browser, transparent background, app shortcuts, keyboard/mouse selection, responsive paging, decode/audio, release-only disc scrubbing, reopen\n";
}
