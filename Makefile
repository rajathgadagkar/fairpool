CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread -Iinclude

CORE_SRCS := src/fair_resource_pool.cpp src/logger.cpp src/simulator.cpp
TARGET    := build/fairpool
TEST_BIN  := build/fairpool_tests

.PHONY: all test sanitize-asan sanitize-tsan clean demo

all: $(TARGET)

build:
	@mkdir -p build

$(TARGET): build $(CORE_SRCS) src/main.cpp
	$(CXX) $(CXXFLAGS) $(CORE_SRCS) src/main.cpp -o $(TARGET)

$(TEST_BIN): build $(CORE_SRCS) tests/test_fairpool.cpp
	$(CXX) $(CXXFLAGS) $(CORE_SRCS) tests/test_fairpool.cpp -o $(TEST_BIN)

test: $(TEST_BIN) $(TARGET)
	./$(TEST_BIN)

sanitize-asan: build $(CORE_SRCS) tests/test_fairpool.cpp
	$(CXX) $(CXXFLAGS) -g -O1 -fsanitize=address,undefined $(CORE_SRCS) tests/test_fairpool.cpp -o build/fairpool_tests_asan
	./build/fairpool_tests_asan

sanitize-tsan: build $(CORE_SRCS) tests/test_fairpool.cpp
	$(CXX) $(CXXFLAGS) -g -O1 -fsanitize=thread $(CORE_SRCS) tests/test_fairpool.cpp -o build/fairpool_tests_tsan
	./build/fairpool_tests_tsan

demo: $(TARGET)
	./$(TARGET) --input tests/fixtures/case1_low_drop.in --tick-ms 150

clean:
	rm -rf build
