CXX := g++

CPPFLAGS := -I.
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O3 -DNDEBUG -mavx2

BUILD_DIR := build
BENCHMARK_TARGET := $(BUILD_DIR)/bloom_benchmark
TEST_TARGET := $(BUILD_DIR)/bloom_test

COMMON_SRCS := \
		bloom_filter/BasicBloomFilter.cpp \
        bloom_filter/BlockedBloomFilter.cpp \
        bloom_filter/ArrowBloomFilter.cpp

BENCHMARK_SRCS := \
	src/bench.cpp \
	$(COMMON_SRCS)

TEST_SRCS := \
	src/test.cpp \
	$(COMMON_SRCS)

COMMON_HEADERS := \
	bloom_filter/BasicBloomFilter.h \
	bloom_filter/BlockedBloomFilter.h \
	bloom_filter/ArrowBloomFilter.h \
	bloom_filter/BloomFilter.h \
	util/HelperFuncs.h

BENCHMARK_HEADERS := \
	$(COMMON_HEADERS) \
	util/PerfEvent.h

TEST_HEADERS := \
	$(COMMON_HEADERS)

.PHONY: all benchmark test clean

all: benchmark test

benchmark: $(BENCHMARK_TARGET)

test: $(TEST_TARGET)

$(BENCHMARK_TARGET): $(BENCHMARK_SRCS) $(BENCHMARK_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BENCHMARK_SRCS) -o $@

$(TEST_TARGET): $(TEST_SRCS) $(TEST_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_SRCS) -o $@

$(BUILD_DIR): 
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)