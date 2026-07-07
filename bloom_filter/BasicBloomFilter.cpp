#include "BasicBloomFilter.h"

BasicBloomFilter::BasicBloomFilter(std::size_t num_bits, std::size_t num_hashes)
    : bitvector((num_bits + 7) / 8, 0), num_hashes(num_hashes), num_bits(num_bits) {}

void BasicBloomFilter::insertImpl(std::uint64_t hash) {
    for (std::size_t i = 0; i < num_hashes; i++) {
        setBit(generate_bit(hash, i));
    }
}

bool BasicBloomFilter::containsImpl(std::uint64_t hash) const {
    for (std::size_t i = 0; i < num_hashes; i++) {
        if (!getBit(generate_bit(hash, i))) {
            return false;
        }
    }
    return true;
}

void BasicBloomFilter::setBit(std::size_t idx) {
    const std::size_t byte_idx = idx / 8;
    const std::size_t bit_idx = idx % 8;
    bitvector[byte_idx] = bitvector[byte_idx] | static_cast<std::uint8_t>(1u << bit_idx);
}

bool BasicBloomFilter::getBit(std::size_t idx) const {
    const std::size_t byte_idx = idx / 8;
    const std::size_t bit_idx = idx % 8;
    return ((bitvector[byte_idx] & static_cast<std::uint8_t>(1u << bit_idx)) != 0);
}

std::size_t BasicBloomFilter::generate_bit(std::uint64_t hash, std::size_t i) const {
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

void BasicBloomFilter::insertBatchImpl(const std::uint64_t* hash_array, std::size_t count) {
    for (std::size_t i = 0; i < count; i++) {
        insertImpl(hash_array[i]);
    }
}

void BasicBloomFilter::containsBatchImpl(const std::uint64_t* hash_array, std::uint8_t* result, std::size_t count) const {
    std::fill(result, result + ((count + 7) / 8), 0);
    for (std::size_t i = 0; i < count; i++) {
        if (containsImpl(hash_array[i])) {
            result[i / 8] |= static_cast<std::uint8_t>(1u << (i % 8));
        }
    }
}