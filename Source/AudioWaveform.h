// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>

// Timestamped 10 ms RMS bins. Keep decoded-ahead samples separate from the
// audible history; callers provide the playback clock when requesting bars.
class AudioWaveform {
public:
    static constexpr size_t barCount = 40;
    using Bars = std::array<float, barCount>;

    void clear() { bins.clear(); }

    void append(const float* samples, int frames, int channels, int sampleRate, double start) {
        if (!samples || frames <= 0 || channels <= 0 || sampleRate < 100) return;
        const int binFrames = sampleRate / 100;
        const auto firstSample = static_cast<int64_t>(std::llround(start * sampleRate));
        for (int frame = 0; frame < frames;) {
            const auto sample = firstSample + frame;
            const auto index = sample / binFrames;
            const int count = std::min<int64_t>(frames - frame, binFrames - sample % binFrames);
            if (!bins.empty() && index < bins.back().index) clear();
            if (bins.empty() || bins.back().index != index) bins.push_back({index});
            auto& bin = bins.back();
            for (int i = 0; i < count * channels; ++i) {
                const float value = samples[frame * channels + i];
                if (std::isfinite(value)) bin.energy += double(value) * value;
            }
            bin.samples += count * channels;
            frame += count;
            // More than the player's three-second decode buffer plus history.
            while (bins.size() > 1000) bins.pop_front();
        }
    }

    Bars at(double seconds, float gain = 1.0f) const {
        Bars result{};
        if (seconds < 0.0) return result;
        const auto end = static_cast<int64_t>(std::floor(seconds * 100.0));
        const auto begin = end - static_cast<int64_t>(barCount);
        for (const auto& bin : bins) {
            if (bin.index < begin) continue;
            if (bin.index >= end) break;
            result[bin.index - begin] = std::clamp(
                static_cast<float>(std::sqrt(bin.energy / bin.samples)) * gain * 2.5f, 0.0f, 1.0f);
        }
        return result;
    }

private:
    struct Bin { int64_t index; double energy = 0.0; int samples = 0; };
    std::deque<Bin> bins;
};
