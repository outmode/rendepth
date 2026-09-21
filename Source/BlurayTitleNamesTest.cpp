// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "BlurayTitleNames.h"
#include <array>
#include <cassert>
#include <iostream>

using Command = std::array<uint32_t, 3>;
std::vector<uint8_t> movieObject(std::initializer_list<Command> commands) {
    std::vector<uint8_t> bytes(50, 0);
    bytes[0] = 'M'; bytes[1] = 'O'; bytes[2] = 'B'; bytes[3] = 'J';
    bytes[49] = 1;
    const auto append = [&](uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) bytes.push_back(uint8_t(value >> (i * 8)));
    };
    append(0, 2); append(commands.size(), 2);
    for (const auto& cmd : commands) for (auto word : cmd) append(word, 4);
    const uint32_t length = bytes.size() - 44;
    for (int i = 0; i < 4; ++i) bytes[40 + i] = uint8_t(length >> (24 - 8 * i));
    return bytes;
}
int main() {
    const auto check = [](std::initializer_list<Command> commands, std::set<uint32_t> expected) {
        const auto refs = BlurayTitleNames::playlistReferences(movieObject(commands));
        assert(refs.size() == 1 && refs[0] == expected);
    };
    check({{0x22800000, 210, 0}}, {210}); // Immediate PlayPL.
    check({{0x50400001, 4075, 133}, {0x22000000, 4075, 0}}, {133});
    check({{0x22000000, 4075, 0}}, {}); // Unknown register.
    check({{0x50400001, 4074, 133}, {0x22000000, 4075, 0}}, {});
    check({{0x20810000, 2, 0}, {0x50400001, 4075, 133}, {0x22000000, 4075, 0}}, {});
    check({{0x20010000, 8, 0}, {0x50400001, 4075, 133}, {0x22000000, 4075, 0}}, {});
    check({{0x48400200, 1, 0}, {0x50400001, 4075, 133}, {0x22000000, 4075, 0}}, {});
    check({{0x50400001, 4075, 133}, {0x22030000, 4075, 0}}, {}); // TerminatePL.
    check({{0x22800000, 100000, 0}}, {});
    check({{0x22800000, 210, 0}, {0x22800000, 211, 0}}, {});
    check({{0x22800000, 210, 0}, {0x22000000, 4075, 0}}, {});
    check({{0x42820000, 210, 1}}, {}); // Chapter-only reference.
    auto bytes = movieObject({{0x22800000, 210, 0}});
    for (size_t size = 0; size < bytes.size(); ++size)
        assert(BlurayTitleNames::playlistReferences({bytes.data(), size}).empty());
    bytes[0] = 'X';
    assert(BlurayTitleNames::playlistReferences(bytes).empty());
    std::cout << "PASS: explicit playlist names, ambiguous control flow, malformed movie objects\n";
}
