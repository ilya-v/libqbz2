# fuzz/CMakeLists.txt — Build fuzz harnesses for libqbz2
#
# Usage:
#   cmake -S fuzz -B build/fuzz -DCMAKE_C_COMPILER=clang \
#         -DLIBQBZ2_INCLUDE=include -DLIBQBZ2_LIB=build/libqbz2.a
#   cmake --build build/fuzz

cmake_minimum_required(VERSION 3.16)
project(libqbz2-fuzz C)

# Paths to libqbz2 and reference libbz2
set(LIBQBZ2_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../include" CACHE PATH "libqbz2 include dir")
set(LIBQBZ2_LIB "" CACHE FILEPATH "Path to libqbz2 static library")
set(REF_BZ2_LIB "" CACHE FILEPATH "Path to reference libbz2 shared library")

# Sanitizer and fuzzer flags
set(FUZZ_FLAGS "-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -g")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FUZZ_FLAGS}")

# Common include directories
include_directories(${LIBQBZ2_INCLUDE})

# Function to add a fuzz target
function(add_fuzz_target name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} ${LIBQBZ2_LIB})
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
