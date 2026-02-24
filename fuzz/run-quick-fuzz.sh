#!/bin/bash
# run-quick-fuzz.sh — Per-commit quick fuzz script.
#
# Runs a curated subset of fuzz harnesses with ASAN enabled.
# Designed to be called by the tester's validation pipeline with no arguments.
# Uses up to 3 cores. Must fit within the time allocated by the validation pipeline
# (part of the 2-minute per-commit budget).
#
# Controlled by: fuzz/quick-fuzz-list.txt (list of harnesses to run)
# Exit code: 0 if all harnesses pass, 1 if any crash or divergence found.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/fuzz"
CORPUS_DIR="$SCRIPT_DIR/corpus"
FUZZ_LIST="$SCRIPT_DIR/quick-fuzz-list.txt"
OUTPUT_DIR="$PROJECT_DIR/test-output/quick-fuzz"

# Time budget per harness (seconds)
TIME_PER_HARNESS=10

# Max total time (seconds) — leave room for other validation stages
MAX_TOTAL_TIME=30

# Number of parallel jobs
MAX_JOBS=3

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${GREEN}[quick-fuzz]${NC} $*"; }
warn() { echo -e "${YELLOW}[quick-fuzz]${NC} $*"; }
err() { echo -e "${RED}[quick-fuzz]${NC} $*" >&2; }

# --- Pre-flight checks ---

if [ ! -f "$FUZZ_LIST" ]; then
    err "Fuzz list not found: $FUZZ_LIST"
    err "Create it with one harness name per line (e.g., fuzz_compress)"
    exit 1
fi

# Read harness list (skip comments and blank lines)
mapfile -t HARNESSES < <(grep -v '^\s*#' "$FUZZ_LIST" | grep -v '^\s*$')

if [ ${#HARNESSES[@]} -eq 0 ]; then
    warn "No harnesses listed in $FUZZ_LIST — nothing to run"
    exit 0
fi

# Check that harness binaries exist
missing=0
for h in "${HARNESSES[@]}"; do
    if [ ! -x "$BUILD_DIR/$h" ]; then
        err "Harness binary not found: $BUILD_DIR/$h"
        err "Build the fuzz targets first (cmake --build build/fuzz)"
        missing=1
    fi
done
if [ "$missing" -eq 1 ]; then
    exit 1
fi

# --- Build reference library if needed (for differential fuzzer) ---

REF_LIB="$PROJECT_DIR/reference/libbz2_ref.so"
if [[ " ${HARNESSES[*]} " =~ fuzz_diff ]] && [ ! -f "$REF_LIB" ]; then
    log "Building reference libbz2 shared library..."
    REF_SRC="$PROJECT_DIR/reference/bzip2"
    gcc -shared -fPIC -O2 -o "$REF_LIB" \
        "$REF_SRC/blocksort.c" "$REF_SRC/huffman.c" "$REF_SRC/crctable.c" \
        "$REF_SRC/randtable.c" "$REF_SRC/compress.c" "$REF_SRC/decompress.c" \
        "$REF_SRC/bzlib.c" -I"$REF_SRC"
fi

# --- Run harnesses ---

mkdir -p "$OUTPUT_DIR"

log "Running ${#HARNESSES[@]} harnesses (${TIME_PER_HARNESS}s each, max ${MAX_JOBS} parallel)"

PIDS=()
NAMES=()
RESULTS=()
failures=0
start_time=$(date +%s)

run_harness() {
    local name="$1"
    local binary="$BUILD_DIR/$name"
    local corpus="$OUTPUT_DIR/corpus_$name"
    local log_file="$OUTPUT_DIR/${name}.log"

    mkdir -p "$corpus"

    # Select seed directory based on harness type
    local seed_dirs=()
    case "$name" in
        fuzz_compress)
            seed_dirs=("$CORPUS_DIR/compress_seeds")
            ;;
        fuzz_decompress)
            seed_dirs=("$CORPUS_DIR/decompress_seeds" "$CORPUS_DIR/malformed_seeds" "$CORPUS_DIR/bzip2_tests_seeds")
            ;;
        fuzz_streaming)
            seed_dirs=("$CORPUS_DIR/compress_seeds")
            ;;
        fuzz_bufftobuff)
            seed_dirs=("$CORPUS_DIR/compress_seeds")
            ;;
        fuzz_differential|fuzz_diff_streaming)
            seed_dirs=("$CORPUS_DIR/compress_seeds" "$CORPUS_DIR/decompress_seeds" "$CORPUS_DIR/malformed_seeds" "$CORPUS_DIR/multiblock_seeds" "$CORPUS_DIR/bzip2_tests_seeds")
            ;;
        *)
            seed_dirs=("$CORPUS_DIR/compress_seeds")
            ;;
    esac

    # Build seed args
    local seed_args=""
    for sd in "${seed_dirs[@]}"; do
        [ -d "$sd" ] && seed_args="$seed_args $sd"
    done

    # Dictionary for bzip2 format tokens (improves coverage dramatically)
    local dict_arg=""
    [ -f "$SCRIPT_DIR/bzip2.dict" ] && dict_arg="-dict=$SCRIPT_DIR/bzip2.dict"

    # Run with libFuzzer
    "$binary" "$corpus" $seed_args \
        -max_total_time="$TIME_PER_HARNESS" \
        -print_final_stats=1 \
        -jobs=1 \
        -workers=1 \
        $dict_arg \
        > "$log_file" 2>&1

    return $?
}

# Launch harnesses in parallel (up to MAX_JOBS at a time)
active=0
harness_idx=0

while [ $harness_idx -lt ${#HARNESSES[@]} ] || [ $active -gt 0 ]; do
    # Launch new harnesses if under the job limit
    while [ $active -lt $MAX_JOBS ] && [ $harness_idx -lt ${#HARNESSES[@]} ]; do
        h="${HARNESSES[$harness_idx]}"
        log "Starting: $h"
        run_harness "$h" &
        PIDS+=($!)
        NAMES+=("$h")
        active=$((active + 1))
        harness_idx=$((harness_idx + 1))
    done

    # Check total time budget
    now=$(date +%s)
    elapsed=$((now - start_time))
    if [ $elapsed -ge $MAX_TOTAL_TIME ]; then
        warn "Total time budget (${MAX_TOTAL_TIME}s) exceeded — killing remaining harnesses"
        for pid in "${PIDS[@]}"; do
            kill "$pid" 2>/dev/null || true
        done
        break
    fi

    # Wait for any child to finish
    if [ $active -gt 0 ]; then
        wait -n 2>/dev/null || true
        # Check which ones finished
        new_pids=()
        new_names=()
        for i in "${!PIDS[@]}"; do
            if kill -0 "${PIDS[$i]}" 2>/dev/null; then
                new_pids+=("${PIDS[$i]}")
                new_names+=("${NAMES[$i]}")
            else
                wait "${PIDS[$i]}" 2>/dev/null
                exit_code=$?
                if [ $exit_code -ne 0 ]; then
                    err "CRASH: ${NAMES[$i]} exited with code $exit_code"
                    failures=$((failures + 1))
                else
                    log "OK: ${NAMES[$i]}"
                fi
                active=$((active - 1))
            fi
        done
        PIDS=("${new_pids[@]+"${new_pids[@]}"}")
        NAMES=("${new_names[@]+"${new_names[@]}"}")
    fi
done

# Wait for any remaining
for i in "${!PIDS[@]}"; do
    wait "${PIDS[$i]}" 2>/dev/null
    exit_code=$?
    if [ $exit_code -ne 0 ]; then
        err "CRASH: ${NAMES[$i]} exited with code $exit_code"
        failures=$((failures + 1))
    else
        log "OK: ${NAMES[$i]}"
    fi
done

end_time=$(date +%s)
total_time=$((end_time - start_time))

log "Completed in ${total_time}s: ${#HARNESSES[@]} harnesses, $failures failures"

if [ $failures -gt 0 ]; then
    err "$failures harness(es) found crashes or divergences!"
    err "Check logs in: $OUTPUT_DIR/"
    exit 1
fi

exit 0
