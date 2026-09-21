// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <set>
#include <span>
#include <vector>

namespace BlurayTitleNames {
// Read explicit playlist references in HDMV movie objects. This is metadata
// inspection, not navigation execution: never guess register-dependent targets.
inline std::vector<std::set<uint32_t>> playlistReferences(std::span<const uint8_t> bytes) {
    auto contains = [&](size_t offset, size_t count) {
        return offset <= bytes.size() && count <= bytes.size() - offset;
    };
    auto u16 = [&](size_t offset) -> uint16_t {
        return uint16_t(bytes[offset]) << 8 | bytes[offset + 1];
    };
    auto u32 = [&](size_t offset) -> uint32_t {
        return uint32_t(u16(offset)) << 16 | u16(offset + 2);
    };
    if (!contains(0, 50) || u32(0) != 0x4d4f424a) return {};
    const size_t length = u32(40);
    if (length < 6 || !contains(44, length)) return {};
    const size_t end = 44 + length;
    std::vector<std::set<uint32_t>> result(u16(48));
    size_t cursor = 50;
    for (auto& playlists : result) {
        if (cursor > end || end - cursor < 4) return {};
        const unsigned count = u16(cursor + 2);
        cursor += 4;
        if (count > (end - cursor) / 12) return {};
        std::set<unsigned> jumpTargets;
        bool indirectGoto = false;
        bool unresolvedPlaylist = false;
        for (unsigned i = 0; i < count; ++i) {
            const auto at = cursor + i * 12;
            const auto op = u32(at);
            // Branch / Goto: command indices are zero-based.
            if (((op >> 24) & 31) == 0 && ((op >> 16) & 15) == 1) {
                if (op & 0x00800000) jumpTargets.insert(u32(at + 4));
                else indirectGoto = true;
            }
        }
        for (unsigned i = 0; i < count; ++i) {
            const auto at = cursor + i * 12;
            const auto op = u32(at);
            const auto option = (op >> 16) & 15;
            // Only whole-playlist playback identifies a title; a chapter or
            // bookmark reference is not a name for the entire playlist.
            if (((op >> 24) & 31) != 2 || option != 0 || (op >> 29) == 0) continue;
            uint32_t playlist = u32(at + 4);
            if (!(op & 0x00800000)) {
                // Accept an immediately preceding Move register, constant only
                // when it cannot be skipped by a comparison or a Goto.
                if (i == 0 || indirectGoto || jumpTargets.contains(i) ||
                    u32(at - 12) != 0x50400001 || u32(at - 8) != playlist ||
                    (i > 1 && ((u32(at - 24) >> 27) & 3) == 1)) {
                    unresolvedPlaylist = true;
                    continue;
                }
                playlist = u32(at - 4);
            }
            if (playlist <= 99999) playlists.insert(playlist);
            else unresolvedPlaylist = true;
        }
        // A menu entry that plays multiple videos is not a name for each one.
        if (unresolvedPlaylist || playlists.size() > 1) playlists.clear();
        cursor += count * 12;
    }
    return result;
}
}
