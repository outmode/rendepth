// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "BlurayPlaylist.h"
#include <array>
#include <iostream>
#include <cstring>

// Check MVC playlist detection against valid, truncated, and unsupported binary fixtures.
int main() {
    std::array<uint8_t, 80> data{};
    data[0] = 'M'; data[1] = 'P'; data[2] = 'L'; data[3] = 'S';
    data[19] = 40; // Extension starts at byte 40.
    data[43] = 36; // Extension size, excluding its length field.
    data[51] = 1;  // One extension entry.
    data[53] = 2; data[55] = 2; // Subpath extension 2.2.
    data[59] = 24; data[63] = 16; // Relative offset and size.
    data[67] = 12; data[69] = 1; // Subpath data length and count.
    data[73] = 6; data[75] = 8; data[79] = 1; // MVC, one playitem.
    if (!BlurayPlaylist::hasMvc(data)) return 1;
    for (size_t size = 0; size < data.size(); ++size)
        if (BlurayPlaylist::hasMvc(std::span(data).first(size))) return 2;
    data[75] = 7; // Picture-in-picture is not MVC.
    if (BlurayPlaylist::hasMvc(data)) return 3;
    data[75] = 8; data[79] = 0; // Empty subpath.
    if (BlurayPlaylist::hasMvc(data)) return 4;
    data[79] = 1; data[59] = 255; // Out-of-bounds extension offset.
    if (BlurayPlaylist::hasMvc(data)) return 5;
    data[59] = 24; data[73] = 255; // Oversized subpath.
    if (BlurayPlaylist::hasMvc(data)) return 6;
    data[73] = 6; data[19] = 0; // Plain 2D playlist.
    if (BlurayPlaylist::hasMvc(data)) return 7;
    data[19] = 40;
    std::vector<uint8_t> playlist(data.begin(), data.end());
    playlist.resize(110);
    playlist[43] = 66; playlist[63] = 46; playlist[67] = 42; playlist[73] = 36;
    playlist[81] = 28;
    std::memcpy(playlist.data() + 82, "00113M2TS", 9);
    playlist[99] = 1; // in time
    playlist[103] = 3; // out time
    playlist[105] = 2; // linked base playitem
    playlist[109] = 1; // sync time
    std::vector<BlurayPlaylist::MvcClip> clips;
    if (!BlurayPlaylist::hasMvc(playlist, &clips) || clips.size() != 1 ||
        clips[0].clip != "00113" || clips[0].playItem != 2 || clips[0].inTime != 1 ||
        clips[0].outTime != 3 || clips[0].syncTime != 1) return 8;
    playlist[94] = 1; // unsupported multi-clip subplayitem
    if (BlurayPlaylist::hasMvc(playlist, &clips) || !clips.empty()) return 9;
    std::cout << "PASS: MVC, 2D, PiP, empty, truncated and malformed playlists\n";
}
