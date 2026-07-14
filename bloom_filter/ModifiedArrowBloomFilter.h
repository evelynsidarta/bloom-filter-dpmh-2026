#pragma once

#include "BloomFilter.h"
#include "util/HelperFuncs.h"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <immintrin.h>
#include <stdexcept>
#include <array>

// precompute a collection of small Bloom filter bit masks
//      and let the Bloom filter quickly retrieve one mask
// done in order to check for membership quickly
struct ModifiedBloomFilterMasks {
    // set bits per mask
    static constexpr int bitsPerMask = 5;
    // determine table size for the precomputed masks
    //      Here: 2 ^ 11 = 2048
    static constexpr int logNumberMasks = 11;
    static constexpr int numberMasks = 1ULL << logNumberMasks;
    static constexpr std::uint64_t indexMask = numberMasks - 1;
    // lookup table physical structure
    alignas(64) std::array<std::uint64_t, numberMasks> masks{};
    ModifiedBloomFilterMasks();

    // use lowest 11 bits for the mask lookup
    inline std::uint64_t mask(std::uint64_t hash) const {
        return masks[static_cast<std::size_t>(hash & indexMask)];
    }
};

class ModifiedArrowBloomFilter : public BloomFilter<ModifiedArrowBloomFilter> {
    friend class BloomFilter<ModifiedArrowBloomFilter>;
    public:
        enum class ImplMode {
            scalar, avx2
        };

        ModifiedArrowBloomFilter(std::size_t rows_to_insert, ImplMode mode = ImplMode::scalar) : impl_mode(mode) {
#ifndef __AVX2__
            if (impl_mode == ImplMode::avx2) {
                throw std::invalid_argument("avx2 mode not available.");
            }
#endif
            // number of allocated filter bits per inserted key
            constexpr std::size_t bitsPerKey = 8;
            // minimum allocated number of bits for the bloom filter
            //      so our filter does not become too small (high FP rate)
            constexpr std::size_t min_num_bits = 512;
            const std::size_t desired_num_bits = std::max(min_num_bits, rows_to_insert * bitsPerKey);
            // round up to nearest 2 multiple
            const int log_num_bits = ceil_log2(desired_num_bits);
            // convert bloom filter size from number of bits
            //      into number of 64-bit blocks
            //      2^6 = 64
            log_num_blocks = log_num_bits - 6;
            num_blocks = 1ULL << log_num_blocks;
            block_shift = 64 - log_num_blocks;
            // allocate and zero out the bitvector
            bitvector.assign(num_blocks, 0);
        }
    private:
        static ModifiedBloomFilterMasks masks;
        std::vector<std::uint64_t> bitvector;
        // must be power of 2
        int log_num_blocks;
        int block_shift;
        std::size_t num_blocks;
        ImplMode impl_mode;

        inline std::uint64_t mask(std::uint64_t hash) const {
           return masks.mask(hash);
        }

        // helper function to pick which 64-bit block of the bloom filter
        //      should be used for the inserted values
        // blocked bloom filter = only retrieve one block from memory at a time
        //      so that lookup is cheap
        inline std::size_t block_id(std::uint64_t hash) const {
            return (hash >> block_shift);
        }

        // insert new members into the bloom filter
        inline void insertImpl(std::uint64_t hash) {
            bitvector[block_id(hash)] |= mask(hash);
        }

        // membership check
        inline bool containsImpl(uint64_t hash) const {
            std::uint64_t m = mask(hash);
            std::uint64_t b = bitvector[block_id(hash)];
            // in some compilers might be cheaper
            //      because we can use andn instead.
            //      andn already sets the 0-flag if it's equal to 0
            //      so it is 1 instruction instead of 2
            return (m & ~b) == 0;
        }

        // batch functions
        void insertBatchImpl(const std::uint64_t* hash_array, std::size_t count);
        void containsBatchImpl(const std::uint64_t* hash_array, std::uint8_t* result, std::size_t count) const;
        
        // helper function for avx2 implementations
#ifdef __AVX2__
        inline __m256i mask_avx2(__m256i hash) const;
        inline __m256i block_id_avx2(__m256i hash) const;
        std::size_t containsBatchImpl_avx2(const std::uint64_t* hash_array, std::uint8_t* result, std::size_t count) const;
#endif
};