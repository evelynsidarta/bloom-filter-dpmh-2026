#include "ArrowBloomFilter.h"

#include <random>

// generate look up table for the bloom filter masks
//      masks are generated in a sliding-window fashion
// numberMasks = 1024 by default
BloomFilterMasks::BloomFilterMasks() {
    std::seed_seq seed{0, 0, 0, 0, 0, 0, 0, 0};
    std::mt19937 rng(seed);
    auto random = [&rng](int min_value, int max_value) {
        std::uniform_int_distribution<int> dist(min_value, max_value);
        return dist(rng);
    };
    // clear all bits in the mask table
    std::memset(masks, 0, totalBytes);
    // prepare the first mask
    int num_bits_set = random(minBitsSet, maxBitsSet);
    for (int i = 0; i < num_bits_set; i++) {
        while (true) {
            int bit_pos = random(0, bitsPerMask - 1);
            // if the bit is not set yet, set the bit
            //      and get out of the while(true) loop
            if (!getBit(masks, bit_pos)) {
                setBit(masks, bit_pos);
                break;
            }    
        }
    }
    // total of bits that we need to set is equal
    //      numberMasks + bits_needed_for_mask - 1
    int64_t num_bits_total = numberMasks + bitsPerMask - 1;
    // generate the remaining masks after first one
    // start at bitsPerMask, since our first #bitsPerMask number of bits
    //      are already generated before
    for (int64_t i = bitsPerMask; i < num_bits_total; i++) {
        // take the value of the bit that is being left out by the sliding window
        //      e.g. for the first loop, digit 0 is not in the second
        //           sliding window anymore, so bit_leaving = 0;
        int bit_leaving = getBit(masks, i - bitsPerMask) ? 1 : 0;
        // if the leaving bit is set, then the next bit need to be 1
        //      since the mask requires us to set a minimum of x amount of bits
        if (bit_leaving == 1 && num_bits_set == minBitsSet) {
            setBit(masks, i);
            continue;
        }
        if (bit_leaving == 0 && num_bits_set == maxBitsSet) {
            //      In that case, there is a bit of randomness that can be added
            // next bit has to be 0 since we only want to have a certain amount
            //      of set bits per window
            continue;
        }
        // there is a certain chance that the new bit is set to 1
        //      bitsPerMask = 57
        //      minBitsSet = 4, maxBitsSet = 5
        //      random(0, 113) < 9 -> ~7.9% chance to set the bit to 1
        if (random(0, bitsPerMask * 2 - 1) < minBitsSet + maxBitsSet) {
            setBit(masks, i);
            // if bit_leaving == 1 then count stays the same
            if (bit_leaving == 0) {
                num_bits_set = num_bits_set + 1;
            }
        } else {
            // if we do not set new bit,
            //      then we have one less set bit for now
            if (bit_leaving == 1) {
                num_bits_set = num_bits_set - 1;
            }
        }
    } 
};

BloomFilterMasks ArrowBloomFilter::masks;