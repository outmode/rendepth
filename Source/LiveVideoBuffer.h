// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <memory>

// Keep display cadence independent of inference cadence. Delay grows to cover
// observed inference latency, but never accumulates without bound. Frame is a
// VideoFrame-like type so the timing policy can be tested without a GPU.
template<class Frame> class LiveVideoBuffer {
public:
    void clear() { frames.clear(); bytes = 0; delay = 0; latestTime = -1; }
    void observeDepth(double timestamp) {
        if (latestTime >= timestamp)
            delay = std::max(delay, std::min(0.250, latestTime - timestamp + 0.025));
    }
    void push(std::shared_ptr<Frame> frame, double now) {
        if (!frames.empty() && (frame->generation != frames.back().frame->generation ||
            frame->width != frames.back().frame->width ||
            frame->height != frames.back().frame->height ||
            frame->presentationTime <= latestTime)) clear();
        latestTime = frame->presentationTime;
        std::size_t size = frame->inferenceRGBA.size();
        for (const auto& plane : frame->planes) size += plane.size();
        frames.push_back({std::move(frame), now, size});
        bytes += size;
        while (frames.size() > 1 && (bytes > maxBytes || frames.size() > 32 ||
            now - frames.front().arrival > 0.350)) pop();
    }
    std::shared_ptr<Frame> take(double now) {
        std::shared_ptr<Frame> result;
        while (!frames.empty() && frames.front().arrival + delay <= now) {
            result = frames.front().frame;
            pop();
        }
        return result;
    }
    double delaySeconds() const { return delay; }
    std::size_t bufferedBytes() const { return bytes; }
private:
    struct Entry { std::shared_ptr<Frame> frame; double arrival; std::size_t bytes; };
    void pop() { bytes -= frames.front().bytes; frames.pop_front(); }
    static constexpr std::size_t maxBytes = 128 * 1024 * 1024;
    std::deque<Entry> frames;
    std::size_t bytes = 0;
    double delay = 0;
    double latestTime = -1;
};
