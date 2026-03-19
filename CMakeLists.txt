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

    # Oracle tests — compare decoded YUV MD5 against reference
    # These tests require the decoder to produce YUV output (-o flag)
    # They are labeled "oracle" so they can be run separately: ctest -L oracle
    set(FIXTURES_DIR "${CMAKE_SOURCE_DIR}/tests/conformance/fixtures")
    set(ORACLE_SCRIPT "${CMAKE_SOURCE_DIR}/tools/oracle_test.sh")

    # All oracle tests skip gracefully when decoder can't produce output yet
    # Exit code 2 = SKIP (decoder not implemented for this feature)

    # Phase 4 — I-only, no filters
    add_test(NAME oracle_i_64x64_qp22
        COMMAND ${ORACLE_SCRIPT} ${FIXTURES_DIR}/i_64x64_qp22.265 64 64
            48c64cd2de381113913149e92065d66c $<TARGET_FILE:hevc-torture>)
    set_tests_properties(oracle_i_64x64_qp22 PROPERTIES LABELS "oracle;phase4"
        SKIP_RETURN_CODE 2)

    # Phase 5 — P-frames, no filters
    add_test(NAME oracle_p_qcif_10f
        COMMAND ${ORACLE_SCRIPT} ${FIXTURES_DIR}/p_qcif_10f.265 176 144
            921650ab84b8e366a1aa4e16f2527e46 $<TARGET_FILE:hevc-torture>)
    set_tests_properties(oracle_p_qcif_10f PROPERTIES LABELS "oracle;phase5"
        SKIP_RETURN_CODE 2)

    # Phase 5 — B-frames, no filters
    add_test(NAME oracle_b_qcif_10f
        COMMAND ${ORACLE_SCRIPT} ${FIXTURES_DIR}/b_qcif_10f.265 176 144
            dffe2712c0561c75ee40256d59299826 $<TARGET_FILE:hevc-torture>)
    set_tests_properties(oracle_b_qcif_10f PROPERTIES LABELS "oracle;phase5"
        SKIP_RETURN_CODE 2)

    # Phase 6 — I-frame with deblocking only
    add_test(NAME oracle_i_64x64_deblock
        COMMAND ${ORACLE_SCRIPT} ${FIXTURES_DIR}/i_64x64_deblock.265 64 64
            576ef497ceedffa01709ddfaa13c76c3 $<TARGET_FILE:hevc-torture>)
    set_tests_properties(oracle_i_64x64_deblock PROPERTIES LABELS "oracle;phase6"
        SKIP_RETURN_CODE 2)

    # Phase 6 — I-frame with SAO only
    add_test(NAME oracle_i_64x64_sao
        COMMAND ${ORACLE_SCRIPT} ${FIXTURES_DIR}/i_64x64_sao.265 64 64
            eacb4952591daa891e8dc5344c6c34ee $<TARGET_FILE:hevc-torture>)
    set_tests_properties(oracle_i_64x64_sao PROPERTIES LABELS "oracle;phase6"
        SKIP_RETURN_CODE 2)

    # Phase 6 — I-frame with deblocking + SAO
    add_test(NAME oracle_i_64x64_full
        COMMAND ${ORACLE_SCRIPT} ${FIXTURES_DIR}/i_64x64_full.265 64 64
            dcb7ae02b4514b6e7fd801ae691d901a $<TARGET_FILE:hevc-torture>)
    set_tests_properties(oracle_i_64x64_full PROPERTIES LABELS "oracle;phase6"
        SKIP_RETURN_CODE 2)

    # Phase 6 — Full pipeline (B-frames + deblocking + SAO) = Main profile milestone
    add_test(NAME oracle_full_qcif_10f
        COMMAND ${ORACLE_SCRIPT} ${FIXTURES_DIR}/full_qcif_10f.265 176 144
            a4984ee61d18ac9619808c5338132eaa $<TARGET_FILE:hevc-torture>)
    set_tests_properties(oracle_full_qcif_10f PROPERTIES LABELS "oracle;phase6;milestone"
        SKIP_RETURN_CODE 2)
endif()
