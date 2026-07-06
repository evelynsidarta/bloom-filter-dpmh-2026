#pragma once

#include <cstdint>
#include <cstddef>

inline uint64_t rotate_left(uint64_t value, int shift) {
    shift = shift & 63;
    if (shift == 0) {
        return value;
    }
    return (value << shift) | (value >> (64 - shift));
}

inline int ceil_log2(uint64_t x) {
    if (x <= 1) {
        return 0;
    }
    x = x - 1;
    int result = 0;
    while (x > 0) {
        x = x >> 1;
        result = result + 1;
    }
    return result;
}

inline void setBit(uint8_t* bytes, std::size_t idx) {
    bytes[idx / 8] |= static_cast<uint8_t>(1u << (idx % 8));
}

inline bool getBit(const uint8_t* bytes, std::size_t idx) {
    return ((bytes[idx / 8] & static_cast<uint8_t>(1u << (idx % 8))) != 0);
}