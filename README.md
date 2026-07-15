# bloom-filter-dpmh-2026

How to use:
build test & benchmark -> make

TEST
- small "correctness" test to ensure bloom filters are working properly
- tests:
    -- creating empty bloom array and ensure bitarray is not set (totally empty) during creation
    -- inserting into bloom filters in a scalar and batched manner and test bitarray state after insertion
    -- tries inserting duplicates into the bloom filter and check if the bit array changes at all afterwards
    -- checks if scalar and batch insertion behaves in the same way
    -- checks contains function (both scalar and batched contains)
    -- checks if fp rate of the bloom filter is abnormally high (it needs to be below threshold specified in main function)
- possible to insert more implementations (that implement BloomFilter.h), simply modify main function in src/test.cpp
- possible to modify fp rate to check against (default is set to 5%)

BENCHMARK
- benchmarks the build cycles, insert ns/tuple, insert cyc/tuple, probe ns/tuple, probe cyc/tuple for 3 different types of workloads
- different workloads: all hits, all misses, mixed workload (with hits and mits to the Bloom Filter)
- different Bloom filter implementations: Basic Bloom FIlter implementation, Blocked Bloom Filter, Arrow's Bloom filter, etc.
- possible to add more tests/bloom filter implementations (modify bench.cpp)
- results are printed out at the end (or saved to csv if the path is specified with -f option)

run the benchmark (possible with following options):
    -nr INTEGER_NUMBER -> number of rows to insert to the Bloom Filter (and number of negative checks to do to the filter) (720'721 by default)
    -r  INTEGER_NUMBER -> number of hot runs to do for the benchmark (10 by default)
    -wr INTEGER_NUMBER -> number of warmup runs to do (1 by default)
    -f  CSV_PATH       -> to write down results of the benchmark into the specified path (turned off by default)
    --help for instructions

CURRENTLY AVAILABLE IMPLEMENTATIONS:
- Basic bloom filter
- Blocked bloom filter
- Apache Arrow's bloom filter, src: https://github.com/apache/arrow/blob/main/cpp/src/arrow/acero/bloom_filter.h
- Improved Arrow bloom filter -> slightly optimized version of Apache Arrow's original implementation
- Modified Arrow bloom filter -> modified version of Arrow's implementation with a direct lookup table instead of sliding-window lookup table

TO-DOs:
- multithreaded approach comparisons
- implementations that use smaller block sizes