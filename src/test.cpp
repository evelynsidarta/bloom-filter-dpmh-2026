#include "bloom_filter/BasicBloomFilter.h"
#include "bloom_filter/ArrowBloomFilter.h"
#include "util/HelperFuncs.h"

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <iostream>

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
static void UT_noFalseNegatives(const std::vector<std::uint64_t>& inserted,
                                    const std::string& name,
                                    Filter& filter) {
    // insert values into the bloom filter
    for (std::uint64_t h : inserted) {
        filter.insert(h);
    }
    // afterwards check if all of them does not return false when probed
    for (std::uint64_t h : inserted) {
        check_filter(filter.contains(h), (name + " returns false negative after insertion."),
                        __FILE__, __LINE__);
    }
    // try inserting duplicate values for every inserted value
    for (std::uint64_t h : inserted) {
        filter.insert(h);
    }
    // check again if inserting duplicates screwed up the bloom filters somehow
    for (std::uint64_t h : inserted) {
        check_filter(filter.contains(h), (name + " returns false negative after duplicated"),
                        __FILE__, __LINE__);
    }    
}

// check whether or not fp rate is too high
//      not_present = some values that are not present in the table
template <typename Filter>
static void UT_fpRate(const Filter& filter,
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
    std::ostringstream msg;
    msg << name << ": false-positive rate unexpectedly high: " << fp_rate;
    check_filter((fp_rate <= desired_fp), msg.str(),
                    __FILE__, __LINE__);
}

template <typename Filter>
void run_allTests(const std::string& name, Filter& filter,
                    const std::vector<std::uint64_t>& to_insert,
                    const std::vector<std::uint64_t>& not_present,
                    double desired_fp) {
    UT_createEmpty(filter, not_present, name);
    UT_noFalseNegatives(to_insert, name, filter);
    UT_fpRate(filter, not_present, desired_fp, name);
}

int main() {
    // TODO: possible to modify these parameters
    constexpr std::size_t rows_to_insert = 10000;
    constexpr std::size_t num_inserts = 20000;
    constexpr std::size_t bits_per_key = 8;
    constexpr std::size_t min_num_bits = 512;
    // create empty basic and arrow bloom filters
    std::vector<std::uint64_t> to_insert = generate_hashes(rows_to_insert, 0x1ULL);
    std::vector<std::uint64_t> not_present = generate_hashes(num_inserts, 0x100000000ULL);
    // calculate equivalent bits for basic implementation
    std::size_t basic_num_bits = calculate_arrow_equivalent_bits(rows_to_insert, bits_per_key, min_num_bits);
    BasicBloomFilter basic_filter(basic_num_bits, 5);
    ArrowBloomFilter arrow_filter(rows_to_insert);
    // run all tests for every implementations
    run_allTests("BasicBloomFilter", basic_filter, to_insert, not_present, 0.10);
    run_allTests("ArrowBloomFilter", arrow_filter, to_insert, not_present, 0.10);
    std::cout << "\nAll tests passed.\n";
    return 0;
}