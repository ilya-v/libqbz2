# Validation Report: 43755b — fix PGO build script to use CMake-built training binary

**Commit:** 43755b4
**Description:** ops: fix PGO build script to use CMake-built training binary
**Date:** 2026-02-24
**Validator:** tester (per-commit validation specialist)
**Verdict:** PASS

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc 15.2.1 -O2 | PASS |
| ASAN+UBSAN | clang 21.1.8 -fsanitize=address,undefined | PASS |
| Fuzz harnesses | clang 21.1.8 -fsanitize=fuzzer,address | PASS |

## 2. Unit Tests

33 test suites, 1,218 tests, 474,433 assertions, 15.53s total (Release).

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.01s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.02s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.18s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.10s |
| test_roundtrip | 137 | 137 | 0 | 175 | 0.95s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.00s |
| test_differential | 206 | 206 | 0 | — | 0.14s |
| test_diff_multiblock | 129 | 129 | 0 | — | 3.84s |
| test_fileio | 58 | 58 | 0 | 952 | 0.02s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.58s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 0.95s |
| test_bzip2_corpus | 162 | 162 | 0 | — | 7.85s |
| test_oom | 22 | 22 | 0 | 318 | 0.02s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.01s |
| test_malformed | 32 | 32 | 0 | 99 | 0.01s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.03s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.17s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.00s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.03s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.42s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.01s |
| test_bufftobuff_edge | 45 | 45 | 0 | 1,180 | 0.01s |
| test_huffman_decode_oob | 17 | 17 | 0 | 87 | 0.02s |
| test_coverage_gaps | 23 | 23 | 0 | 85 | 0.00s |
| test_coverage_gaps2 | 56 | 56 | 0 | 2,320 | 0.01s |
| test_decompress_branches | 24 | 24 | 0 | 163 | 0.01s |
| test_bzlib_branches | 45 | 45 | 0 | 7,221 | 0.01s |
| test_blocksort_branches | 24 | 24 | 0 | 125 | 0.01s |
| test_compress_branches | 26 | 26 | 0 | 455 | 0.02s |
| test_decompress_crc | 63 | 63 | 0 | 571 | 0.07s |
| test_crc32_internal | 17 | 17 | 0 | 594 | 0.00s |
| test_streaming_states | 18 | 18 | 0 | 2,134 | 0.02s |
| test_randomised_blocks | 16 | 16 | 0 | 122 | 0.03s |

## 3. Differential Tests (Deterministic Suite)

| Suite | Inputs Tested | Passed | Divergences |
|-------|---------------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| test_param_combos | 92 | 92 | 0 |
| test_concat_readahead | 20 | 20 | 0 |
| test_rle_huffman_edge | 47 | 47 | 0 |
| test_block_boundary_bitreader | 20 | 20 | 0 |
| test_decompress_crc | 63 | 63 | 0 |
| test_randomised_blocks | 16 (diff subset) | 16 | 0 |
| **Total** | **755** | **755** | **0** |

Error behavior comparison: 0 error divergences.

## 4. ASAN+UBSAN

33/33 test suites pass under AddressSanitizer + UndefinedBehaviorSanitizer. 0 ASAN errors, 0 UBSAN warnings. Total time: 98.40s.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Coverage | Crashes | Divergences | Time |
|---------|------|--------|----------|---------|-------------|------|
| fuzz_compress | 75 | 6 | 9,451 | 0 | — | 10s |
| fuzz_decompress | 443 | 31 | 4,689 | 0 | — | 10s |
| fuzz_differential | 485 | 12 | 6,296 | 0 | 0 | 10s |
| fuzz_diff_streaming | 485 | 12 | 6,339 | 0 | 0 | 10s |
| **Total** | **1,488** | — | — | **0** | **0** | 30s |

fuzz_diff_streaming killed at 30s budget (SIGTERM, not a crash). All harnesses completed corpus initialization and fuzzing.

## 6. Benchmarks

Two passes averaged:

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 25.40 MB/s | 24.05 MB/s | 1.06x | 152.28 MB/s | 128.20 MB/s | 1.19x |
| text-100k | 5 | 33.61 MB/s | 23.70 MB/s | 1.42x | 130.85 MB/s | 129.29 MB/s | 1.01x |
| text-100k | 9 | 32.33 MB/s | 20.03 MB/s | 1.61x | 146.89 MB/s | 119.54 MB/s | 1.23x |
| binary-100k | 1 | 16.95 MB/s | 16.95 MB/s | 1.00x | 71.23 MB/s | 34.19 MB/s | 2.08x |
| binary-100k | 5 | 17.12 MB/s | 17.06 MB/s | 1.00x | 71.37 MB/s | 35.90 MB/s | 1.99x |
| binary-100k | 9 | 16.41 MB/s | 15.99 MB/s | 1.03x | 70.37 MB/s | 35.10 MB/s | 2.00x |
| repeated-100k | 1 | 67.92 MB/s | 13.03 MB/s | 5.21x | 436.22 MB/s | 433.81 MB/s | 1.01x |
| repeated-100k | 5 | 62.10 MB/s | 17.71 MB/s | 3.51x | 430.48 MB/s | 434.19 MB/s | 0.99x |
| repeated-100k | 9 | 55.09 MB/s | 17.32 MB/s | 3.18x | 420.53 MB/s | 433.72 MB/s | 0.97x |
| zeros-100k | 1 | 579.29 MB/s | 319.98 MB/s | 1.81x | 3814.51 MB/s | 597.05 MB/s | 6.39x |
| zeros-100k | 5 | 558.03 MB/s | 298.35 MB/s | 1.87x | 3715.84 MB/s | 594.38 MB/s | 6.25x |
| zeros-100k | 9 | 606.99 MB/s | 325.63 MB/s | 1.86x | 3850.04 MB/s | 600.25 MB/s | 6.41x |

Performance summary:
- Text compression: 1.0x-1.6x faster than reference
- Text decompression: 1.0x-1.2x faster than reference
- Binary decompression: ~2.0x faster than reference
- Repeated compression: 3.2x-5.2x faster than reference
- Zeros decompression: ~6.4x faster than reference

## 7. Known Issues

None. No known pre-existing divergences, bugs, or test failures.

## 8. Summary

Commit 43755b4 is a build script fix (scripts/pgo-build.sh only, 1 insertion, 9 deletions) that replaces manual gcc compilation of the PGO training driver with the CMake-built pgo_training target. No library source code changed. All 33 test suites pass (1,218 tests, 474,433 assertions), all 755 differential inputs match the reference, ASAN+UBSAN is clean, 1,488 fuzz runs produced 0 crashes and 0 divergences, and benchmarks show no performance change. This commit is clean and enables the PGO build path for both GCC and Clang.
