// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
// Integration test: DiscSubtitleTimingTest <disc> [seek seconds] [scan seconds]
#include "VideoPlayer.h"
#include "Core.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <iostream>

SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string&) { return surface; }

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: DiscSubtitleTimingTest <disc> [seek seconds] [scan seconds]\n";
        return 1;
    }
    const double seekTime = argc > 2 ? std::stod(argv[2]) : 0.0;
    const double scanDuration = argc > 3 ? std::stod(argv[3]) : 120.0;
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_AUDIO)) return 2;
    VideoPlayer player;
    std::string error;
    if (!player.open(argv[1], error)) {
        std::cerr << error << '\n';
        return 3;
    }
    player.cycleSubtitleTrack();
    std::cout << player.subtitleLanguage() << '\n';
    if (seekTime > 0.0) player.seek(seekTime);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    std::shared_ptr<const VideoSubtitle> previous;
    int captions = 0;
    int clears = 0;
    int captionsAfterClear = 0;
    double lastTime = seekTime;
    while (lastTime < seekTime + scanDuration && std::chrono::steady_clock::now() < deadline) {
        player.update();
        if (auto message = player.takeError(); !message.empty()) {
            std::cerr << message << '\n';
            return 4;
        }
        auto frame = player.takeFrame();
        if (!frame) { SDL_Delay(2); continue; }
        if (frame->generation != player.generation()) continue;
        player.setAudioBuffering(false);
        lastTime = frame->presentationTime;
        player.setPresentedPosition(lastTime);
        auto current = player.subtitle();
        if (current != previous) {
            if (current) {
                ++captions;
                if (clears) ++captionsAfterClear;
                std::cout << "Caption at " << lastTime << '\n';
            } else if (previous) {
                ++clears;
                std::cout << "Clear at " << lastTime << '\n';
            }
            previous = std::move(current);
        }
    }
    player.close();
    SDL_Quit();
    std::cout << "Captions=" << captions << " clears=" << clears
              << " resumed=" << captionsAfterClear << " scanned through=" << lastTime << '\n';
    return captions >= 2 && clears >= 1 && captionsAfterClear >= 1 ? 0 : 5;
}
