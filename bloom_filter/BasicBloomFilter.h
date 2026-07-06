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

        void insertImpl(uint64_t hash);
        bool containsImpl(uint64_t hash) const;

        // setting and getting the bits in the array
        void setBit(std::size_t idx);
        bool getBit(std::size_t idx) const;

        // to simulate having different hash functions, we use the input hash
        //      and then "hash" it even more to derive multiple bit indexes out of it
        //      to turn it into a "fairer" comparison vs. arrow's 1 hash implementation
        std::size_t generate_bit(std::uint64_t hassh, std::size_t i) const;

        // physical structure
        std::vector<std::uint8_t> bitvector;
        // number of bits that need to be set at a minimum (and maximum)
        std::size_t num_hashes;
        std::size_t num_bits;
};