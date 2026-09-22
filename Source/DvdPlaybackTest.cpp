// Integration test: DvdPlaybackTest <DVD folder, image or device>
#include "DvdReader.h"
#include "VideoPlayer.h"
#include "Core.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>

// Provide a no-op surface orientation dependency for the standalone playback test.
SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string&) { return surface; }

// Compare unaligned and random DVD reads against sequential data, including seek bounds and EOF.
static bool checkReads(DvdReader& reader) {
    constexpr int blockSize = DVD_VIDEO_LB_LEN;
    std::vector<uint8_t> reference(128 * blockSize);
    if (reader.read(reference.data(), static_cast<int>(reference.size())) != reference.size()) return false;
    // Compare arbitrary FFmpeg byte requests with a sequential block read.
    // Includes partial sectors, sector crossings, backward and forced seeks.
    for (int offset : {65 * blockSize + 17, 0, blockSize - 1, 7, 32 * blockSize}) {
        if (reader.seek(offset, SEEK_SET | 0x20000) != offset) return false;
        std::vector<uint8_t> actual(3 * blockSize + 19);
        int copied = 0;
        for (int chunk : {1, 17, 2047, 4098}) {
            if (reader.read(actual.data() + copied, chunk) != chunk) return false;
            copied += chunk;
        }
        if (!std::equal(actual.begin(), actual.end(), reference.begin() + offset) ||
            reader.bytePosition() != offset + copied ||
            reader.seek(-copied, SEEK_CUR) != offset) return false;
    }
    const auto position = reader.bytePosition();
    const auto size = reader.seek(0, 0x10000 | 0x20000);
    if (size <= 0 || reader.bytePosition() != position ||
        reader.seek(std::numeric_limits<int64_t>::max(), SEEK_CUR) != -1 ||
        reader.bytePosition() != position) return false;
    // Exercise offsets past the old byte-seek API's signed 32-bit limit
    // when the title is large.
    for (const int64_t offset : {int64_t(1) << 31}) {
        if (offset + blockSize > size) continue;
        std::cout << "Checking byte offset " << offset << " of " << size << std::endl;
        uint8_t first[blockSize], second[blockSize];
        if (reader.seek(offset, SEEK_SET) != offset || reader.read(first, blockSize) != blockSize ||
            reader.seek(-blockSize, SEEK_CUR) != offset || reader.read(second, blockSize) != blockSize ||
            !std::equal(first, first + blockSize, second)) return false;
    }
    uint8_t tail[32];
    // Verify logical EOF without requiring readable sectors at the library's
    // reported end, which is not always readable on physical media.
    return reader.seek(-7, SEEK_END) == size - 7 && reader.seek(7, SEEK_CUR) == size &&
        reader.read(tail, sizeof(tail)) == 0;
}

// Check byte-level DVD access and player startup and seeking against a supplied disc source.
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: DvdPlaybackTest <DVD source> [seek seconds ...]\n"; return 1; }
    DvdReader reader;
    std::string error;
    if (!reader.open(argv[1], error)) { std::cerr << error << '\n'; return 2; }
    const int title = reader.activeTitle();
    if (!checkReads(reader)) { std::cerr << "DVD byte read/seek mismatch\n"; return 3; }
    reader.close();
    std::cout << "PASS: DVD byte reads, seeks and EOF\n";

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_AUDIO)) return 4;
    VideoPlayer player;
    if (!player.open(argv[1], error, title)) { std::cerr << error << '\n'; return 5; }
    bool decodedAudio = false;
    auto frames = [&](int count, double seekTarget = -1.0) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        int received = 0;
        while (received < count && std::chrono::steady_clock::now() < deadline) {
            player.update();
            decodedAudio |= player.bufferedAudioDuration() > 0.0;
            if (auto message = player.takeError(); !message.empty()) {
                std::cerr << message << '\n'; return false;
            }
            if (auto frame = player.takeFrame()) {
                if (frame->generation != player.generation()) continue;
                if (received == 0 && seekTarget >= 0.0) {
                    // Ignore preroll, but reject the old size-based seek's
                    // jumps many minutes beyond the requested position.
                    if (frame->presentationTime + 0.1 < seekTarget) continue;
                    if (frame->presentationTime > seekTarget + 10.0) {
                        std::cerr << "DVD seek overshot: requested " << seekTarget
                            << ", received " << frame->presentationTime << '\n';
                        return false;
                    }
                }
                if (frame->presentationTime < 0.0 || frame->width <= 0 ||
                    frame->height <= 0 || frame->planes[0].empty()) {
                    std::cerr << "Invalid DVD frame at " << frame->presentationTime << '\n';
                    return false;
                }
                player.setAudioBuffering(false);
                if (received == 0) std::cout << "First frame: " << frame->presentationTime << std::endl;
                ++received;
            } else SDL_Delay(2);
        }
        std::cout << "Decoded " << received << " frames\n";
        return received == count;
    };
    if (!frames(48)) return 6;
    if (player.audioReady() && !decodedAudio) {
        std::cerr << "No decoded DVD audio\n"; return 8;
    }
    std::cout << "Decoded audio: " << decodedAudio << '\n';
    for (int i = 2; i < argc; ++i) {
        const double destination = std::stod(argv[i]);
        std::cout << "Seeking to " << destination << std::endl;
        player.seek(destination);
        if (!frames(24, destination)) return 7;
    }
    player.close();
    SDL_Quit();
    std::cout << "PASS: DVD playback startup\n";
}
