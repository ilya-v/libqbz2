# Validation Report: d3d790 — remove dead MTF decode fields from decompression state

**Commit:** d3d790a
**Description:** refactor: remove dead MTF decode fields from decompression state
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

31 test suites, 1,146 tests, 1,146 passed, 0 failed, 471,991 assertions, 18.47s total.

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.013s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.018s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.208s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.146s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.046s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_differential | 206 | 206 | 0 | — | 0.14s |
| test_diff_multiblock | 129 | 129 | 0 | — | 4.29s |
| test_fileio | 58 | 58 | 0 | 952 | 0.026s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.706s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.110s |
| test_bzip2_corpus | 162 | 162 | 0 | — | 9.57s |
| test_oom | 22 | 22 | 0 | 318 | 0.020s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.008s |
| test_malformed | 32 | 32 | 0 | 99 | 0.017s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.035s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.096s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.008s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.111s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.698s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.012s |
| test_bufftobuff_edge | 45 | 45 | 0 | 1,180 | 0.005s |
| test_huffman_decode_oob | 17 | 17 | 0 | 87 | 0.025s |
| test_coverage_gaps | 23 | 23 | 0 | 85 | 0.002s |
| test_decompress_branches | 24 | 24 | 0 | 163 | 0.019s |
| test_bzlib_branches | 45 | 45 | 0 | 7,221 | 0.008s |
| test_blocksort_branches | 24 | 24 | 0 | 125 | 0.005s |
| test_compress_branches | 26 | 26 | 0 | 455 | 0.017s |
| test_decompress_crc | 63 | 63 | 0 | 571 | 0.106s |
| test_crc32_internal | 17 | 17 | 0 | 594 | 0.001s |
| test_streaming_states | 18 | 18 | 0 | 2,134 | 0.022s |

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
| **Total** | **739** | **739** | **0** |

Error behavior comparison: differential tests compare both success outputs and error return codes. 0 error divergences.

## 4. ASAN+UBSAN

31/31 test suites pass under AddressSanitizer + UndefinedBehaviorSanitizer. 0 ASAN errors, 0 UBSAN warnings. Total time: 108.72s.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Coverage | Crashes | Divergences | Time |
|---------|------|--------|----------|---------|-------------|------|
| fuzz_compress | 52 | 4 | 9,451 | 0 | — | 10s |
| fuzz_decompress | 443 | 24 | 4,689 | 0 | — | 10s |
| fuzz_differential | 485 | 11 | 6,296 | 0 | 0 | 10s |
| fuzz_diff_streaming | 485 | 11 | 6,339 | 0 | 0 | ~10s (killed at 30s budget) |
| **Total** | **1,465** | — | — | **0** | **0** | 30s |

Note: fuzz_diff_streaming was killed by the 30s total time budget (not a crash). Exit code 143 = SIGTERM. All corpus initialization completed and fuzzing ran for full duration on all 4 harnesses.

## 6. Benchmarks

Two passes averaged:

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 22.84 MB/s | 19.59 MB/s | 1.17x | 124.25 MB/s | 104.03 MB/s | 1.19x |
| text-100k | 5 | 27.80 MB/s | 19.09 MB/s | 1.46x | 121.11 MB/s | 104.25 MB/s | 1.16x |
| text-100k | 9 | 27.59 MB/s | 21.66 MB/s | 1.27x | 123.80 MB/s | 107.16 MB/s | 1.16x |
| binary-100k | 1 | 13.94 MB/s | 13.90 MB/s | 1.00x | 57.40 MB/s | 27.67 MB/s | 2.07x |
| binary-100k | 5 | 14.05 MB/s | 14.23 MB/s | 0.99x | 57.88 MB/s | 28.78 MB/s | 2.01x |
| binary-100k | 9 | 13.64 MB/s | 15.44 MB/s | 0.88x | 66.15 MB/s | 32.89 MB/s | 2.01x |
| repeated-100k | 1 | 62.73 MB/s | 13.16 MB/s | 4.77x | 394.94 MB/s | 402.24 MB/s | 0.98x |
| repeated-100k | 5 | 57.98 MB/s | 16.78 MB/s | 3.45x | 371.23 MB/s | 341.36 MB/s | 1.09x |
| repeated-100k | 9 | 51.92 MB/s | 16.21 MB/s | 3.20x | 389.66 MB/s | 404.30 MB/s | 0.96x |
| zeros-100k | 1 | 541.40 MB/s | 293.50 MB/s | 1.84x | 3595.94 MB/s | 557.14 MB/s | 6.45x |
| zeros-100k | 5 | 591.60 MB/s | 312.58 MB/s | 1.89x | 3584.46 MB/s | 556.83 MB/s | 6.44x |
| zeros-100k | 9 | 579.78 MB/s | 308.63 MB/s | 1.88x | 3521.43 MB/s | 559.71 MB/s | 6.29x |

Performance summary:
- Text compression: 1.0x-1.5x faster than reference
- Text decompression: 1.15x-1.19x faster than reference
- Binary decompression: ~2.0x faster than reference
- Repeated/zeros compression: 3.2x-4.8x faster than reference
- Zeros decompression: ~6.4x faster than reference

## 7. Known Issues

None. No known pre-existing divergences, bugs, or test failures. The CRC batching commit (d392432) was previously reverted at 8728dfb and is not present in this codebase. All Huffman OOB bugs were fixed at 8513bc3. All previous crash bugs have permanent regression tests.

## 8. Summary

Commit d3d790a is a clean refactor that removes two dead fields (yy and ll) from the decompression state struct in qbz2_internal.h. The change is minimal (1 file, 2 insertions, 4 deletions) and introduces no functional change. All 31 test suites pass (1,146 tests, 471,991 assertions), all 739 differential inputs match the reference, ASAN+UBSAN is clean, 1,465 fuzz runs produced 0 crashes and 0 divergences, and benchmarks show no performance regression. This commit is clean.
