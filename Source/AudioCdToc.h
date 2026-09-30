#pragma once
#include "AudioCdReader.h"
#include <span>

// Decode an MMC format-0 TOC with LBA addresses (used by macOS). Keep data
// tracks as boundaries, but never expose their sectors as PCM audio.
inline std::vector<AudioCdStream::Track> audioCdTracks(std::span<const uint8_t> toc) {
    if (toc.size() < 4) return {};
    const size_t length = (size_t(toc[0]) << 8 | toc[1]) + 2;
    const int first = toc[2], last = toc[3];
    if (first < 1 || last < first || last > 99 || length > toc.size() ||
        length != 4 + size_t(last - first + 2) * 8) return {};
    auto sector = [&](size_t offset) -> int64_t {
        return int64_t(toc[offset + 4]) << 24 | int64_t(toc[offset + 5]) << 16 |
            int64_t(toc[offset + 6]) << 8 | toc[offset + 7];
    };
    std::vector<AudioCdStream::Track> tracks;
    for (int track = first; track <= last; ++track) {
        const size_t offset = 4 + size_t(track - first) * 8;
        const auto begin = sector(offset), end = sector(offset + 8);
        if (toc[offset + 2] != track ||
            toc[offset + 10] != (track == last ? 0xaa : track + 1) ||
            begin >= end || end > INT32_MAX) return {};
        if (!(toc[offset + 1] & 4)) tracks.push_back({track, int(begin), int(end)});
    }
    return tracks;
}
