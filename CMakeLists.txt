cmake_minimum_required(VERSION 3.16)
project(hevc-torture VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_WASM "Build WebAssembly target" OFF)
option(HEVC_DEBUG "Enable debug logging" OFF)

# Warnings
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Debug : AddressSanitizer
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT BUILD_WASM)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Debug logging
if(HEVC_DEBUG OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(HEVC_DEBUG=1)
endif()

# Library
add_library(hevc-torture-lib
    src/bitstream/bitstream_reader.cpp
)

target_include_directories(hevc-torture-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# CLI executable
add_executable(hevc-torture
    src/main.cpp
)

target_link_libraries(hevc-torture PRIVATE hevc-torture-lib)

# WASM-specific settings
if(BUILD_WASM)
    set_target_properties(hevc-torture PROPERTIES
        SUFFIX ".js"
    )
    target_compile_options(hevc-torture PRIVATE -O3)
    target_link_options(hevc-torture PRIVATE
        -s WASM=1
        -s ALLOW_MEMORY_GROWTH=1
        -s MODULARIZE=1
        -s EXPORT_NAME=HEVCDecoder
    )
endif()

# Tests
if(BUILD_TESTS AND NOT BUILD_WASM)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(hevc_tests
        tests/unit/test_bitstream_reader.cpp
    )

    target_link_libraries(hevc_tests PRIVATE
        hevc-torture-lib
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(hevc_tests)
endif()
