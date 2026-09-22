// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
// Standalone: c++ -std=c++17 Source/LiveVideoBufferTest.cpp -o /tmp/live-buffer-test
#include "LiveVideoBuffer.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

struct Frame {
    double presentationTime;
    unsigned generation = 1;
    int width = 1920, height = 1080;
    std::array<std::vector<std::uint8_t>, 3> planes;
    std::vector<std::uint8_t> inferenceRGBA;
};
// Create a minimal frame carrying a known presentation timestamp.
static auto frame(double time) {
    auto result = std::make_shared<Frame>();
    result->presentationTime = time;
    return result;
}
// Verify adaptive capture delay, bounded buffering, and recovery after playback stalls.
int main() {
    LiveVideoBuffer<Frame> buffer;
    buffer.push(frame(1), 10);
    assert(buffer.take(10)->presentationTime == 1); // SBS/no-depth: no delay.
    buffer.observeDepth(0.825);
    assert(std::abs(buffer.delaySeconds() - .2) < .000001);
    buffer.push(frame(2), 11);
    assert(!buffer.take(11.19));
    assert(buffer.take(11.201)->presentationTime == 2);
    buffer.observeDepth(1.99);
    assert(std::abs(buffer.delaySeconds() - .2) < .000001); // No delay oscillation.
    buffer.observeDepth(0);
    assert(buffer.delaySeconds() == .25); // Slow/stalled inference cannot grow it.
    buffer.clear();
    buffer.push(frame(0), 0);
    buffer.observeDepth(0);
    int presented = 0;
    for (int i = 1; i <= 180; ++i) {
        const double time = i / 60.0;
        buffer.push(frame(time), time);
        if (buffer.take(time)) ++presented;
    }
    assert(presented >= 178); // Buffer preserves 60 Hz cadence after filling.
    assert(buffer.take(4)); // A stopped source drains without another arrival.
    assert(!buffer.take(5));
    buffer.clear();
    buffer.push(frame(4), 1);
    buffer.observeDepth(3);
    auto replacement = frame(5);
    replacement->generation = 2;
    buffer.push(replacement, 1.01);
    assert(buffer.delaySeconds() == 0);
    assert(buffer.take(1.01) == replacement);
    for (int i = 0; i < 40; ++i) {
        auto large = frame(i + 6);
        large->planes[0].resize(8 * 1024 * 1024);
        buffer.push(large, i / 100.0);
        assert(buffer.bufferedBytes() <= 128 * 1024 * 1024);
    }
    assert(buffer.take(2)->presentationTime == 45); // Skip backlog after a stall.
    buffer.clear();
    assert(buffer.bufferedBytes() == 0 && buffer.delaySeconds() == 0);
    std::cout << "Live capture timing and buffer bounds passed.\n";
}
