// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "AudioWaveform.h"
#include <cassert>
#include <cstdio>
#include <vector>

// Verify waveform energy, playback alignment, volume, reset behavior, and bounded history.
int main() {
    AudioWaveform waveform;
    std::vector<float> samples(48000 * 2, 0.0f);
    waveform.append(samples.data(), 48000, 2, 48000, 0.0);
    assert(waveform.at(0.5) == AudioWaveform::Bars{});

    // Opposite stereo channels must retain energy rather than cancel out.
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = i % 2 ? -0.2f : 0.2f;
    waveform.append(samples.data(), 48000, 2, 48000, 1.0);
    assert(waveform.at(1.0) == AudioWaveform::Bars{}); // queued-ahead sound stays invisible
    for (float level : waveform.at(1.5)) assert(std::abs(level - 0.5f) < 0.001f);
    assert(waveform.at(1.5, 0.0f) == AudioWaveform::Bars{});
    assert(waveform.at(-1.0) == AudioWaveform::Bars{});

    // Decoder packet boundaries must not change the waveform.
    AudioWaveform split;
    for (int i = 0; i < 48000; i += 137) {
        const int count = std::min(137, 48000 - i);
        split.append(samples.data() + i * 2, count, 2, 48000, 1.0 + i / 48000.0);
    }
    assert(split.at(1.5) == waveform.at(1.5));
    waveform.clear();
    assert(waveform.at(1.5) == AudioWaveform::Bars{});
    // Long playback evicts old history and retains the current audible bins.
    for (int second = 0; second < 20; ++second)
        waveform.append(samples.data(), 48000, 2, 48000, second);
    assert(waveform.at(1.5) == AudioWaveform::Bars{});
    for (float level : waveform.at(19.5)) assert(std::abs(level - 0.5f) < 0.001f);
    std::puts("Audio waveform: silence, stereo energy, playback timing, mute, packet boundaries, reset and bounded history passed");
}
