# fuzz/CMakeLists.txt — Build fuzz harnesses for libqbz2
#
# Builds the library from source with fuzzer coverage instrumentation,
# then links each harness with libFuzzer + ASAN + UBSAN.
#
# Usage:
#   cmake -S fuzz -B build/fuzz -DCMAKE_C_COMPILER=clang
#   cmake --build build/fuzz

cmake_minimum_required(VERSION 3.16)
project(libqbz2-fuzz C)

# Paths
set(LIBQBZ2_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src" CACHE PATH "libqbz2 source dir")
set(LIBQBZ2_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../include" CACHE PATH "libqbz2 include dir")

# Build libqbz2 from source with coverage instrumentation (fuzzer-no-link)
# This ensures libFuzzer can track coverage inside the library.
set(LIB_SOURCES
    ${LIBQBZ2_SRC}/blocksort.c
    ${LIBQBZ2_SRC}/huffman.c
    ${LIBQBZ2_SRC}/crctable.c
    ${LIBQBZ2_SRC}/randtable.c
    ${LIBQBZ2_SRC}/compress.c
    ${LIBQBZ2_SRC}/decompress.c
    ${LIBQBZ2_SRC}/bzlib.c
    ${LIBQBZ2_SRC}/libsais.c
)

add_library(qbz2_fuzz STATIC ${LIB_SOURCES})
target_include_directories(qbz2_fuzz PUBLIC ${LIBQBZ2_INCLUDE} PRIVATE ${LIBQBZ2_SRC})
target_compile_options(qbz2_fuzz PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-omit-frame-pointer -g -O1
)

# Harness compile/link flags
set(FUZZ_FLAGS "-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -g -O1")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FUZZ_FLAGS}")

# Common include directories
include_directories(${LIBQBZ2_INCLUDE})

# Function to add a fuzz target
function(add_fuzz_target name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} qbz2_fuzz)
    if(ARGN)
        target_link_libraries(${name} ${ARGN})
    endif()
endfunction()

# Fuzz targets
add_fuzz_target(fuzz_compress fuzz_compress.c)
add_fuzz_target(fuzz_decompress fuzz_decompress.c)
add_fuzz_target(fuzz_streaming fuzz_streaming.c)
add_fuzz_target(fuzz_bufftobuff fuzz_bufftobuff.c)
add_fuzz_target(fuzz_differential fuzz_differential.c dl)
add_fuzz_target(fuzz_diff_streaming fuzz_diff_streaming.c dl)
add_fuzz_target(fuzz_fileio fuzz_fileio.c)
