#pragma once

#include "BloomFilter.h"
#include "util/HelperFuncs.h"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstddef>

// original reference: https://github.com/apache/arrow/blob/main/cpp/src/arrow/acero/bloom_filter.h

// structure for the mask lookup
// precompute a collection of small Bloom filter bit masks
//      and let the Bloom filter quickly retrieve one mask
// done in order to check for membership quickly
struct BloomFilterMasks {
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
    static constexpr uint64_t fullMask = (1ULL << bitsPerMask) - 1;
    // number of bytes needed for the mask
    //      since blocks are moved to the right
    //      and there is an overlap between the masks depending on offset
    //      so we have # of masks + 64 (since we load 64 bits) / 8 (to get bytes)
    // TODO: possible to fine-tune this
    // sets the maximum size of the lookup table used for the mask
    // here it is set to 2^10 by default
    static constexpr int logNumberMasks = 10;
    static constexpr int numberMasks = 1 << logNumberMasks;
    // total number of bytes needed for all masks
    static constexpr int totalBytes = (numberMasks + 64) / 8;
    uint8_t masks[totalBytes];
    BloomFilterMasks();
    // extract a 57-bit mask starting at an arbitrary bit position
    //      the mask represents the # of bits that should be checked
    //      to verify membership
    // e.g. if bit_offset is 13, then bit_offset % 8 = 5
    //      so we shift by 5 bits and then keep the next 57 bits
    inline uint64_t mask(int bit_offset) const {
        // assume little endian
        // find the byte that contains the starting bit to be extracted
        const uint8_t* start_byte = masks + bit_offset / 8;
        // load 64 bits
        uint64_t loaded_bits = 0;
        std::memcpy(&loaded_bits, start_byte, sizeof(loaded_bits));
        // shift to get the lowest 57 bits after the offset
        int shift = bit_offset % 8;
        return (loaded_bits >> shift) & fullMask;
    }
};

class ArrowBloomFilter : public BloomFilter<ArrowBloomFilter> {
    friend class BloomFilter<ArrowBloomFilter>;
    public:
        ArrowBloomFilter(int64_t rows_to_insert) {
            // number of allocated filter bits per inserted key
            constexpr int64_t bitsPerKey = 8;
            // minimum allocated number of bits for the bloom filter
            //      so our filter does not become too small (high FP rate)
            constexpr  int64_t min_num_bits = 512;
            // since we can heuristically achieve a certain number of FP rate
            // TODO: change this part to use the formula
            //      common bloom filter sizing formula:
            //      m = -(n * ln(p)) / ln(2) ^ 2)
            //      n = expected number of items
            //      p = desired FP rate
            //      m = number of bits (max(512, rows_to_insert * bitsPerKey))
            int64_t desired_num_bits = std::max(min_num_bits, rows_to_insert * bitsPerKey);
            int log_num_bits = ceil_log2(desired_num_bits);
            // convert bloom filter size from number of bits
            //      into number of 64-bit blocks
            //      2^6 = 64
            log_num_blocks = log_num_bits - 6;
            num_blocks = 1ULL << log_num_blocks;
            // allocate and zero out the bitvector
            bitvector.assign(static_cast<std::size_t>(num_blocks), 0);
        }
    private:
        // physical division of hash value bits
        //      [...][block id][6 bits - rotation][10 bits - mask id]
        // physical structures
        static BloomFilterMasks masks;
        // bit table stored as vector of uint64 blocks
        //      blocks = raw pointer to the filter's bit storage
        //      each 64-bit word is one block
        std::vector<std::uint64_t> bitvector;
        // must be power of 2
        int log_num_blocks;
        int64_t num_blocks;

        // helper function to create mask
        //      turn hash value into 64-bit mask
        //      used to set or check several bits at once in one block
        inline uint64_t mask(uint64_t hash) const {
            // use the lowest 10 bits of the hash to choose the mask
            int mask_id = static_cast<int>(hash & (BloomFilterMasks::numberMasks - 1));
            uint64_t result = masks.mask(mask_id);
            // extract 6 bits (max value 63) to use as rotation amount
            //      shifted first by logNumberMasks
            //      since the first 10 bits are already used for initial mask
            //      after shifting, pick the lowest 6 bits
            int num_rotation = (hash >> BloomFilterMasks::logNumberMasks) & 63;
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
        inline int64_t block_id(uint64_t hash) const {
            // skip the first 16 bits of the hash
            //      since they were already used to pick mask id and for rotation
            //      use the next num_blocks amount of bits to pick the corresponding block
            // num_blocks - 1 only works if it is a power of two
            return (hash >> (BloomFilterMasks::logNumberMasks + 6)) & (num_blocks - 1);
        }

        // insert new members into the bloom filter
        void insertImpl(uint64_t hash) {
            uint64_t m = mask(hash);
            uint64_t& b = bitvector[block_id(hash)];
            b = b | m;
        }
        // TODO: SIMD implementation
        // void insert();

        // membership check
        inline bool containsImpl(uint64_t hash) const {
            uint64_t m = mask(hash);
            uint64_t b = bitvector[block_id(hash)];
            return (b & m) == m;
        }
        // TODO: SIMD implementation
        // void contains() const;

        // function to shrink the bloom filter
        //      can be used if there are too many bits allocated for the bloom filter
        //      Folding simply means the upper and lower part goes through
        //      a logical OR, which shrinks the filter size into half,
        //      but this is a tradeoff since this means we have more false positives
        // void fold();
};