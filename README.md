# Bloom Filter DPMH 2026

A C++ project for testing and benchmarking multiple Bloom filter implementations. 
Created for Data Processing on Modern Hardware Praktikum, 07.2026.

## Build

Build the test and benchmark targets with:

```bash
make
```

## Correctness Tests

The test suite performs small correctness checks to verify that each Bloom filter implementation behaves as expected.

It checks that:

- A newly created Bloom filter has an empty bit array.
- Scalar and batch insertions update the bit array correctly.
- Inserting duplicate values does not change the bit array.
- Scalar and batch insertion produce equivalent results.
- Scalar and batch membership checks work correctly.
- The false-positive rate remains below the configured threshold.

The default false-positive-rate threshold is **5%** and can be changed in the test program's main function.

### Adding an implementation to the tests

Additional implementations can be tested as long as they implement `BloomFilter.h`. Register them in:

```text
src/test.cpp
```

## Benchmark

The benchmark measures:

- Build cycles
- Insertion time in nanoseconds per tuple
- Insertion cycles per tuple
- Probe time in nanoseconds per tuple
- Probe cycles per tuple

### Workloads

Three workload types are supported:

1. **All hits** — every lookup is expected to be present.
2. **All misses** — every lookup is expected to be absent.
3. **Mixed** — lookups contain both hits and misses.

### Output

Results are printed when the benchmark finishes. They can also be saved to a CSV file by using the `-f` option.

### Command-line options

| Option | Value | Default | Description |
|---|---:|---:|---|
| `-nr` | Integer | `720,721` | Number of rows inserted into the Bloom filter and number of negative membership checks performed. |
| `-r` | Integer | `10` | Number of measured, or “hot,” benchmark runs. |
| `-wr` | Integer | `1` | Number of warm-up runs. |
| `-f` | CSV path | Disabled | Writes benchmark results to the specified CSV file. |
| `--help` | — | — | Displays usage instructions. |

To add benchmark cases or Bloom filter implementations, modify:

```text
bench.cpp
```

## Available Implementations

- **Basic Bloom Filter**
- **Blocked Bloom Filter**
- **Apache Arrow Bloom Filter** — based on [Apache Arrow's implementation](https://github.com/apache/arrow/blob/main/cpp/src/arrow/acero/bloom_filter.h)
- **Improved Arrow Bloom Filter** — a slightly optimized version of the original Apache Arrow implementation
- **Modified Arrow Bloom Filter** — uses a direct lookup table instead of a sliding-window lookup table

## To-Dos

- [ ] Compare multithreaded approaches
- [ ] Add implementations that use smaller block sizes