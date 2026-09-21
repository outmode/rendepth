// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <cstdint>
struct AVPacket;
struct AVFrame;

// Adds the dependent view to the existing libbluray/FFmpeg playback stream.
// Output is an owned, full-resolution side-by-side YUV420P AVFrame.
class MvcDecoder {
public:
    MvcDecoder();
    ~MvcDecoder();
    bool open(const std::filesystem::path& path, int titleIndex,
        std::function<bool()> interrupted, std::string& error,
        std::shared_ptr<std::mutex> discReadMutex = {});
    // Convert clip-local MPEG-TS timestamps to the continuous title timeline.
    // Offset is in 90 kHz ticks, including the first clip's timestamp origin.
    int64_t timestampOffset(int64_t bytePosition) const;
    bool send(const AVPacket& base, std::string& error);
    AVFrame* receive(); // caller owns the returned frame
    void flush();
    bool finish(std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
