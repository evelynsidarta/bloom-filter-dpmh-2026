#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <vector>

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

// calculates how many bits would be used for the basic bloom filter
//      such that it has the same size as an ArrowBloomFilter
//      since ArrowBloomFilter uses the next power-of-two size
// done in order to be fair to the basic implementation
inline std::size_t calculate_arrow_equivalent_bits(std::size_t rows_to_insert,
                                                std::size_t bits_per_key,
                                                std::size_t min_num_bits) {
    std::size_t x = std::max(min_num_bits, rows_to_insert * bits_per_key) - 1;
    for (std::size_t i = 1; i < sizeof(std::size_t) * 8; i <<= 1) {
        x = x | (x >> i);
    }
    return x + 1;
}

// to generate the hash values inputted into the bloom filters
inline uint64_t murmurHash(uint64_t x) {
    x = x ^ (x >> 33);
    x = x * 0xff51afd7ed558ccdULL;
    x = x ^ (x >> 33);
    x = x * 0xc4ceb9fe1a85ec53ULL;
    x = x ^ (x >> 33);
    return x;
}

// for generating the hashes to be inserted into the bloom filters
inline std::vector<std::uint64_t> generate_hashes(std::size_t count, std::uint64_t seed) {
    std::vector<std::uint64_t> hashes;
    hashes.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        hashes.push_back(murmurHash(seed + static_cast<std::uint64_t>(i)));
    }
    return hashes;
}