#include "ModifiedArrowBloomFilter.h"

#include <unordered_set>
#include <random>

// generate look up table for the bloom filter masks
//      masks are generated in a sliding-window fashion
// numberMasks = 1024 by default
ModifiedBloomFilterMasks::ModifiedBloomFilterMasks() {
    // seed to generate masks
    std::seed_seq seed{0, 0, 0, 0, 0, 0, 0, 0};
    std::mt19937 rng(seed);
    // prevent the lookup table from containing the same mask
    std::unordered_set<std::uint64_t> used;
    used.reserve(numberMasks * 2);
    // generate masks for the table
    std::size_t table_index = 0;
    while (table_index < numberMasks) {
        std::uint64_t value = 0;
        unsigned bits_set = 0;
        // generate x bits per mask according to the desired fp rate
        while (bits_set < bitsPerMask) {
            // use last 6 bits of random number to generate a value
            //      since we have 64-bit blocks
            const unsigned bit = static_cast<unsigned>(rng() & 63);
            const std::uint64_t bit_mask = 1ULL << bit;
            // ensure x distinct bits are set within the mask
            if ((value & bit_mask) == 0) {
                value |= bit_mask;
                bits_set = bits_set + 1;
            }
        }
        // avoid duplicate masks
        if (used.insert(value).second) {
            masks[table_index++] = value;
        }
    }
}

ModifiedBloomFilterMasks ModifiedArrowBloomFilter::masks;

#ifdef __AVX2__
// avx2 mask generation
//      generate 4 masks in parallel
inline __m256i ModifiedArrowBloomFilter::mask_avx2(__m256i hash) const {
    // extract index from the masks
    //      2048 entries in the lookup table -> take lowest 11 bits
    const __m256i indices = _mm256_and_si256(hash, _mm256_set1_epi64x(static_cast<long long>(ModifiedBloomFilterMasks::indexMask)));
    // use lookup table
    return _mm256_i64gather_epi64(reinterpret_cast<const long long*>(masks.masks.data()), indices,
                                    static_cast<int>(sizeof(std::uint64_t)));
}

inline __m256i ModifiedArrowBloomFilter::block_id_avx2(__m256i hash) const {
    return _mm256_srlv_epi64(hash, _mm256_set1_epi64x(static_cast<long long>(block_shift)));
}

// batch contains implementation
//      pretty much the same as arrow's but with m & ~b == 0 instead
std::size_t ModifiedArrowBloomFilter::containsBatchImpl_avx2(
    const std::uint64_t* hash_array, std::uint8_t* result, std::size_t count) const {
    constexpr int unroll = 8;
    constexpr int unroll_div2 = unroll >> 1;
    const long long* blocks = reinterpret_cast<const long long*>(bitvector.data());
    const __m256i zero = _mm256_setzero_si256();
    std::size_t loop_count = count - (count % unroll);
    for (std::size_t i = 0; i < loop_count; i = i + unroll) {
        __m256i hash_A = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(hash_array + i));
        __m256i hash_B = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(hash_array + i + unroll_div2));
        __m256i mask_A = mask_avx2(hash_A);
        __m256i mask_B = mask_avx2(hash_B);
        __m256i block_id_A = block_id_avx2(hash_A);
        __m256i block_id_B = block_id_avx2(hash_B);
        __m256i block_A = _mm256_i64gather_epi64(blocks, block_id_A, sizeof(std::uint64_t));
        __m256i block_B = _mm256_i64gather_epi64(blocks, block_id_B, sizeof(std::uint64_t));
        // m & ~b == 0
        __m256i missing_A = _mm256_andnot_si256(block_A, mask_A);
        __m256i missing_B = _mm256_andnot_si256(block_B, mask_B);
        __m256i match_A = _mm256_cmpeq_epi64(missing_A, zero);
        __m256i match_B = _mm256_cmpeq_epi64(missing_B, zero);
        int lanes_A = _mm256_movemask_pd(_mm256_castsi256_pd(match_A));
        int lanes_B = _mm256_movemask_pd(_mm256_castsi256_pd(match_B));
        std::uint8_t packed = static_cast<std::uint8_t>((lanes_A & 0xF) | ((lanes_B & 0xF) << 4));
        // result is a packed bit vector
        result[i >> 3] = packed;
    }
    return loop_count;
}
#endif

void ModifiedArrowBloomFilter::insertBatchImpl(const std::uint64_t* hash_array, std::size_t count) {
    for (std::size_t i = 0; i < count; i++) {
        insertImpl(hash_array[i]);
    }
}

void ModifiedArrowBloomFilter::containsBatchImpl(const std::uint64_t* hash_array, std::uint8_t* result, std::size_t count) const {
    std::size_t processed = 0;
#ifdef __AVX2__
    if (impl_mode == ImplMode::avx2) {
        processed = containsBatchImpl_avx2(hash_array, result, count);
        if (processed == count) {
            return;
        }
        const std::size_t output_byte = processed >> 3;
        std::uint8_t packed = 0;
        // tail
        for (std::size_t i = processed; i < count; i++) {
            packed |= static_cast<std::uint8_t>(containsImpl(hash_array[i])) << (i - processed);
        }
        result[output_byte] = packed;
        return;
    }
#endif
    // scalar implementation
    const std::size_t output_bytes = (count + 7) >> 3;
    for (std::size_t byte = 0; byte < output_bytes; byte++) {
        const std::size_t begin = byte << 3;
        const std::size_t end = std::min(begin + 8, count);
        std::uint8_t packed = 0;
        for (std::size_t i = begin; i < end; i++) {
            packed |= static_cast<std::uint8_t>(containsImpl(hash_array[i])) << (i - begin);
        }
        result[byte] = packed;
    }
}