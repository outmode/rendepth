// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "VideoPlayer.h"
#include "BlurayReader.h"
#include "Core.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <fstream>
// Provide a no-op image orientation dependency for the standalone MVC test.
SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string&) { return surface; }

// Verify native stereo frames, timing, seeking, clip transitions, and looping on a supplied MVC disc.
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: MvcPlaybackTest <disc> [title index] [seek seconds] [frame.yuv] [frame count]\n"; return 1; }
    const int dumpFrameCount = argc > 5 ? std::stoi(argv[5]) : 240;
    if (dumpFrameCount < 24 || dumpFrameCount > 10000) {
        std::cerr << "Frame count must be between 24 and 10000\n"; return 1;
    }
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SDL_Init(SDL_INIT_AUDIO);
    BlurayReader reader;
    std::string error;
    if (!reader.open(argv[1], error)) { std::cerr << error << '\n'; return 2; }
    const BlurayTitle* title = nullptr;
    for (const auto& candidate : reader.titles())
        if (candidate.mvc && (argc < 3 ? (!title || candidate.duration > title->duration) : candidate.index == std::stoi(argv[2]))) title = &candidate;
    if (!title) { std::cerr << "No MVC title\n"; return 3; }
    const int index = title->index;
    const double duration = title->duration;
    std::cout << "MVC playlist " << title->playlist << " duration " << title->duration << '\n';
    const bool hasAudio = title->audioTrackCount > 0;
    std::vector<double> boundaries;
    auto* disc = bd_open(BlurayReader::resolveDiscRoot(argv[1]).string().c_str(), nullptr);
    if (!disc) return 12;
    if (!bd_get_titles(disc, TITLES_RELEVANT, 0)) bd_get_titles(disc, TITLES_ALL, 0);
    auto* info = bd_get_title_info(disc, index, 0);
    if (info) {
        for (unsigned i = 1; i < info->clip_count; ++i) boundaries.push_back(info->clips[i].start_time / 90000.);
        bd_free_title_info(info);
    }
    bd_close(disc);
    reader.close();
    VideoPlayer player;
    if (!player.open(argv[1], error, index)) { std::cerr << error << '\n'; return 4; }
    if (!player.nativeStereo()) return 5;
    std::ofstream dump;
    if (argc > 4) {
        dump.open(argv[4], std::ios::binary);
        if (!dump) { std::cerr << "Could not open frame dump\n"; return 16; }
    }
    auto frames = [&](double minimum, int count, bool requireDifferent = true, bool loop = false) {
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::seconds(std::max(40, count / 20 + 20));
        int received = 0; bool different = false; double previous = -1;
        auto generation = player.generation(); bool looped = false;
        auto firstFrameTime = started;
        auto previousFrameTime = started;
        std::vector<double> frameGaps;
        while (std::chrono::steady_clock::now() < deadline && received < count) {
            player.update();
            if (auto err = player.takeError(); !err.empty()) { std::cerr << err << '\n'; return false; }
            auto frame = player.takeFrame();
            if (!frame) { SDL_Delay(2); continue; }
            // Mirror the app resuming audio once video is available. Leaving
            // audio paused fills its queue and stalls longer playback tests.
            player.setAudioBuffering(false);
            const auto frameTime = std::chrono::steady_clock::now();
            if (!received) firstFrameTime = frameTime;
            else frameGaps.push_back(std::chrono::duration<double, std::milli>(frameTime - previousFrameTime).count());
            previousFrameTime = frameTime;
            if (loop && frame->generation != generation) {
                generation = frame->generation; minimum = 0; previous = -1; looped = true;
            }
            if (frame->generation != player.generation() || frame->presentationTime + 0.05 < minimum ||
                frame->presentationTime < previous || frame->format != VideoFrame::Format::YUV420P ||
                frame->width != 3840 || frame->height != 1080 || !frame->inferenceRGBA.empty()) {
                std::cerr << "Invalid stereo frame: " << frame->width << 'x' << frame->height
                    << " pts=" << frame->presentationTime << '\n'; return false;
            }
            const auto& y = frame->planes[0];
            if (y.size() != size_t(frame->width) * frame->height) return false;
            for (int row = 0; row < frame->height && !different; row += 11)
                for (int x = 0; x < frame->width / 2; x += 13)
                    if (y[row * frame->width + x] != y[row * frame->width + x + frame->width / 2]) { different = true; break; }
            if (dump && received == 23) {
                for (const auto& plane : frame->planes) dump.write(reinterpret_cast<const char*>(plane.data()), plane.size());
            }
            previous = frame->presentationTime;
            if (!received) std::cout << "Frame " << frame->width << 'x' << frame->height << " pts=" << previous << std::endl;
            ++received;
        }
        const double steadySeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - firstFrameTime).count();
        if (received > 1 && steadySeconds > 0)
            std::cout << "Throughput after first frame: " << (received - 1) / steadySeconds << " frames/s\n";
        if (!frameGaps.empty()) {
            std::sort(frameGaps.begin(), frameGaps.end());
            std::cout << "Frame delivery gaps: p95=" << frameGaps[(frameGaps.size() - 1) * 95 / 100]
                << " ms, max=" << frameGaps.back() << " ms, over 100 ms="
                << std::count_if(frameGaps.begin(), frameGaps.end(), [](double gap) { return gap > 100.; }) << '\n';
        }
        std::cout << "Elapsed " << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << "s, last PTS " << previous << "\n";
        std::cout << "Received " << received << " stereo frames; distinct eyes=" << different << std::endl;
        return received == count && (!requireDifferent || different) && (!loop || looped);
    };
    if (argc < 4 && !frames(0, 48)) return 6;
    const auto seeks = argc > 3 ? std::vector<double>{std::stod(argv[3])} : std::vector<double>{60., 10., 180.};
    for (double position : seeks) {
        if (position + 2 >= duration) continue;
        player.seek(position);
        if (!frames(position - 0.05, argc > 4 ? dumpFrameCount : 24)) return 7;
    }
    if (argc < 4) {
        for (double boundary : boundaries) {
            player.seek(std::max(0., boundary - 2));
            if (!frames(std::max(0., boundary - 2.05), 96, false)) return 13;
            player.seek(boundary + 2);
            if (!frames(boundary + 1.95, 24, false)) return 14;
        }
        player.seek(duration - 1);
        if (!frames(duration - 1.05, 72, false, true)) return 15;
    }
    if (argc > 4) {
        player.close(); SDL_Quit();
        std::cout << "PASS: " << dumpFrameCount << " stereo frames; frame 24 saved as planar YUV420P 3840x1080\n";
        return 0;
    }
    player.setPlaying(false);
    player.seek(std::min(20., duration / 2));
    if (!frames(std::min(20., duration / 2) - 0.05, 1)) return 9;
    player.setPlaying(true);
    if (!frames(std::min(20., duration / 2) - 0.05, 24)) return 10;
    if (hasAudio && player.bufferedAudioDuration() <= 0) { std::cerr << "No decoded MVC title audio\n"; return 11; }
    player.close();
    if (player.nativeStereo()) return 8;
    SDL_Quit();
    std::cout << "PASS: native MVC stereo, distinct views, timestamps, generation, seeking, clip transitions and looping\n";
}
