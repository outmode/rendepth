// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace BlurayPlaylist {
struct MvcClip {
    std::string clip;
    uint16_t playItem = 0;
    uint32_t inTime = 0, outTime = 0, syncTime = 0; // 45 kHz
};
// MPLS extension 2.2 contains subpaths; type 8 is the MVC dependent view.
// Inspect the selected playlist, not the disc-wide 3D flag: extras and 2D
// versions on the same disc need not contain an MVC view.
inline bool hasMvc(std::span<const uint8_t> bytes, std::vector<MvcClip>* clips = nullptr) {
    if (clips) clips->clear();
    auto contains = [&](size_t offset, size_t count) {
        return offset <= bytes.size() && count <= bytes.size() - offset;
    };
    auto u16 = [&](size_t offset) -> uint16_t {
        return (uint16_t(bytes[offset]) << 8) | bytes[offset + 1];
    };
    auto u32 = [&](size_t offset) -> uint32_t {
        return (uint32_t(u16(offset)) << 16) | u16(offset + 2);
    };
    if (!contains(0, 20) || bytes[0] != 'M' || bytes[1] != 'P' ||
        bytes[2] != 'L' || bytes[3] != 'S') return false;
    const size_t extension = u32(16);
    if (!extension || !contains(extension, 12)) return false;
    const size_t length = u32(extension);
    if (length < 8 || !contains(extension + 4, length)) return false;
    const size_t end = extension + 4 + length;
    const unsigned entries = bytes[extension + 11];
    if (size_t(entries) * 12 > end - extension - 12) return false;
    for (unsigned i = 0; i < entries; ++i) {
        const size_t entry = extension + 12 + i * 12;
        if (u16(entry) != 2 || u16(entry + 2) != 2) continue;
        const size_t offset = u32(entry + 4), size = u32(entry + 8);
        if (offset > end - extension || size > end - extension - offset || size < 6) continue;
        const size_t start = extension + offset;
        const size_t subLength = u32(start);
        if (subLength < 2 || subLength > size - 4) continue;
        const size_t subEnd = start + 4 + subLength;
        const unsigned count = u16(start + 4);
        size_t cursor = start + 6;
        for (unsigned sub = 0; sub < count; ++sub) {
            if (subEnd - cursor < 10) break;
            const size_t subSize = u32(cursor);
            if (subSize < 6 || subSize > subEnd - cursor - 4) break;
            if (bytes[cursor + 5] == 8 && bytes[cursor + 9] != 0) {
                if (!clips) return true;
                std::vector<MvcClip> parsed;
                size_t item = cursor + 10;
                const size_t itemEnd = cursor + 4 + subSize;
                for (unsigned n = 0; n < bytes[cursor + 9]; ++n) {
                    if (itemEnd - item < 2) return false;
                    const size_t itemSize = u16(item);
                    item += 2;
                    if (itemSize < 28 || itemSize > itemEnd - item ||
                        (bytes[item + 12] & 1)) return false; // multi-clip requires angle selection
                    std::string id(reinterpret_cast<const char*>(bytes.data() + item), 5);
                    if (id.find_first_not_of("0123456789") != std::string::npos) return false;
                    if (u32(item + 5) != 0x4d325453 || u32(item + 18) <= u32(item + 14)) return false;
                    parsed.push_back({id, u16(item + 22), u32(item + 14),
                        u32(item + 18), u32(item + 24)});
                    item += itemSize;
                }
                *clips = std::move(parsed);
                return true;
            }
            cursor += 4 + subSize;
        }
    }
    return false;
}
}
