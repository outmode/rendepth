// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "DiscReadAhead.h"
#include <iostream>
#include <stdexcept>

// Fail immediately when a read-ahead regression check does not hold.
static void require(bool condition) {
    if (!condition) throw std::runtime_error("Disc read-ahead regression");
}

// Verify buffered reads, logical seeks, resets, EOF, and I/O failure propagation.
int main() {
    std::vector<uint8_t> source(29);
    for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i);
    int64_t physicalPosition = 0;
    DiscReadAhead input([&](uint8_t* output, int size) {
        const auto count = std::min<int64_t>(size, source.size() - physicalPosition);
        std::memcpy(output, source.data() + physicalPosition, count);
        physicalPosition += count;
        return static_cast<int>(count);
    }, [&](int64_t offset, int whence) -> int64_t {
        if (whence == AVSEEK_SIZE) return source.size();
        const auto target = whence == SEEK_END ? static_cast<int64_t>(source.size()) + offset : offset;
        if (target < 0 || target > static_cast<int64_t>(source.size())) return AVERROR(EINVAL);
        return physicalPosition = target;
    }, 8, 0);

    uint8_t bytes[16]{};
    require(input.read(bytes, 3) == 3 && bytes[0] == 0 && bytes[2] == 2);
    require(input.seek(0, AVSEEK_SIZE) == 29);
    // A seek relative to the consumer must not use the prefetched position.
    require(input.seek(0, SEEK_CUR) == 3);
    require(input.read(bytes, 5) == 5 && bytes[0] == 3 && bytes[4] == 7);
    require(input.seek(-2, SEEK_CUR) == 6);
    require(input.read(bytes, 2) == 2 && bytes[0] == 6 && bytes[1] == 7);
    require(input.seek(-3, SEEK_END) == 26);
    require(input.read(bytes, 16) == 3 && bytes[0] == 26 && bytes[2] == 28);
    require(input.read(bytes, 1) == AVERROR_EOF);
    require(input.seek(0, SEEK_SET) == 0);
    for (int next = 0; next < 29;) {
        const int count = input.read(bytes, 5);
        require(count > 0);
        for (int i = 0; i < count; ++i) require(bytes[i] == next++);
    }
    require(input.read(bytes, 1) == AVERROR_EOF);

    // The disc's time/chapter seek happens outside AVIO. Join first and
    // discard old input before adopting its new byte position.
    require(input.seek(0, SEEK_SET) == 0);
    require(input.read(bytes, 1) == 1);
    input.reset();
    physicalPosition = 17;
    input.setPosition(17);
    require(input.read(bytes, 3) == 3 && bytes[0] == 17 && bytes[2] == 19);

    DiscReadAhead broken([](uint8_t*, int) { return AVERROR(EIO); },
        [](int64_t, int) -> int64_t { return AVERROR(EIO); }, 8, 0);
    require(broken.read(bytes, 1) == AVERROR(EIO));
    std::cout << "PASS: read-ahead bytes, partial reads, logical seeks, EOF, reset and I/O errors\n";
}
