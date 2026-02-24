cmake_minimum_required(VERSION 3.14)

project(libqbz2
    VERSION 1.1.0
    DESCRIPTION "High-performance drop-in replacement for libbz2"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release build — prevents accidental unoptimized builds that
# produce misleading benchmark results. Can be overridden with
# -DCMAKE_BUILD_TYPE=Debug or -DCMAKE_BUILD_TYPE=RelWithDebInfo.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified — defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Need POSIX APIs (fdopen) on strict C11
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Hardware CRC-32 acceleration via PCLMULQDQ (build-time detection)
include(CheckCCompilerFlag)
check_c_compiler_flag("-mpclmul -msse4.2" HAVE_PCLMUL)

# Library source files
set(LIBQBZ2_SOURCES
    src/blocksort.c
    src/bzlib.c
    src/compress.c
    src/crctable.c
    src/crc32_pclmul.c
    src/decompress.c
    src/huffman.c
    src/libsais.c
    src/randtable.c
)

# Static library
add_library(qbz2 STATIC ${LIBQBZ2_SOURCES})

target_include_directories(qbz2
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Strict warning flags
target_compile_options(qbz2 PRIVATE
    -Wall -Wextra -Wpedantic
)

# Enable PCLMULQDQ hardware CRC if supported
if(HAVE_PCLMUL)
    set_source_files_properties(src/crc32_pclmul.c src/crctable.c
        PROPERTIES COMPILE_FLAGS "-mpclmul -msse4.2 -mssse3")
endif()

# Shared library
add_library(qbz2_shared SHARED ${LIBQBZ2_SOURCES})

target_include_directories(qbz2_shared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(qbz2_shared PRIVATE
    -Wall -Wextra -Wpedantic
)

set_target_properties(qbz2_shared PROPERTIES
    OUTPUT_NAME bz2
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# Build reference library from cloned source
set(REF_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/reference/bzip2)
set(REF_SOURCES
    ${REF_SRC_DIR}/blocksort.c
    ${REF_SRC_DIR}/bzlib.c
    ${REF_SRC_DIR}/compress.c
    ${REF_SRC_DIR}/crctable.c
    ${REF_SRC_DIR}/decompress.c
    ${REF_SRC_DIR}/huffman.c
    ${REF_SRC_DIR}/randtable.c
)

# Generate bz_version.h for reference build
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ref_bz_version.h
    "#define BZ_VERSION \"1.1.0\"\n"
)

add_library(bz2_ref STATIC ${REF_SOURCES})
target_include_directories(bz2_ref
    PUBLIC ${REF_SRC_DIR}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
)
# The reference includes bz_version.h which we generated
target_include_directories(bz2_ref PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# Rename the generated file so it matches what bzlib.c expects
configure_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ref_bz_version.h
    ${CMAKE_CURRENT_BINARY_DIR}/bz_version.h
    COPYONLY
)

# ASAN+UBSAN build variant
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(qbz2 PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(qbz2 PRIVATE -fsanitize=address,undefined)
    target_compile_options(qbz2_shared PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(qbz2_shared PRIVATE -fsanitize=address,undefined)
    target_compile_options(bz2_ref PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(bz2_ref PRIVATE -fsanitize=address,undefined)
endif()

# --- PGO (Profile-Guided Optimization) support ---
# Training driver used by the PGO build script
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/pgo_training.c)
    add_executable(pgo_training tools/pgo_training.c)
    target_link_libraries(pgo_training qbz2)
endif()

# Custom target: run the full PGO build pipeline
# Usage: cmake --build build --target pgo
add_custom_target(pgo
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pgo-build.sh
            gcc ${CMAKE_CURRENT_SOURCE_DIR}/build/pgo
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running PGO build pipeline (instrument -> train -> optimize)"
    VERBATIM
)

# --- Test and benchmark targets (tester-owned) ---
enable_testing()

# Unit tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.c)
    add_executable(test_api tests/test_api.c)
    target_link_libraries(test_api qbz2)
    add_test(NAME unit_tests COMMAND test_api)
endif()

# Edge case and conformance tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_edge_cases.c)
    add_executable(test_edge_cases tests/test_edge_cases.c)
    target_link_libraries(test_edge_cases qbz2)
    add_test(NAME edge_case_tests COMMAND test_edge_cases)
endif()

# Streaming and stress tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_streaming.c)
    add_executable(test_streaming tests/test_streaming.c)
    target_link_libraries(test_streaming qbz2)
    add_test(NAME streaming_tests COMMAND test_streaming)
endif()

# Advanced tests (state machines, bzopen API, concatenated streams, RLE, memory)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_advanced.c)
    add_executable(test_advanced tests/test_advanced.c)
    target_link_libraries(test_advanced qbz2)
    add_test(NAME advanced_tests COMMAND test_advanced)
endif()

# Systematic round-trip tests (patterns, sizes, block sizes, work factors)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_roundtrip.c)
    add_executable(test_roundtrip tests/test_roundtrip.c)
    target_link_libraries(test_roundtrip qbz2)
    add_test(NAME roundtrip_tests COMMAND test_roundtrip)
endif()

# Error paths and state machine coverage
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_error_paths.c)
    add_executable(test_error_paths tests/test_error_paths.c)
    target_link_libraries(test_error_paths qbz2)
    add_test(NAME error_path_tests COMMAND test_error_paths)
endif()

# Differential tests (uses dlopen for reference)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_differential.c)
    add_executable(test_differential tests/test_differential.c)
    target_link_libraries(test_differential qbz2 dl)
    add_test(NAME differential_tests COMMAND test_differential)
endif()

# Multi-block differential tests (uses dlopen for reference)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_diff_multiblock.c)
    add_executable(test_diff_multiblock tests/test_diff_multiblock.c)
    target_link_libraries(test_diff_multiblock qbz2 dl)
    add_test(NAME diff_multiblock_tests COMMAND test_diff_multiblock)
endif()

# FILE* I/O comprehensive tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fileio.c)
    add_executable(test_fileio tests/test_fileio.c)
    target_link_libraries(test_fileio qbz2)
    add_test(NAME fileio_tests COMMAND test_fileio)
endif()

# Multi-block CRC regression and streaming edge cases
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_multiblock.c)
    add_executable(test_multiblock tests/test_multiblock.c)
    target_link_libraries(test_multiblock qbz2)
    add_test(NAME multiblock_tests COMMAND test_multiblock)
endif()

# Blocksort path tests (SA-IS, fallback, mainSort, intermediate block sizes)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_blocksort_paths.c)
    add_executable(test_blocksort_paths tests/test_blocksort_paths.c)
    target_link_libraries(test_blocksort_paths qbz2)
    add_test(NAME blocksort_paths_tests COMMAND test_blocksort_paths)
endif()

# bzip2-tests corpus conformance (uses dlopen for reference)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bzip2_corpus.c)
    add_executable(test_bzip2_corpus tests/test_bzip2_corpus.c)
    target_link_libraries(test_bzip2_corpus qbz2 dl)
    add_test(NAME bzip2_corpus_tests COMMAND test_bzip2_corpus)
endif()

# OOM (out-of-memory) regression tests using custom allocator hooks
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_oom.c)
    add_executable(test_oom tests/test_oom.c)
    target_link_libraries(test_oom qbz2)
    add_test(NAME oom_tests COMMAND test_oom)
endif()

# Decompression error coverage tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_decompress_errors.c)
    add_executable(test_decompress_errors tests/test_decompress_errors.c)
    target_link_libraries(test_decompress_errors qbz2)
    add_test(NAME decompress_error_tests COMMAND test_decompress_errors)
endif()

# Malformed bz2 stream decompression tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_malformed.c)
    add_executable(test_malformed tests/test_malformed.c)
    target_link_libraries(test_malformed qbz2)
    add_test(NAME malformed_tests COMMAND test_malformed)
endif()

# Streaming API state machine edge cases
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_streaming_edge.c)
    add_executable(test_streaming_edge tests/test_streaming_edge.c)
    target_link_libraries(test_streaming_edge qbz2)
    add_test(NAME streaming_edge_tests COMMAND test_streaming_edge)
endif()

# Parameter combination coverage (bs x wf x data type, differential)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_param_combos.c)
    add_executable(test_param_combos tests/test_param_combos.c)
    target_link_libraries(test_param_combos qbz2 dl)
    add_test(NAME param_combo_tests COMMAND test_param_combos)
endif()

# Concatenated stream read-ahead tests (uses dlopen for reference)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_concat_readahead.c)
    add_executable(test_concat_readahead tests/test_concat_readahead.c)
    target_link_libraries(test_concat_readahead qbz2 dl)
    add_test(NAME concat_readahead_tests COMMAND test_concat_readahead)
endif()

# RLE/Huffman/MTF edge cases with differential verification
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rle_huffman_edge.c)
    add_executable(test_rle_huffman_edge tests/test_rle_huffman_edge.c)
    target_link_libraries(test_rle_huffman_edge qbz2 dl)
    add_test(NAME rle_huffman_edge_tests COMMAND test_rle_huffman_edge)
endif()

# Block boundary bit-reader tests (uses dlopen for reference)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_block_boundary_bitreader.c)
    add_executable(test_block_boundary_bitreader tests/test_block_boundary_bitreader.c)
    target_link_libraries(test_block_boundary_bitreader qbz2 dl)
    add_test(NAME block_boundary_bitreader_tests COMMAND test_block_boundary_bitreader)
endif()

# Compression state machine edge cases
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compress_states.c)
    add_executable(test_compress_states tests/test_compress_states.c)
    target_link_libraries(test_compress_states qbz2)
    add_test(NAME compress_states_tests COMMAND test_compress_states)
endif()

# Buffer-to-buffer API edge cases and small-mode decompression
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bufftobuff_edge.c)
    add_executable(test_bufftobuff_edge tests/test_bufftobuff_edge.c)
    target_link_libraries(test_bufftobuff_edge qbz2)
    add_test(NAME bufftobuff_edge_tests COMMAND test_bufftobuff_edge)
endif()

# Huffman decode OOB regression tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_huffman_decode_oob.c)
    add_executable(test_huffman_decode_oob tests/test_huffman_decode_oob.c)
    target_link_libraries(test_huffman_decode_oob qbz2)
    target_compile_definitions(test_huffman_decode_oob PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME huffman_decode_oob_tests COMMAND test_huffman_decode_oob)
endif()

# Branch coverage gap tests (randomised blocks, FILE* API, verbose, bzdopen, etc.)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_coverage_gaps.c)
    add_executable(test_coverage_gaps tests/test_coverage_gaps.c)
    target_link_libraries(test_coverage_gaps qbz2)
    add_test(NAME coverage_gaps_tests COMMAND test_coverage_gaps)
endif()

# Coverage gap tests round 2 (NULL params, state machine suspension, verbosity)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_coverage_gaps2.c)
    add_executable(test_coverage_gaps2 tests/test_coverage_gaps2.c)
    target_link_libraries(test_coverage_gaps2 qbz2)
    add_test(NAME coverage_gaps2_tests COMMAND test_coverage_gaps2)
endif()

# decompress.c branch coverage tests (GET_BITS paths, RLE decode, streaming)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_decompress_branches.c)
    add_executable(test_decompress_branches tests/test_decompress_branches.c)
    target_link_libraries(test_decompress_branches qbz2)
    add_test(NAME decompress_branch_tests COMMAND test_decompress_branches)
endif()

# bzlib.c branch coverage tests (API edge cases, state machine, verbosity)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bzlib_branches.c)
    add_executable(test_bzlib_branches tests/test_bzlib_branches.c)
    target_link_libraries(test_bzlib_branches qbz2)
    add_test(NAME bzlib_branch_tests COMMAND test_bzlib_branches)
endif()

# blocksort.c branch coverage tests (fallbackSort internals, threshold boundary)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_blocksort_branches.c)
    add_executable(test_blocksort_branches tests/test_blocksort_branches.c)
    target_link_libraries(test_blocksort_branches qbz2)
    add_test(NAME blocksort_branch_tests COMMAND test_blocksort_branches)
endif()

# compress.c branch coverage tests (nGroups thresholds, Huffman tooLong, etc.)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compress_branches.c)
    add_executable(test_compress_branches tests/test_compress_branches.c)
    target_link_libraries(test_compress_branches qbz2)
    add_test(NAME compress_branch_tests COMMAND test_compress_branches)
endif()

# Decompression CRC correctness tests (streaming output buffer boundaries)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_decompress_crc.c)
    add_executable(test_decompress_crc tests/test_decompress_crc.c)
    target_link_libraries(test_decompress_crc qbz2 dl)
    add_test(NAME decompress_crc_tests COMMAND test_decompress_crc)
endif()

# CRC-32 internal correctness tests (PCLMULQDQ, slicing-by-8, alignment)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_crc32_internal.c)
    add_executable(test_crc32_internal tests/test_crc32_internal.c)
    target_include_directories(test_crc32_internal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_crc32_internal qbz2)
    add_test(NAME crc32_internal_tests COMMAND test_crc32_internal)
endif()

# Streaming state machine tests (compression/decompression transitions)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_streaming_states.c)
    add_executable(test_streaming_states tests/test_streaming_states.c)
    target_link_libraries(test_streaming_states qbz2)
    add_test(NAME streaming_state_tests COMMAND test_streaming_states)
endif()

# Legacy randomised block decompression path coverage (uses dlopen for reference)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_randomised_blocks.c)
    add_executable(test_randomised_blocks tests/test_randomised_blocks.c)
    target_link_libraries(test_randomised_blocks qbz2 dl)
    add_test(NAME randomised_block_tests COMMAND test_randomised_blocks)
endif()

# Benchmark — enforce optimization to prevent misleading results
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench.c)
    add_executable(bench_throughput bench/bench.c)
    target_link_libraries(bench_throughput qbz2 dl)

    # Benchmarks are meaningless without optimization. Warn loudly and
    # force -O2 on the benchmark binary when the build type is unset or Debug.
    if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Building benchmarks without optimization (CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'). "
                        "Results will be misleading. Use -DCMAKE_BUILD_TYPE=Release for valid benchmarks.")
        target_compile_options(bench_throughput PRIVATE -O2)
    endif()
endif()
