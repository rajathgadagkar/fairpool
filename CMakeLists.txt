cmake_minimum_required(VERSION 3.16)
project(fairpool
    VERSION 1.0.0
    DESCRIPTION "Multithreaded FCFS Resource Pool & Patience-Aware Concurrency Simulator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(fairpool_core
    src/fair_resource_pool.cpp
    src/logger.cpp
    src/simulator.cpp
)

target_include_directories(fairpool_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(fairpool_core
    PUBLIC
        Threads::Threads
)

target_compile_options(fairpool_core
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

add_executable(fairpool src/main.cpp)
target_link_libraries(fairpool PRIVATE fairpool_core)
target_compile_options(fairpool
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

enable_testing()
add_executable(fairpool_tests tests/test_fairpool.cpp)
target_link_libraries(fairpool_tests PRIVATE fairpool_core)
target_compile_options(fairpool_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

add_test(NAME FairPoolUnitAndConcurrencyTests COMMAND fairpool_tests)
