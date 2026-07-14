#include "bloom_filter/BasicBloomFilter.h"
#include "bloom_filter/BlockedBloomFilter.h"
#include "bloom_filter/ArrowBloomFilter.h"
#include "bloom_filter/ModifiedArrowBloomFilter.h"
#include "util/HelperFuncs.h"
#include "util/PerfEvent.h"

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
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>

// important:
//      sudo sysctl -w kernel.kptr_restrict=0 &&
//      sudo sysctl -w kernel.perf_event_paranoid=-1

// to ensure results and functions are not optimized "away" by the optimizer
volatile std::uint64_t benchmark_sink = 0;

std::uint64_t result_checksum(const std::vector<std::uint8_t>& result) {
    std::uint64_t checksum = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : result) {
        checksum = checksum ^ byte;
        checksum = checksum * 0x100000001b3ULL;
    }
    return checksum;
}

// struct to save all configuration choices
struct Config {
    std::size_t rows = 720'721;
    std::size_t negative_queries = 720'721;
    std::size_t hot_runs = 10;
    std::size_t warmup_runs = 1;
    std::string csv_path;
};

// since we have scalar and batch inserts/contains
enum class Mode { scalar, batch };

const char* to_string(Mode mode) {
    if (mode == Mode::scalar) {
        return "scalar";
    } else {
        return "batch";
    }
}

// struct to store benchmark details
struct BenchmarkResult {
    std::string implementation_name;
    // batch inserts / scalar inserts
    Mode insert_mode = Mode::scalar;
    // batch contains / scalar contains
    Mode probe_mode = Mode::scalar;
    // fp_rate = #fp / #queries
    double fp_rate = 0.0;
    // measure complete build time
    double build_cycles = 0.0;
    // insert cpu cycles / #inserted tuples
    double insert_cycles_per_tuple = 0.0;
    // probe cpu cycles / #queries
    double probe_hit_cycles_per_tuple = 0.0;
    double probe_miss_cycles_per_tuple = 0.0;
    double probe_mixed_cycles_per_tuple = 0.0;
    // elapsed time / #inserted
    double build_s = 0.0;
    double insert_ns_per_tuple = 0.0;
    double probe_hit_ns_per_tuple = 0.0;
    double probe_miss_ns_per_tuple = 0.0;
    double probe_mixed_ns_per_tuple = 0.0;
};

// struct to keep track of timing results
//      cycles per tuple + real time elapsed per tuple
struct Timekeeper {
    double cycles_per_tuple = 0.0;
    double ns_per_tuple = 0.0;
};

std::size_t parse_number(const char* arg, const char* option) {
    try {
        const std::string number_value(arg);
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(number_value, &consumed);
        // throw error if invalid value
        if (consumed != number_value.size()) {
            throw std::invalid_argument(std::string("Invalid value for ") + option + ": " + number_value);
        }
        if (parsed > std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument(std::string("Value too large for ") + option);
        }
        // return parsed value
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("Invalid value for ") + option + ": " + arg);
    }
}

// parse argument from terminal and store it inside Config object
Config parse_args(int argc, char** argv) {
    Config config;
    for (int i = 1; i < argc; i++) {
        std::string cur_arg(argv[i]);
        // helper function
        //      returns the next argument if current argument is valid
        auto confirm_next = [&](const char* option) -> const char* {
            if (i + 1 >= argc) {
                throw std::invalid_argument(std::string("Missing value for ") + option);
            }
            return argv[++i];
        };
        if (cur_arg == "-nr") {
            config.rows = parse_number(confirm_next("-nr"), "-nr");
            config.negative_queries = config.rows;
        } else if (cur_arg == "-r") {
            config.hot_runs = parse_number(confirm_next("-r"), "-r");
        } else if (cur_arg == "-wr") {
            config.warmup_runs = parse_number(confirm_next("-wr"), "-wr");
        } else if (cur_arg == "-f") {
            config.csv_path = confirm_next("-f");
        } else if (cur_arg == "--help" || cur_arg == "-h") {
            std::cout << "Usage: bloom_bench [-nr N] [-r N] [-wr N] [-f PATH]\n"
                      << "with -nr rows,\n"
                      << "     -r  hot runs,\n"
                      << "     -wr warmup runs,\n"
                      << "     -f  csv path\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown option: " + cur_arg);
        }
    }
    if (config.rows == 0) {
        throw std::invalid_argument("-r cannot be zero.\n");
    }
    if (config.hot_runs == 0) {
        throw std::invalid_argument("-r cannot be zero.\n");
    }
    return config;
}

// print out result tables
void print_table(const std::vector<BenchmarkResult>& results) {
    // for formatting only
    constexpr int w1 = 25;
    constexpr int w2 = 7;
    constexpr int w3 = 7;
    constexpr int w4 = 15;
    constexpr int w5 = 10;
    constexpr int t_w = w1 + 2 * w2 + w3 + 5 * w4 + 5 * w5;
    std::cout << '\n' << std::left << std::setw(w1) << "implementation"
              << std::left << std::setw(w2) << "insert"
              << std::left << std::setw(w2) << "probe"
              << std::right << std::setw(w3) << "FPR"
              << std::right << std::setw(w4) << "build cyc"
              << std::right << std::setw(w5) << "build s"
              << std::right << std::setw(w4) << "ins cyc/tup"
              << std::right << std::setw(w5) << "ins ns"
              << std::right << std::setw(w4) << "hit cyc/tup"
              << std::right << std::setw(w5) << "hit ns"
              << std::right << std::setw(w4) << "miss cyc/tup"
              << std::right << std::setw(w5) << "miss ns"
              << std::right << std::setw(w4) << "mixed cyc/tup"
              << std::right << std::setw(w5) << "mixed ns"
              << '\n';
    std::cout << std::string(t_w, '=') << '\n';
    for (const BenchmarkResult& i : results) {
        std::cout << std::left << std::setw(w1) << i.implementation_name
                  << std::setw(w2) << to_string(i.insert_mode)
                  << std::setw(w2) << to_string(i.probe_mode)
                  << std::right << std::fixed << std::setprecision(3)
                  << std::right << std::setw(w3) << i.fp_rate * 100.0 << "%"
                  << std::setprecision(1)
                  << std::setw(w4) << i.build_cycles
                  << std::setprecision(6)
                  << std::setw(w5) << i.build_s
                  << std::setprecision(3)
                  << std::setw(w4) << i.insert_cycles_per_tuple
                  << std::setw(w5) << i.insert_ns_per_tuple
                  << std::setw(w4) << i.probe_hit_cycles_per_tuple
                  << std::setw(w5) << i.probe_hit_ns_per_tuple
                  << std::setw(w4) << i.probe_miss_cycles_per_tuple
                  << std::setw(w5) << i.probe_miss_ns_per_tuple
                  << std::setw(w4) << i.probe_mixed_cycles_per_tuple
                  << std::setw(w5) << i.probe_mixed_ns_per_tuple
                  << '\n';
    }
}

// print out to file
void write_csv(const std::string& path, const std::vector<BenchmarkResult>& res) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Could not open output file: " + path);
    }
    out << "implementation,insert_mode,probe_mode,fp_rate,"
           "build_cycles,"
           "insert_cycles_per_tuple,probe_hit_cycles_per_tuple,"
           "probe_miss_cycles_per_tuple,probe_mixed_cycles_per_tuple,"
           "build_s,"
           "insert_ns_per_tuple,probe_hit_ns_per_tuple,"
           "probe_miss_ns_per_tuple,probe_mixed_ns_per_tuple\n";
    out << std::setprecision(12);
    for (const BenchmarkResult& i : res) {
        out << i.implementation_name << ','
            << to_string(i.insert_mode) << ','
            << to_string(i.probe_mode) << ','
            << i.fp_rate << ','
            << i.build_cycles << ','
            << i.insert_cycles_per_tuple << ','
            << i.probe_hit_cycles_per_tuple << ','
            << i.probe_miss_cycles_per_tuple << ','
            << i.probe_mixed_cycles_per_tuple << ','
            << i.build_s << ','
            << i.insert_ns_per_tuple << ','
            << i.probe_hit_ns_per_tuple << ','
            << i.probe_miss_ns_per_tuple << ','
            << i.probe_mixed_ns_per_tuple << '\n';
    }
}

// benchmark for inserting into Bloom Filters
template <typename Filter, typename FilterObj>
Timekeeper BENCH_buildFilters(PerfEvent& perf, FilterObj&& filter_obj,
                                const std::vector<std::uint64_t>& to_insert,
                                std::size_t warmup_runs, std::size_t hot_runs) {
    // do some initial cold runs and discard the result
    for (std::size_t i = 0; i < warmup_runs; i++) {
        std::unique_ptr<Filter> filter = filter_obj();
        // pretend to write results
        benchmark_sink = benchmark_sink ^ static_cast<std::uint64_t>(filter->contains(to_insert.back()));
    }
    // to store all the run results
    std::vector<double> cycle_results;
    std::vector<double> ns_results;
    cycle_results.reserve(hot_runs);
    ns_results.reserve(hot_runs);
    // do the hot runs
    for (std::size_t i = 0; i < hot_runs; i++) {
        // fence to prevent reordering
        std::atomic_signal_fence(std::memory_order_seq_cst);
        perf.startCounters();
        std::unique_ptr<Filter> filter = filter_obj();
        perf.stopCounters();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        // pretend to write results
        benchmark_sink = benchmark_sink ^ static_cast<std::uint64_t>(filter->contains(to_insert.back()));
        double cycles_elapsed = perf.getCounter("cycle");
        double s_r = perf.getDuration();
        cycle_results.push_back(cycles_elapsed);
        ns_results.push_back(s_r);
    }
    // return only the median
    return Timekeeper{median(std::move(cycle_results)), median(std::move(ns_results))};
}

// benchmark for inserting into Bloom Filters
template <typename Filter, typename FilterObj>
Timekeeper BENCH_insertFilters(PerfEvent& perf, FilterObj&& filter_obj,
                                const std::vector<std::uint64_t>& to_insert,
                                Mode insert_mode, std::size_t warmup_runs, std::size_t hot_runs) {
    // do some initial cold runs and discard the result
    for (std::size_t i = 0; i < warmup_runs; i++) {
        std::unique_ptr<Filter> filter = filter_obj();
        // scalar insert
        if (insert_mode == Mode::scalar) {
            for (std::uint64_t h : to_insert) {
                filter->insert(h);
            }
        } else {
            std::size_t count = to_insert.size();
            filter->insertBatch(to_insert.data(), count);
        }
        // pretend to write results
        benchmark_sink = benchmark_sink ^ static_cast<std::uint64_t>(filter->contains(to_insert.back()));
    }
    // to store all the run results
    std::vector<double> cycle_results;
    std::vector<double> ns_results;
    cycle_results.reserve(hot_runs);
    ns_results.reserve(hot_runs);
    // do the hot runs
    for (std::size_t i = 0; i < hot_runs; i++) {
        std::unique_ptr<Filter> filter = filter_obj();
        // fence to prevent reordering
        std::atomic_signal_fence(std::memory_order_seq_cst);
        // scalar insert
        if (insert_mode == Mode::scalar) {
            perf.startCounters();
            for (std::uint64_t h : to_insert) {
                filter->insert(h);
            }
            perf.stopCounters();
        } else {
        // batch insert
            perf.startCounters();
            std::size_t count = to_insert.size();
            filter->insertBatch(to_insert.data(), count);
            perf.stopCounters();
        }
        std::atomic_signal_fence(std::memory_order_seq_cst);
        // pretend to write results
        benchmark_sink = benchmark_sink ^ static_cast<std::uint64_t>(filter->contains(to_insert.back()));
        double cycles_elapsed = perf.getCounter("cycle");
        std::size_t count = to_insert.size();
        double cycles_r = cycles_elapsed / static_cast<double>(count);
        // convert time to ns
        double ns_r = perf.getDuration() * 1e9 / static_cast<double>(count);
        cycle_results.push_back(cycles_r);
        ns_results.push_back(ns_r);
    }
    // return only the median
    return Timekeeper{median(std::move(cycle_results)), median(std::move(ns_results))};
}

template <typename Filter>
Timekeeper BENCH_probeFilters(PerfEvent& perf, const Filter& filter,
                                const std::vector<std::uint64_t>& queries,
                                Mode probe_mode, std::size_t warmup_runs, std::size_t hot_runs) {
    std::vector<std::uint8_t> probe_result(result_bytes(queries.size()));
    // warm up runs
    for (std::size_t i = 0; i < warmup_runs; i++) {
        if (probe_mode == Mode::scalar) {
            std::fill(probe_result.begin(), probe_result.end(), 0);
            for (std::size_t j = 0; j < queries.size(); j++) {
                if (filter.contains(queries[j])) {
                    probe_result[j / 8] |= static_cast<std::uint8_t>(1u << (j % 8));
                }
            }
        } else {
            filter.containsBatch(queries.data(), probe_result.data(), queries.size());
        }
        benchmark_sink = benchmark_sink ^ result_checksum(probe_result);
    }
    // to store all the run results
    std::vector<double> cycle_results;
    std::vector<double> ns_results;
    cycle_results.reserve(hot_runs);
    ns_results.reserve(hot_runs);
    // do the hot runs
    for (std::size_t i = 0; i < hot_runs; i++) {
        // fence to prevent reordering
        std::atomic_signal_fence(std::memory_order_seq_cst);
        // scalar probe
        if (probe_mode == Mode::scalar) {
            std::fill(probe_result.begin(), probe_result.end(), 0);
            perf.startCounters();
            for (std::size_t j = 0; j < queries.size(); j++) {
                if (filter.contains(queries[j])) {
                    probe_result[j / 8] |= static_cast<std::uint8_t>(1u << (j % 8));
                }
            }
            perf.stopCounters();
        } else {
        // batch insert
            perf.startCounters();
            std::size_t count = queries.size();
            filter.containsBatch(queries.data(), probe_result.data(), count);
            perf.stopCounters();
        }
        std::atomic_signal_fence(std::memory_order_seq_cst);
        benchmark_sink = benchmark_sink ^ result_checksum(probe_result);
        double cycles_elapsed = perf.getCounter("cycle");
        std::size_t count = queries.size();
        double cycles_r = cycles_elapsed / static_cast<double>(count);
        // convert time to ns
        double ns_r = perf.getDuration() * 1e9 / static_cast<double>(count);
        cycle_results.push_back(cycles_r);
        ns_results.push_back(ns_r);
    }
    return Timekeeper{median(std::move(cycle_results)), median(std::move(ns_results))};
}

template <typename Filter, typename FilterObj>
BenchmarkResult run_allBenchmark(PerfEvent& perf, std::string name,
                                    FilterObj&& filter_obj, Mode insert_mode, 
                                    Mode probe_mode,
                                    const std::vector<std::uint64_t>& to_insert,
                                    const std::vector<std::uint64_t>& not_present,
                                    const std::vector<std::uint64_t>& mixed,
                                    const Config& config) {
    std::cout << "Starting benchmark for " << name 
              << " (insert = " << to_string(insert_mode) 
              << ", probe = " << to_string(probe_mode)
              << ")..." << std::endl;
    const Timekeeper build_res = BENCH_buildFilters<Filter>(perf, filter_obj, to_insert,
                                                                config.warmup_runs, config.hot_runs);
    const Timekeeper insert_res = BENCH_insertFilters<Filter>(perf, filter_obj, 
                                                                to_insert, insert_mode, 
                                                                config.warmup_runs, config.hot_runs);
    std::unique_ptr<Filter> filter = filter_obj();
    // build the filters again for the probe test
    //      insert mode doesn't matter here
    filter->insertBatch(to_insert.data(), to_insert.size());
    // benchmark fp rate
    //      skip correctness problems, since it should have been tested
    //      by test.cpp
    std::vector<std::uint8_t> hit_result(result_bytes(not_present.size()));
    filter->containsBatch(not_present.data(), hit_result.data(), not_present.size());
    // number of queries that return true
    std::size_t false_positives = count_packed_true(hit_result, not_present.size());
    double fp_rate = static_cast<double>(false_positives) / static_cast<double>(not_present.size());
    // benchmark probe
    Timekeeper hit_probe = BENCH_probeFilters(perf, *filter, to_insert, probe_mode, config.warmup_runs, config.hot_runs);
    Timekeeper miss_probe = BENCH_probeFilters(perf, *filter, not_present, probe_mode, config.warmup_runs, config.hot_runs);
    Timekeeper mixed_probe = BENCH_probeFilters(perf, *filter, mixed, probe_mode, config.warmup_runs, config.hot_runs);
    // build benchmark result
    return BenchmarkResult{
        std::move(name), insert_mode, probe_mode, fp_rate, 
        build_res.cycles_per_tuple, insert_res.cycles_per_tuple,
        hit_probe.cycles_per_tuple, miss_probe.cycles_per_tuple, mixed_probe.cycles_per_tuple,
        build_res.ns_per_tuple, insert_res.ns_per_tuple, hit_probe.ns_per_tuple, 
        miss_probe.ns_per_tuple, mixed_probe.ns_per_tuple
    };
}

int main(int argc, char** argv) {
#if !defined(__linux__)
    throw std::runtime_error("Unable to use PerfEvent.h. Please use Linux.");
#endif
    // parse arguments
    const Config config = parse_args(argc, argv);
    PerfEvent perf;
    if (perf.events.empty()) {
        throw std::runtime_error("Could not open hardware counters. Try sudo sysctl -w kernel.kptr_restrict=0 && sudo sysctl -w kernel.perf_event_paranoid=-1.");
    }
    std::unordered_set<std::uint64_t> used_hashes;
    used_hashes.reserve(config.rows + config.negative_queries);
    // generate hash values to insert and for negative check
    std::vector<std::uint64_t> to_insert = generate_unique(config.rows, 0x1ULL, used_hashes);
    std::vector<std::uint64_t> not_present = generate_unique(config.negative_queries, 0x100000000ULL, used_hashes);
    std::vector<std::uint64_t> mixed = mix_vector(to_insert, not_present);
    // TODO: possible to modify this
    constexpr std::size_t bits_per_key = 8;
    constexpr std::size_t min_num_bits = 512;
    std::size_t basic_num_bits = calculate_arrow_equivalent_bits(config.rows, bits_per_key, min_num_bits);
    std::vector<BenchmarkResult> test_results;
    // TODO: adjust to number of tests
    test_results.reserve(10);
    // run tests and append results to the BenchmarkResult object
    std::cout << "Starting Bloom Filter benchmark with the following parameters:\n"
              << "rows: " << config.rows << '\n'
              << "negative queries: " << config.negative_queries << '\n'
              << "warm-up runs: " << config.warmup_runs << '\n'
              << "hot runs: " << config.hot_runs << '\n';
    auto make_basic_filter = [basic_num_bits]() {
        return std::make_unique<BasicBloomFilter>(basic_num_bits, 5);
    };
    auto make_blocked_filter = [&config]() {
        return std::make_unique<BlockedBloomFilter>(config.rows);
    };
    auto make_arrow_filter = [&config]() {
        return std::make_unique<ArrowBloomFilter>(config.rows, ArrowBloomFilter::ImplMode::scalar);
    };
    auto make_arrow_filter_avx2 = [&config]() {
        return std::make_unique<ArrowBloomFilter>(config.rows, ArrowBloomFilter::ImplMode::avx2);
    };
    auto make_modified_arrow_filter = [&config]() {
        return std::make_unique<ModifiedArrowBloomFilter>(config.rows, ModifiedArrowBloomFilter::ImplMode::scalar);
    };
    auto make_modified_arrow_filter_avx2 = [&config]() {
        return std::make_unique<ModifiedArrowBloomFilter>(config.rows, ModifiedArrowBloomFilter::ImplMode::avx2);
    };
    test_results.push_back(run_allBenchmark<BasicBloomFilter>(perf,
                                "BasicBloomFilter", make_basic_filter,
                                Mode::scalar, Mode::scalar, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<BasicBloomFilter>(perf,
                                "BasicBloomFilter", make_basic_filter,
                                Mode::batch, Mode::batch, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<BlockedBloomFilter>(perf,
                                "BlockedBloomFilter", make_blocked_filter,
                                Mode::scalar, Mode::scalar, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<BlockedBloomFilter>(perf,
                                "BlockedBloomFilter", make_blocked_filter,
                                Mode::batch, Mode::batch, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<ArrowBloomFilter>(perf,
                                "ArrowBloomFilter", make_arrow_filter,
                                Mode::scalar, Mode::scalar, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<ArrowBloomFilter>(perf,
                                "ArrowBloomFilter", make_arrow_filter,
                                Mode::batch, Mode::batch, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<ArrowBloomFilter>(perf,
                                "ArrowBloomFilter_avx2", make_arrow_filter_avx2,
                                Mode::batch, Mode::batch, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<ModifiedArrowBloomFilter>(perf,
                                "ModifiedArrowFilter", make_modified_arrow_filter,
                                Mode::scalar, Mode::scalar, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<ModifiedArrowBloomFilter>(perf,
                                "ModifiedArrowFilter", make_modified_arrow_filter,
                                Mode::batch, Mode::batch, to_insert, not_present, mixed, config));
    test_results.push_back(run_allBenchmark<ModifiedArrowBloomFilter>(perf,
                                "ModifiedArrowFilter_avx2", make_modified_arrow_filter_avx2,
                                Mode::batch, Mode::batch, to_insert, not_present, mixed, config));
    print_table(test_results);
    // write down results
    if (!config.csv_path.empty()) {
        write_csv(config.csv_path, test_results);
        std::cout << "\nResults written in " << config.csv_path << '\n';
    }
    return 0;
}