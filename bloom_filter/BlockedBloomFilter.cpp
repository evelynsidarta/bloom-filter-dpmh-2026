#include "BlockedBloomFilter.h"
#include "util/HelperFuncs.h"

BlockedBloomFilter::BlockedBloomFilter(std::int64_t rows_to_insert) {
    // estimate total number of filter bits
    //      rows_to_insert * bitsPerKey (unless it's smaller than min size of bloom filter)
    const std::int64_t desired_num_bits = std::max(min_num_bits, rows_to_insert * bitsPerKey);
    // round up to next power of two
    int log_num_bits = ceil_log2(desired_num_bits);
    // convert into number of blocks, given bits in each block
    log_num_blocks = log_num_bits - log_bits_block;
    num_blocks = 1ULL << log_num_blocks;
    // allocate and zero out the bitvector
    bitvector.assign(static_cast<std::size_t>(num_blocks), 0);
}

// turn input hash value into bit mask
std::uint64_t BlockedBloomFilter::mask(std::uint64_t hash) const {
    // generate num_hashes bit positions in the selected block.
    uint64_t x = hash;
    std::uint64_t result = 0;
    for (int i = 0; i < num_hashes; i++) {
        // to avoid setting the same bit results, use while(true) loop
        while (true) {
            // choose some bit position from 0 to 63
            //      take the lowest 6 digits
            x = murmurHash(x + static_cast<std::uint64_t>(i));
            int bit_pos = static_cast<int>(x & (bits_per_block - 1));
            // use the bit_pos to set the determined bit to 1
            uint64_t bit = 1ULL << bit_pos;
            if ((result & bit) == 0) {
                result = result | bit;
                break;
            }
        }
    }
    return result;
}

std::size_t BlockedBloomFilter::block_id(std::uint64_t hash) const {
    // each block is uint64_t
    //      take the top log_num_blocks bits of the hash as block_id
    return static_cast<std::size_t>(hash >> (64 - log_num_blocks));
}

void BlockedBloomFilter::insertImpl(std::uint64_t hash) {
    const std::uint64_t m = mask(hash);
    bitvector[block_id(hash)] |= m;
}

bool BlockedBloomFilter::containsImpl(std::uint64_t hash) const {
    const std::uint64_t m = mask(hash);
    const std::uint64_t block = bitvector[block_id(hash)];
    return (block & m) == m;
}

void BlockedBloomFilter::insertBatchImpl(const std::uint64_t* hash_array, std::size_t count) {
    for (std::size_t i = 0; i < count; i++) {
        insertImpl(hash_array[i]);
    }
}

void BlockedBloomFilter::containsBatchImpl(const std::uint64_t* hash_array,
                                                std::uint8_t* result,
                                                std::size_t count) const {
    const std::size_t result_bytes = (count + 7) / 8;
    std::fill(result, result + result_bytes, 0);
    for (std::size_t i = 0; i < count; i++) {
        if (containsImpl(hash_array[i])) {
            result[i / 8] |= static_cast<std::uint8_t>(1u << (i % 8));
        }
    }
}