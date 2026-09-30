// Integration test: BluraySeekPlaybackTest <Blu-ray folder or device> [seek seconds ...]
#include "BlurayReader.h"
#include "VideoPlayer.h"
#include "Core.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string&) { return surface; }

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: BluraySeekPlaybackTest <Blu-ray source> [seek seconds ...]\n";
        return 1;
    }
    BlurayReader reader;
    std::string error;
    if (!reader.open(argv[1], error)) { std::cerr << error << '\n'; return 2; }
    const BlurayTitle* title = nullptr;
    for (const auto& candidate : reader.titles())
        if (!candidate.mvc && (!title || candidate.duration > title->duration)) title = &candidate;
    if (!title) { std::cerr << "No ordinary Blu-ray title\n"; return 3; }
    const int titleIndex = title->index;
    const double duration = title->duration;
    std::cout << "Testing playlist " << title->playlist << ", title " << titleIndex << std::endl;
    reader.close();

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_AUDIO)) return 4;
    VideoPlayer player;
    if (!player.open(argv[1], error, titleIndex)) { std::cerr << error << '\n'; return 5; }
    if (player.nativeStereo()) return 6;
    auto frames = [&](double target) {
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::seconds(35);
        double previous = -1.0;
        int received = 0;
        while (received < 24 && std::chrono::steady_clock::now() < deadline) {
            if (auto message = player.takeError(); !message.empty()) {
                std::cerr << message << '\n'; return false;
            }
            auto frame = player.takeFrame();
            if (!frame) { SDL_Delay(2); continue; }
            if (frame->generation != player.generation() || frame->planes[0].empty() ||
                frame->presentationTime < previous ||
                frame->presentationTime + 0.1 < target ||
                frame->presentationTime > target + 5.0) {
                std::cerr << "Invalid frame at " << frame->presentationTime
                          << " after seek to " << target << '\n';
                return false;
            }
            if (!received) {
                std::cout << "First frame at " << frame->presentationTime << " after "
                          << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
                          << " seconds" << std::endl;
            }
            previous = frame->presentationTime;
            player.setAudioBuffering(false);
            ++received;
        }
        return received == 24;
    };
    std::vector<double> seekTimes;
    if (argc == 2) seekTimes.push_back(std::min(60.0, duration / 2.0));
    for (int i = 2; i < argc; ++i) seekTimes.push_back(std::stod(argv[i]));
    for (double target : seekTimes) {
        if (target < 0.0 || target >= duration - 1.0) {
            std::cerr << "Seek target outside playable title: " << target << '\n';
            return 7;
        }
        player.seek(target);
        if (!frames(target)) return 7;
    }
    if (duration > 252.0) {
        // The decoder may still be finishing an old frame when a second seek
        // supersedes the first. Only the final generation may be returned.
        player.seek(250.0);
        SDL_Delay(5);
        player.seek(90.0);
        if (!frames(90.0)) return 8;
    }
    player.close();
    SDL_Quit();
    std::cout << "PASS: Blu-ray seeks and sequential decode\n";
}
