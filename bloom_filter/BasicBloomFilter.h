#pragma once

#include "BloomFilter.h"

#include <vector>
#include <cstdint>
#include <cstddef>

class BasicBloomFilter : public BloomFilter<BasicBloomFilter> {
    public:
        BasicBloomFilter(std::size_t num_bits = 1024, std::size_t num_hashes = 4);
    private:
        friend class BloomFilter<BasicBloomFilter>;

        void insertImpl(std::uint64_t hash);
        bool containsImpl(std::uint64_t hash) const;
        void insertBatchImpl(const std::uint64_t* hash_array, std::size_t count);
        void containsBatchImpl(const std::uint64_t* hash_array, std::uint8_t* result, std::size_t count) const;

        // setting and getting the bits in the array
        void setBit(std::size_t idx);
        bool getBit(std::size_t idx) const;

        // physical structure
        std::vector<std::uint8_t> bitvector;
        // number of bits that need to be set at a minimum (and maximum)
        std::size_t num_hashes;
        std::size_t num_bits;
};