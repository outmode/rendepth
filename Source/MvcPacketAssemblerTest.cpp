// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "MvcPacketAssembler.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition) {
    if (!condition) throw std::runtime_error("MVC packet assembly regression");
}

int main() {
    MvcPacketAssembler assembler;
    std::optional<MvcPacketAssembler::Packet> completed;
    // Sizes observed on the disc: FFmpeg emits the tail without a timestamp.
    std::vector<uint8_t> first(204632, 0x12), tail(43588, 0x34);
    const std::vector<uint8_t> next{0, 0, 1, 20, 0x56};
    require(assembler.push(28135620, first, completed) && !completed);
    require(assembler.push(std::nullopt, tail, completed) && !completed);
    require(assembler.push(28139374, next, completed) && completed);
    auto expected = first;
    expected.insert(expected.end(), tail.begin(), tail.end());
    require(completed->pts == 28135620 && completed->bytes == expected);
    auto final = assembler.finish();
    require(final && final->pts == 28139374 && final->bytes == next);
    require(!assembler.finish());

    // Multiple fragments, including a repeated timestamp and a split NAL
    // start code, are joined byte-for-byte. B-picture PTS can go backwards.
    const std::vector<uint8_t> prefix{0, 0}, suffix{1, 20, 0xab};
    require(assembler.push(300, prefix, completed) && !completed);
    require(assembler.push(300, suffix, completed) && !completed);
    require(assembler.push(std::nullopt, tail, completed) && !completed);
    require(assembler.push(100, next, completed) && completed);
    expected = {0, 0, 1, 20, 0xab};
    expected.insert(expected.end(), tail.begin(), tail.end());
    require(completed->pts == 300 && completed->bytes == expected);

    // Seek/cancellation discards the old partial picture; orphan continuations
    // must not contaminate the first timestamped picture after the seek.
    assembler.reset();
    require(assembler.push(std::nullopt, tail, completed) && !completed);
    require(!assembler.finish());
    require(assembler.push(500, next, completed) && !completed);
    final = assembler.finish();
    require(final && final->pts == 500 && final->bytes == next);

    // Bound memory even for malformed input containing endless continuations.
    std::vector<uint8_t> limit(MvcPacketAssembler::maxPictureBytes, 0);
    require(assembler.push(600, limit, completed) && !completed);
    require(!assembler.push(std::nullopt, next, completed));
    require(!assembler.finish());
    require(assembler.push(700, next, completed) && !completed);
    final = assembler.finish();
    require(final && final->bytes == next);
    std::cout << "PASS: MVC fragments, decode order, EOF, seek reset and input limit\n";
}
