CXX := g++

CPPFLAGS := -I.
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -mavx2

BUILD_DIR := build
TARGET := $(BUILD_DIR)/bloom_test

SRCS := src/test.cpp \
		bloom_filter/BasicBloomFilter.cpp \
        bloom_filter/BlockedBloomFilter.cpp \
        bloom_filter/ArrowBloomFilter.cpp

HEADERS := $(wildcard bloom_filter/*.h) \
		$(wildcard util/*.h)

.PHONY: all test run clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRCS) -o $(TARGET)

$(BUILD_DIR): 
	mkdir -p $(BUILD_DIR)

test: $(TARGET)
	 ./$(TARGET)

run: test

clean:
	rm -rf $(BUILD_DIR)