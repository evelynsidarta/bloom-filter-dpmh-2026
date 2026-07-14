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

// original reference: https://github.com/apache/arrow/blob/main/cpp/src/arrow/acero/bloom_filter.h

// structure for the mask lookup
// precompute a collection of small Bloom filter bit masks
//      and let the Bloom filter quickly retrieve one mask
// done in order to check for membership quickly
struct alignas(64) ImprovedBloomFilterMasks {
    // a single 64-bit load gives 64 bits
    //      at worst, we shift to the right 7 times: 64 bits - 7 bits = 57 bits
    //      57 is the largest mask size that can always be extracted
    static constexpr int bitsPerMask = 57;
    // number of bits that need to be set at a minimum (and maximum)
    //      for each sliding window frame
    static constexpr int minBitsSet = 4;
    static constexpr int maxBitsSet = 5;
    // mask that selects the lowest 57 bits (fill with 1)
    //      rest of the bits outside of the needed 57 are 0
    static constexpr std::uint64_t fullMask = (1ULL << bitsPerMask) - 1;
    // number of bytes needed for the mask
    //      since blocks are moved to the right
    //      and there is an overlap between the masks depending on offset
    //      so we have # of masks + 64 (since we load 64 bits) / 8 (to get bytes)
    // TODO: possible to fine-tune this
    // sets the maximum size of the lookup table used for the mask
    // here it is set to 2^10 by default
    static constexpr int logNumberMasks = 10;
    static constexpr int numberMasks = 1 << logNumberMasks;
    static constexpr int maskIndex = numberMasks - 1;
    // total number of bytes needed for all masks
    // CHANGED: shift instead of / 8
    static constexpr int totalBytes = (numberMasks + 64) >> 3;
    std::uint8_t masks[totalBytes];
    ImprovedBloomFilterMasks();
    // extract a 57-bit mask starting at an arbitrary bit position
    //      the mask represents the # of bits that should be checked
    //      to verify membership
    // e.g. if bit_offset is 13, then bit_offset % 8 = 5
    //      so we shift by 5 bits and then keep the next 57 bits
    inline std::uint64_t mask(int bit_offset) const {
        // assume little endian
        // find the byte that contains the starting bit to be extracted
        // CHANGED: shift instead of div
        const std::uint8_t* start_byte = masks + (bit_offset >> 3u);
        // load 64 bits
        std::uint64_t loaded_bits = 0;
        std::memcpy(&loaded_bits, start_byte, sizeof(loaded_bits));
        // shift to get the lowest 57 bits after the offset
        // CHANGED: and instead of %
        int shift = bit_offset & 7;
        return (loaded_bits >> shift) & fullMask;
    }
};

class ImprovedArrowBloomFilter : public BloomFilter<ImprovedArrowBloomFilter> {
    friend class BloomFilter<ImprovedArrowBloomFilter>;
    public:
        enum class ImplMode {
            scalar, avx2
        };

        ImprovedArrowBloomFilter(std::size_t rows_to_insert, ImplMode mode = ImplMode::scalar) : impl_mode(mode) {
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
            // since we can heuristically achieve a certain number of FP rate
            // TODO: change this part to use the formula
            //      common bloom filter sizing formula:
            //      m = -(n * ln(p)) / ln(2) ^ 2)
            //      n = expected number of items
            //      p = desired FP rate
            //      m = number of bits (max(512, rows_to_insert * bitsPerKey))
            const std::size_t desired_num_bits = std::max(min_num_bits, rows_to_insert * bitsPerKey);
            const int log_num_bits = ceil_log2(desired_num_bits);
            // convert bloom filter size from number of bits
            //      into number of 64-bit blocks
            //      2^6 = 64
            log_num_blocks = log_num_bits - 6;
            num_blocks = 1ULL << log_num_blocks;
            block_shift = 64 - log_num_blocks;
            // allocate and zero out the bitvector
            bitvector.assign(static_cast<std::size_t>(num_blocks), 0);
        }
    private:
        // physical division of hash value bits
        //      [block id][...][6 bits - rotation][10 bits - mask id]
        // physical structures
        static ImprovedBloomFilterMasks masks;
        // bit table stored as vector of uint64 blocks
        //      blocks = raw pointer to the filter's bit storage
        //      each 64-bit word is one block
        std::vector<std::uint64_t> bitvector;
        // must be power of 2
        int log_num_blocks;
        std::size_t num_blocks;
        ImplMode impl_mode;
        int block_shift;

        // helper function to create mask
        //      turn hash value into 64-bit mask
        //      used to set or check several bits at once in one block
        inline std::uint64_t mask(std::uint64_t hash) const {
            // use the lowest 10 bits of the hash to choose the mask
            int mask_id = static_cast<int>(hash & (ImprovedBloomFilterMasks::maskIndex));
            std::uint64_t result = masks.mask(mask_id);
            // extract 6 bits (max value 63) to use as rotation amount
            //      shifted first by logNumberMasks
            //      since the first 10 bits are already used for initial mask
            //      after shifting, pick the lowest 6 bits
            int num_rotation = (hash >> ImprovedBloomFilterMasks::logNumberMasks) & 63;
            // rotate the mask by a certain amount
            //      probably a way to reduce the amount of collisions
            //      without storing more mask
            //      instead of having 2^10 possible masks,
            //      now we have 2^10 * 64 possible rotations
            result = rotate_left(result, num_rotation);
            return result;
        }

        // helper function to pick which 64-bit block of the bloom filter
        //      should be used for the inserted values
        // blocked bloom filter = only retrieve one block from memory at a time
        //      so that lookup is cheap
        inline std::size_t block_id(std::uint64_t hash) const {
            // CHANGED: instead of taking the next lowest num_blocks bits,
            //      just take the top num_blocks bits
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
            // CHANGED: some compilers will do andn instead of 2 instructions
            //      since andn already sets the 0-flag if it's equal to 0
            return (m & ~b) == 0;
        }

        // batch functions
        void insertBatchImpl(const std::uint64_t* hash_array, std::size_t count);
        void containsBatchImpl(const std::uint64_t* hash_array, std::uint8_t* result, std::size_t count) const;

        // helper function for avx2 implementations
#ifdef __AVX2__
        inline __m256i mask_avx2(__m256i hash) const;
        inline __m256i block_id_avx2(__m256i hash) const;
        std::size_t insertBatchImpl_avx2(const std::uint64_t* hash_array, std::size_t count);
        std::size_t containsBatchImpl_avx2(const std::uint64_t* hash_array, std::uint8_t* result, std::size_t count) const;
#endif
};