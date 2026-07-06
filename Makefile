CXX := g++

CPPFLAGS := -I.
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -O2

BUILD_DIR := build
TARGET := bloom_test

SRCS := src/test.cpp \
        bloom_filter/ArrowBloomFilter.cpp \
        bloom_filter/BasicBloomFilter.cpp

OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)