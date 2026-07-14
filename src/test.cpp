#include "bloom_filter/BasicBloomFilter.h"
#include "bloom_filter/BlockedBloomFilter.h"
#include "bloom_filter/ArrowBloomFilter.h"
#include "bloom_filter/ModifiedArrowBloomFilter.h"
// #include "bloom_filter/ModifiedBlock16Filter.h"
#include "bloom_filter/ImprovedArrowFilter.h"
#include "util/HelperFuncs.h"

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <unordered_set>
#include <utility>

static void check_filter(bool condition, 
                            const std::string& msg,
                            const char* file,
                            int line) {
    if (!condition) {
        std::ostringstream oss;
        oss << "TEST failed at " << file << ":" << line << ": " << msg;
        throw std::runtime_error(oss.str());
    }
}

// check creating empty bloom filter
//      ensure it doesn't return true for anything
//      since it's supposed to be empty
template <typename Filter>
static void UT_createEmpty(const Filter& filter,
                            const std::vector<std::uint64_t>& queries,
                            const std::string& name) {
    for (std::uint64_t h : queries) {
        check_filter(!filter.contains(h), name + ": empty filter returns true for some values",
                        __FILE__, __LINE__);
    }
}

// check no false negatives
//      name = implementation name for the bloom filter
template <typename Filter>
static void UT_scalarInsert(Filter& filter,
                                    const std::vector<std::uint64_t>& inserted,
                                    const std::string& name) {
    // insert values into the bloom filter
    for (std::uint64_t h : inserted) {
        filter.insert(h);
    }
    // afterwards check if all of them does not return false when probed
    for (std::uint64_t h : inserted) {
        check_filter(filter.contains(h), (name + " returns false negative after scalar insertion."),
                        __FILE__, __LINE__);
    }
    // try inserting duplicate values for every inserted value
    for (std::uint64_t h : inserted) {
        filter.insert(h);
    }
    // check again if inserting duplicates screwed up the bloom filters somehow
    for (std::uint64_t h : inserted) {
        check_filter(filter.contains(h), (name + " returns false negative after duplicate scalar insertion."),
                        __FILE__, __LINE__);
    }    
}

// check false negatives after batch inserts
template <typename Filter>
static void UT_batchInsert(Filter& filter,
                                const std::vector<std::uint64_t>& inserted,
                                const std::string& name) {
    filter.insertBatch(inserted.data(), inserted.size());
    for (const std::uint64_t h : inserted) {
        check_filter(filter.contains(h), (name + " returns false negative after batch insertion."),
                        __FILE__, __LINE__);
    }
    filter.insertBatch(inserted.data(), inserted.size());
    for (const std::uint64_t h : inserted) {
        check_filter(filter.contains(h), (name + " returns false negative after duplicate batch insertion."),
                        __FILE__, __LINE__);
    }
}

// check if scalar and batch insert behaves the same
template<typename FilterScalar, typename FilterBatch>
static void UT_batchInsert_compareImpl(const FilterScalar& s_filter,
                                            const FilterBatch& b_filter,
                                            const std::vector<std::uint64_t>& queries,
                                            const std::string& name) {
    for (std::uint64_t h : queries) {
        check_filter(s_filter.contains(h) == b_filter.contains(h), name + " returns different results for scalar and batch insertions.",
                        __FILE__, __LINE__);
    }
}

// test batch contains
template <typename Filter>
static void UT_batchContains(const Filter& filter,
                                const std::vector<std::uint64_t>& queries,
                                const std::string& name) {
    // to test scalar tail processing in avx2 implementations
    std::vector<std::size_t> batch_sizes = {0, 1, 3, 4, 5, 7, 8, 9, 127, 128, 129, queries.size()};
    for (std::size_t i : batch_sizes) {
        std::size_t count = i;
        std::size_t max_idx = queries.size() - count;
        // ask for a certain range to be evaluated by contains function
        std::size_t q_start = (i * 7) % (max_idx + 1);
        std::size_t result_bytes = (count + 7) / 8;
        // result vector
        std::vector<std::uint8_t> result(std::max<std::size_t>(result_bytes, 1), 0);
        const std::uint64_t* batch_queries = nullptr;
        if (!queries.empty()) {
            batch_queries = queries.data() + q_start;
        }
        filter.containsBatch(batch_queries, result.data(), count);
        // ensure that batch contain behaves exactly as scalar contains would
        for (std::size_t j = 0; j < count; j++) {
            const bool r_scalar = filter.contains(queries[q_start + j]);
            const bool r_batch = packed_result_bit(result.data(), j);
            // message for the failed test
            if (r_scalar != r_batch) {
                std::ostringstream oss;
                oss << name << " scalar/batch implementation mismatch at element " << j;
                check_filter(false, oss.str(), __FILE__, __LINE__);
            }
        }
    }
}



// check whether or not fp rate is too high
//      not_present = some values that are not present in the table
template <typename Filter>
static double UT_fpRate(const Filter& filter,
                        const std::vector<std::uint64_t>& not_present,
                        const double desired_fp,
                        const std::string& name) {
    std::size_t fp_count = 0;
    for (std::uint64_t h : not_present) {
        if (filter.contains(h)) {
            fp_count = fp_count + 1;
        }
    }
    // if fp rate too high
    double fp_rate = static_cast<double>(fp_count) / static_cast<double>(not_present.size());
    std::ostringstream oss;
    oss << name << ": false-positive rate unexpectedly high: " << fp_rate;
    check_filter((fp_rate <= desired_fp), oss.str(),
                    __FILE__, __LINE__);
    return fp_rate;
}

template <typename Filter>
static void run_allTests(const std::string& name, const Filter& filter,
                    const std::vector<std::uint64_t>& to_insert,
                    const std::vector<std::uint64_t>& not_present,
                    const std::vector<std::uint64_t>& mixed_set,
                    double desired_fp) {
    std::cout << "Testing " << name << " implementation...\n";
    auto empty_filter = filter;
    auto scalar_filter = filter;
    auto batch_filter = filter;
    UT_createEmpty(empty_filter, not_present, name);
    UT_batchContains(empty_filter, mixed_set, name);
    UT_scalarInsert(scalar_filter, to_insert, name);
    UT_batchContains(scalar_filter, mixed_set, name + " with scalar build");
    UT_batchInsert(batch_filter, to_insert, name);
    UT_batchContains(batch_filter, mixed_set, name + " with batch build");
    UT_batchInsert_compareImpl(scalar_filter, batch_filter, mixed_set, name);
    double scalar_fp = UT_fpRate(scalar_filter, not_present, desired_fp, name + " with scalar build");
    double batch_fp = UT_fpRate(batch_filter, not_present, desired_fp, name + " with batch build");
    check_filter(scalar_fp == batch_fp, name + ": scalar and batch insertions produced different FP rates somehow.",
                    __FILE__, __LINE__);
    std::cout << "Observed FP-rate: " << std::fixed << std::setprecision(6) << scalar_fp << '\n';
}

int main() {
    // TODO: possible to modify these parameters
    constexpr std::size_t rows_to_insert = 720'721;
    constexpr std::size_t num_inserts = 720'721;
    constexpr std::size_t bits_per_key = 8;
    constexpr std::size_t min_num_bits = 512;
    // for generate_unique function
    std::unordered_set<std::uint64_t> used_hashes;
    used_hashes.reserve(rows_to_insert + num_inserts);
    // generate hash values to insert and for negative check
    std::vector<std::uint64_t> to_insert = generate_unique(rows_to_insert, 0x1ULL, used_hashes);
    std::vector<std::uint64_t> not_present = generate_unique(num_inserts, 0x100000000ULL, used_hashes);
    std::vector<std::uint64_t> mixed_set = mix_vector(to_insert, not_present);
    // calculate equivalent bits for basic implementation
    std::size_t basic_num_bits = calculate_arrow_equivalent_bits(rows_to_insert, bits_per_key, min_num_bits);
    BasicBloomFilter basic_filter(basic_num_bits, 5);
    BlockedBloomFilter blocked_filter(rows_to_insert);
    ArrowBloomFilter arrow_filter(rows_to_insert, ArrowBloomFilter::ImplMode::scalar);
    ArrowBloomFilter arrow_filter_avx2(rows_to_insert, ArrowBloomFilter::ImplMode::avx2);
    ModifiedArrowBloomFilter modified_arrow_filter(rows_to_insert, ModifiedArrowBloomFilter::ImplMode::scalar);
    ModifiedArrowBloomFilter modified_arrow_avx2(rows_to_insert, ModifiedArrowBloomFilter::ImplMode::avx2);
    // ModifiedBlock16Filter modified_block16_filter(rows_to_insert, ModifiedBlock16Filter::ImplMode::scalar);
    // ModifiedBlock16Filter modified_block16_avx2(rows_to_insert, ModifiedBlock16Filter::ImplMode::avx2);
    ImprovedArrowBloomFilter impr_arrow_filter(rows_to_insert, ImprovedArrowBloomFilter::ImplMode::scalar);
    ImprovedArrowBloomFilter impr_arrow_filter_avx2(rows_to_insert, ImprovedArrowBloomFilter::ImplMode::avx2);
    // run all tests for every implementations
    run_allTests("BasicBloomFilter", basic_filter, to_insert, not_present, mixed_set, 0.05);
    run_allTests("BlockedBloomFilter", blocked_filter, to_insert, not_present, mixed_set, 0.05);
    run_allTests("ArrowBloomFilter_scalar", arrow_filter, to_insert, not_present, mixed_set, 0.05);
    run_allTests("ArrowBloomFilter_avx2", arrow_filter_avx2, to_insert, not_present, mixed_set, 0.05);
    run_allTests("ModifiedArrowFilter_scalar", modified_arrow_filter, to_insert, not_present, mixed_set, 0.05);
    run_allTests("ModifiedArrowFilter_avx2", modified_arrow_avx2, to_insert, not_present, mixed_set, 0.05);
    //run_allTests("16BitBlockFilter_scalar", modified_block16_filter, to_insert, not_present, mixed_set, 0.05);
    //run_allTests("16BitBlockFilter_avx2", modified_block16_avx2, to_insert, not_present, mixed_set, 0.05);
    run_allTests("ImprovedArrow_scalar", impr_arrow_filter, to_insert, not_present, mixed_set, 0.05);
    run_allTests("ImprovedArrow_avx2", impr_arrow_filter_avx2, to_insert, not_present, mixed_set, 0.05);
    std::cout << "\nAll tests passed.\n";
    return 0;
}