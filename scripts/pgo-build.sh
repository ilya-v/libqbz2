#!/bin/bash
#
# PGO (Profile-Guided Optimization) build script for libqbz2
#
# Automates the three-step PGO process:
#   1. Instrumented build (-fprofile-generate)
#   2. Training run (compress+decompress across data types and block sizes)
#   3. Optimized rebuild (-fprofile-use) in the same directory
#
# Usage: scripts/pgo-build.sh [gcc|clang] [build_dir]
#
# Output: build_dir/libqbz2.a (PGO-optimized static library)
#

set -euo pipefail

COMPILER="${1:-gcc}"
BUILD_DIR="${2:-build/pgo}"
SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== libqbz2 PGO Build ==="
echo "Compiler: ${COMPILER}"
echo "Build dir: ${BUILD_DIR}"
echo "Source dir: ${SRC_DIR}"
echo ""

# Determine compiler-specific flags
if [ "${COMPILER}" = "gcc" ] || [[ "${COMPILER}" == *gcc* ]]; then
    CC_CMD="${COMPILER}"
    PROF_GEN_FLAGS="-fprofile-generate"
    PROF_USE_FLAGS="-fprofile-use -fprofile-correction"
elif [ "${COMPILER}" = "clang" ] || [[ "${COMPILER}" == *clang* ]]; then
    CC_CMD="${COMPILER}"
    PROF_GEN_FLAGS="-fprofile-instr-generate"
    PROF_USE_FLAGS="-fprofile-instr-use=${BUILD_DIR}/merged.profdata"
else
    echo "Error: unsupported compiler '${COMPILER}'. Use 'gcc' or 'clang'."
    exit 1
fi

mkdir -p "${BUILD_DIR}"

# --- Step 1: Instrumented build ---
echo "=== Step 1/3: Instrumented build ==="

cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_C_COMPILER="${CC_CMD}" \
    -DCMAKE_C_FLAGS="-O2 ${PROF_GEN_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${PROF_GEN_FLAGS}" \
    -DCMAKE_BUILD_TYPE=Release \
    2>&1 | tail -3

cmake --build "${BUILD_DIR}" --target pgo_training -- -j"$(nproc)" 2>&1

echo "Instrumented build complete."
echo ""

# --- Step 2: Training run ---
echo "=== Step 2/3: Training run ==="

if [[ "${COMPILER}" == *clang* ]] || [ "${COMPILER}" = "clang" ]; then
    export LLVM_PROFILE_FILE="${BUILD_DIR}/pgo_%p.profraw"
fi

cd "${BUILD_DIR}"
./pgo_training
cd "${SRC_DIR}"

# For Clang: merge raw profiles
if [[ "${COMPILER}" == *clang* ]] || [ "${COMPILER}" = "clang" ]; then
    echo "Merging Clang profile data..."
    llvm-profdata merge -output="${BUILD_DIR}/merged.profdata" "${BUILD_DIR}"/pgo_*.profraw
    rm -f "${BUILD_DIR}"/pgo_*.profraw
fi

echo "Training run complete."
echo ""

# --- Step 3: Optimized rebuild (in-place) ---
echo "=== Step 3/3: Optimized rebuild ==="

# Remove old object files so CMake rebuilds everything with PGO-use flags
rm -f "${BUILD_DIR}"/CMakeFiles/qbz2.dir/src/*.o
rm -f "${BUILD_DIR}"/libqbz2.a

cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_C_COMPILER="${CC_CMD}" \
    -DCMAKE_C_FLAGS="-O2 ${PROF_USE_FLAGS}" \
    -DCMAKE_BUILD_TYPE=Release \
    2>&1 | tail -3

cmake --build "${BUILD_DIR}" --target qbz2 -- -j"$(nproc)" 2>&1

echo ""
echo "=== PGO Build Complete ==="
echo "PGO-optimized library: ${BUILD_DIR}/libqbz2.a"
echo ""
echo "To use: link against ${BUILD_DIR}/libqbz2.a instead of the regular build."
