#pragma once

#include <cstdint>

// class implementation
template <typename BloomFilterImpl>
class BloomFilter {
    public:
        // accept alredy hashed value to insert
        // since we just want to use the same hash
        void insert(uint64_t hash) {
            this->impl().insertImpl(hash);
        }
        // lookup function
        bool contains(uint64_t hash) const {
            return this->impl().containsImpl(hash);
        }
    private:
        // to get correct BloomFilter implementation
        // for non intrusive functions (insert, clear)
        BloomFilterImpl& impl() {
            return static_cast<BloomFilterImpl&>(*this);
        }
        // for contains
        const BloomFilterImpl& impl() const {
            return static_cast<const BloomFilterImpl&>(*this);
        }
};