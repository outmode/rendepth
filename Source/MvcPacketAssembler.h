// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// With the AVC parser disabled, MPEG-TS can split a large MVC picture into
// packets whose continuations have no PTS. Do not publish it until the next
// timestamp (in decode order) or EOF proves that all its bytes have arrived.
class MvcPacketAssembler {
public:
    struct Packet {
        int64_t pts;
        std::vector<uint8_t> bytes;
    };
    static constexpr size_t maxPictureBytes = 4 * 1024 * 1024;

    bool push(std::optional<int64_t> pts, std::span<const uint8_t> bytes,
        std::optional<Packet>& completed) {
        completed.reset();
        if (pts && pending && *pts != pending->pts) completed = finish();
        if (!pending) {
            // A seek may land on a continuation of an earlier picture.
            if (!pts) return true;
            pending = Packet{*pts, {}};
        }
        if (bytes.size() > maxPictureBytes - pending->bytes.size()) {
            reset();
            return false;
        }
        pending->bytes.insert(pending->bytes.end(), bytes.begin(), bytes.end());
        return true;
    }
    std::optional<Packet> finish() { return std::exchange(pending, std::nullopt); }
    void reset() { pending.reset(); }

private:
    std::optional<Packet> pending;
};
