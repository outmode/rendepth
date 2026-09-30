// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <utility>
#include <vector>
extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

// One consumer, one outstanding disc read. The demuxer stays on the consumer
// thread; only raw I/O runs in the background. Reset before any external seek.
class DiscReadAhead {
public:
    using Read = std::function<int(uint8_t*, int)>;
    using Seek = std::function<int64_t(int64_t, int)>;
    DiscReadAhead(Read read, Seek seek, int blockSize, int64_t position)
        : readSource(std::move(read)), seekSource(std::move(seek)),
          blockSize(blockSize), position(position) {}
    ~DiscReadAhead() { reset(); }

    void reset() {
        if (pending.valid()) pending.wait();
        pending = {};
        block = {};
        cursor = 0;
    }
    void setPosition(int64_t value) { position = value; }

    int read(uint8_t* output, int size) {
        if (size <= 0) return AVERROR(EINVAL);
        if (cursor == block.bytes.size()) {
            if (block.result < 0) return block.result;
            try {
                if (!pending.valid()) start();
                block = pending.get();
                cursor = 0;
                if (block.result < 0) return block.result;
                start();
            } catch (const std::bad_alloc&) { return AVERROR(ENOMEM); }
              catch (const std::system_error&) { return AVERROR(EIO); }
        }
        const auto count = std::min(static_cast<size_t>(size), block.bytes.size() - cursor);
        std::memcpy(output, block.bytes.data() + cursor, count);
        cursor += count;
        position += static_cast<int64_t>(count);
        return static_cast<int>(count);
    }
    int64_t seek(int64_t offset, int whence) {
        whence &= ~AVSEEK_FORCE;
        if (whence == AVSEEK_SIZE) return seekSource(offset, whence);
        // The source is ahead of the consumer by up to two buffers.
        if (whence == SEEK_CUR) {
            if (offset < -position || offset > std::numeric_limits<int64_t>::max() - position)
                return AVERROR(EINVAL);
            offset += position;
            whence = SEEK_SET;
        }
        reset();
        const auto result = seekSource(offset, whence);
        if (result >= 0) position = result;
        else block.result = static_cast<int>(result);
        return result;
    }

private:
    struct Block { std::vector<uint8_t> bytes; int result = 0; };
    void start() {
        pending = std::async(std::launch::async, [this] {
            Block next;
            next.bytes.resize(blockSize);
            const int count = readSource(next.bytes.data(), blockSize);
            next.result = count > 0 ? count : count < 0 ? count : AVERROR_EOF;
            next.bytes.resize(count > 0 ? count : 0);
            return next;
        });
    }
    Read readSource;
    Seek seekSource;
    int blockSize;
    int64_t position;
    Block block;
    size_t cursor = 0;
    std::future<Block> pending;
};
