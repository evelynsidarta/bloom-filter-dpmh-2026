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

#ifdef __AVX2__
// avx2 mask generation
inline __m256i ArrowBloomFilter::mask_avx2(__m256i hash) const {
    const __m256i full_mask = _mm256_set1_epi64x(static_cast<long long>(BloomFilterMasks::fullMask));
    // mask_id = hash & (numberMasks - 1)
    const __m256i mask_id = _mm256_and_si256(hash, _mm256_set1_epi64x(BloomFilterMasks::numberMasks - 1));
    // for the byte offset
    __m256i mask_byte_index = _mm256_srli_epi64(mask_id, 3);
    // bit_shift = bit_offset % 8;
    __m256i bit_shift = _mm256_and_si256(mask_id, _mm256_set1_epi64x(7));
    __m256i loaded_bits = _mm256_i64gather_epi64(reinterpret_cast<const long long*>(masks.masks), mask_byte_index, 1);
    __m256i result = _mm256_srlv_epi64(loaded_bits, bit_shift);
    result = _mm256_and_si256(result, full_mask);
    // rotation
    __m256i rotation = _mm256_srli_epi64(hash, BloomFilterMasks::logNumberMasks);
    rotation = _mm256_and_si256(rotation, _mm256_set1_epi64x(63));
    // rotate left function on avx2
    result = _mm256_or_si256(
        _mm256_sllv_epi64(result, rotation),
        _mm256_srlv_epi64(result, _mm256_sub_epi64(_mm256_set1_epi64x(64), rotation))
    );
    return result;
}

inline __m256i ArrowBloomFilter::block_id_avx2(__m256i hash) const {
    __m256i result = _mm256_srli_epi64(hash, BloomFilterMasks::logNumberMasks + 6);
    result = _mm256_and_si256(result, _mm256_set1_epi64x(num_blocks - 1));
    return result;
}

// batch insert implementation
std::size_t ArrowBloomFilter::insertBatchImpl_avx2(
    const std::uint64_t* hash_array, std::size_t count) {
    constexpr int unroll = 4;
    int64_t loop_count = count - (count % unroll);
    for (int64_t i = 0; i < loop_count; i = i + unroll) {
        __m256i hash = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(hash_array + i));
        __m256i mask = mask_avx2(hash);
        __m256i block_id = block_id_avx2(hash);
        bitvector[_mm256_extract_epi64(block_id, 0)] |= _mm256_extract_epi64(mask, 0);
        bitvector[_mm256_extract_epi64(block_id, 1)] |= _mm256_extract_epi64(mask, 1);
        bitvector[_mm256_extract_epi64(block_id, 2)] |= _mm256_extract_epi64(mask, 2);
        bitvector[_mm256_extract_epi64(block_id, 3)] |= _mm256_extract_epi64(mask, 3);   
    }
    return static_cast<std::size_t>(loop_count);
}

// batch contains implementation
std::size_t ArrowBloomFilter::containsBatchImpl_avx2(
    const std::uint64_t* hash_array, uint8_t* result, std::size_t count
) const {
    constexpr int unroll = 8;
    constexpr int unroll_div2 = unroll / 2;
    const long long* blocks = reinterpret_cast<const long long*>(bitvector.data());
    int64_t loop_count = count - (count % unroll);
    for (int64_t i = 0; i < loop_count; i = i + unroll) {
        __m256i hash_A = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(hash_array + i));
        __m256i hash_B = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(hash_array + i + unroll_div2));
        __m256i mask_A = mask_avx2(hash_A);
        __m256i mask_B = mask_avx2(hash_B);
        __m256i block_id_A = block_id_avx2(hash_A);
        __m256i block_id_B = block_id_avx2(hash_B);
        __m256i block_A = _mm256_i64gather_epi64(blocks, block_id_A, sizeof(std::uint64_t));
        __m256i block_B = _mm256_i64gather_epi64(blocks, block_id_B, sizeof(std::uint64_t));
        __m256i result_A = _mm256_and_si256(block_A, mask_A);
        __m256i result_B = _mm256_and_si256(block_B, mask_B);
        result_A = _mm256_cmpeq_epi64(result_A, mask_A);
        result_B = _mm256_cmpeq_epi64(result_B, mask_B);
        int lanes_A = _mm256_movemask_pd(_mm256_castsi256_pd(result_A));
        int lanes_B = _mm256_movemask_pd(_mm256_castsi256_pd(result_B));
        std::uint8_t packed = static_cast<std::uint8_t>((lanes_A & 0xF) | ((lanes_B & 0xF) << 4));
        // result is a packed bit vector
        result[i / 8] = packed;
    }
    return loop_count;
}
#endif

void ArrowBloomFilter::insertBatchImpl(const std::uint64_t* hash_array, std::size_t count) {
    std::size_t processed = 0;
#ifdef __AVX2__
    processed = insertBatchImpl_avx2(hash_array, count);
#endif
    // scalar tail
    for (std::size_t i = processed; i < count; i++) {
        insertImpl(hash_array[i]);
    }
}

void ArrowBloomFilter::containsBatchImpl(const std::uint64_t* hash_array, uint8_t* result, std::size_t count) const {
    std::size_t processed = 0;
    // clear result buffer
    std::fill(result, result + ((count + 7) / 8), 0);
#ifdef __AVX2__
    processed = containsBatchImpl_avx2(hash_array, result, count);
#endif
    for (std::size_t i = processed; i < count; i++) {
        if (containsImpl(hash_array[i])) {
            result[i / 8] |= static_cast<std::uint8_t>(1u << (i % 8));
        }
    }
}