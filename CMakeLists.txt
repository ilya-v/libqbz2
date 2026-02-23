cmake_minimum_required(VERSION 3.14)

project(libqbz2
    VERSION 1.1.0
    DESCRIPTION "High-performance drop-in replacement for libbz2"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Need POSIX APIs (fdopen) on strict C11
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Library source files
set(LIBQBZ2_SOURCES
    src/blocksort.c
    src/bzlib.c
    src/compress.c
    src/crctable.c
    src/decompress.c
    src/huffman.c
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

# Differential tests (uses dlopen for reference)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_differential.c)
    add_executable(test_differential tests/test_differential.c)
    target_link_libraries(test_differential qbz2 dl)
    add_test(NAME differential_tests COMMAND test_differential)
endif()

# Benchmark
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench.c)
    add_executable(bench_throughput bench/bench.c)
    target_link_libraries(bench_throughput qbz2 dl)
endif()
