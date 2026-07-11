#pragma once

#include "BloomFilter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

class BlockedBloomFilter : public BloomFilter<BlockedBloomFilter> {
    friend class BloomFilter<BlockedBloomFilter>;
    public:
        BlockedBloomFilter(std::int64_t rows_to_insert);
    private:
        static constexpr int bits_per_block = 64;
        static constexpr int log_bits_block = 6;
        // bits allocated per key
        static constexpr int bitsPerKey = 8;
        // number of hash positions
        static constexpr int num_hashes = 4;
        // minimum size for bloom filter
        static constexpr std::int64_t min_num_bits = 512;
        // for the bloom filter itself
        std::vector<std::uint64_t> bitvector;
        int log_num_blocks;
        std::uint64_t num_blocks;

        std::uint64_t mask(std::uint64_t hash) const;
        std::size_t block_id(std::uint64_t hash) const;

        void insertImpl(std::uint64_t hash);
        bool containsImpl(std::uint64_t hash) const;
        void insertBatchImpl(const std::uint64_t* hash_array, std::size_t count);
        void containsBatchImpl(const std::uint64_t* hash_array,
                                    std::uint8_t* result,
                                    std::size_t count) const;
};