#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <vector>
#include <unordered_set>

// this file basically contains a collection of helper functions
//      used by test and implementation files

inline std::uint64_t rotate_left(std::uint64_t value, int shift) {
    shift = shift & 63;
    if (shift == 0) {
        return value;
    }
    return (value << shift) | (value >> (64 - shift));
}

inline int ceil_log2(std::uint64_t x) {
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

inline void setBit(std::uint8_t* bytes, std::size_t idx) {
    bytes[idx / 8] |= static_cast<uint8_t>(1u << (idx % 8));
}

inline bool getBit(const std::uint8_t* bytes, std::size_t idx) {
    return ((bytes[idx / 8] & static_cast<std::uint8_t>(1u << (idx % 8))) != 0);
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
inline std::uint64_t murmurHash(std::uint64_t x) {
    x = x ^ (x >> 33);
    x = x * 0xff51afd7ed558ccdULL;
    x = x ^ (x >> 33);
    x = x * 0xc4ceb9fe1a85ec53ULL;
    x = x ^ (x >> 33);
    return x;
}

inline std::size_t generate_bit(std::uint64_t hash, std::size_t i, std::size_t num_bits) {
    // scramble the bits to introduce more randomness
    //      then extract values out of it to actually set the bit
    //      using MurmurHash3
    hash = hash + i * 0x9e3779b97f4a7c15ULL;
    hash = hash ^ (hash >> 33);
    hash = hash * 0xff51afd7ed558ccdULL;
    hash = hash ^ (hash >> 33);
    hash = hash * 0xc4ceb9fe1a85ec53ULL;
    hash = hash ^ (hash >> 33);
    hash = hash % num_bits;
    return static_cast<std::size_t>(hash);
}

// read boolean result from bit-packed result array 
//      produced by the batch contains method
inline bool packed_result_bit(const std::uint8_t* result, std::size_t idx) {
    return (result[idx / 8] & static_cast<std::uint8_t>(1u << (idx % 8))) != 0;
}

// produce unique and disjoint hash sets
//      guarantees that we have no duplicates within a generated set
//      and no duplicates between different generated sets
inline std::vector<std::uint64_t> generate_unique(std::size_t count, std::size_t seed,
                                                        std::unordered_set<std::uint64_t>& used) {
    // generated set will remember all hashes that have been generated
    //      to prevent duplicates within the same vector
    //      and prevent overlap between previously-generated vectors
    std::vector<std::uint64_t> hashes;
    hashes.reserve(count);
    std::uint64_t value = seed;
    while (hashes.size() < count) {
        const std::uint64_t hash = murmurHash(value++);
        // if doesn't exist in used
        if (used.insert(hash).second) {
            hashes.push_back(hash);
        }
    }
    return hashes;
}

// for testing:
//      used to build a vector containing both hash values that are
//      already inserted and also hash values that are
//      not inserted yet, basically just generates an alternating mix between
//      two given vectors
inline std::vector<std::uint64_t> mix_vector(const std::vector<std::uint64_t>& v1,
                                                const std::vector<std::uint64_t>& v2) {
    // to pass: v1 already inserted values
    //          v2 not yet inserted values
    std::vector<std::uint64_t> v_mixed;
    v_mixed.reserve(v1.size() + v2.size());
    // assume size of both vectors are the same
    for (std::size_t i = 0; i < v1.size(); i++) {
        v_mixed.push_back(v1[i]);
        v_mixed.push_back(v2[i]);
    }
    return v_mixed;
}

// helper function to get the median of the result vector
inline double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    std::size_t middle_pos = values.size() / 2;
    if (values.size() % 2 != 0) {
        return values[middle_pos];
    }
    return (values[middle_pos - 1] + values[middle_pos]) / 2.0;
}

// return tuple count
inline std::size_t result_bytes(std::size_t tuple_count) {
    return (tuple_count + 7) / 8;
}

// count all the true bits from a packed contains function
inline std::size_t count_packed_true(const std::vector<std::uint8_t>& result,
                                    std::size_t tuple_count) {
    std::size_t filled_bytes = tuple_count / 8;
    std::size_t count = 0;
    for (std::size_t i = 0; i < filled_bytes; i++) {
        count = count + static_cast<std::size_t>(__builtin_popcount(static_cast<unsigned>(result[i])));
    }
    // check tail bits
    unsigned tail_bits = static_cast<unsigned>(tuple_count % 8);
    if (tail_bits != 0) {
        std::uint8_t mask = static_cast<std::uint8_t>((1 << tail_bits) - 1);
        count = count + static_cast<std::size_t>(__builtin_popcount(static_cast<unsigned>(result[filled_bytes] & mask))); 
    }
    return count;
}
