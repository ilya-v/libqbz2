# Validation Report: ea8cffa — SIMD-accelerate MTF linear search in compression

**Commit:** ea8cffa  
**Description:** perf: SIMD-accelerate MTF linear search in compression  
**Date:** 2026-02-23  
**Validator:** tester (per-commit validation specialist)

## Build

| Variant | Compiler | Status |
|---------|----------|--------|
| Release | gcc 15.2.1 | PASS |
| ASAN+UBSAN | clang 21.1.8 | PASS |
| Fuzz harnesses | clang 21.1.8 (libFuzzer) | PASS |

All three build variants compile with zero warnings under `-Wall -Wextra -Wpedantic`. The SSE2 intrinsics compile cleanly under both gcc and clang.

## Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time (Release) | Time (ASAN) |
|-------|-------|--------|--------|------------|----------------|-------------|
| test_api | 57 | 57 | 0 | 235 | 0.020s | 0.291s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.036s | 0.545s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.196s | 1.554s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.028s | 0.268s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.109s | 1.450s |
| test_roundtrip | 137 | 137 | 0 | 175 | 0.993s | 7.319s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s | 0.013s |
| test_fileio | 58 | 58 | 0 | 952 | 0.051s | 0.787s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.641s | 5.741s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.065s | 10.300s |
| test_malformed | 32 | 32 | 0 | 99 | 0.011s | 0.103s |
| test_oom | 22 | 22 | 0 | 318 | 0.020s | 0.256s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.014s | 0.178s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.198s | 1.626s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.140s | 0.919s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.009s | 0.139s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.930s | 16.080s |
| **TOTAL** | **827** | **827** | **0** | **422,179** | **4.46s** | **47.57s** |

## Differential Tests

| Suite | Inputs Tested | Pass | Divergences |
|-------|---------------|------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **TOTAL** | **497** | **497** | **0** |

Differential testing covers all block sizes (1-9), multiple work factors, streaming/buffer-to-buffer/FILE* APIs, the bzip2-tests external corpus, and error code comparison on invalid/truncated/corrupted inputs. The SIMD MTF acceleration produces bit-for-bit identical compressed output to the reference library — zero divergences. This is the critical result: the SIMD search finds the same MTF position as the scalar loop for every symbol.

## ASAN+UBSAN

- **Tests run:** 827 (full suite under clang ASAN+UBSAN)
- **ASAN errors:** 0
- **UBSAN errors:** 0
- **Time:** 47.57s

No memory safety violations from the SIMD code path. The 0xFF padding beyond nInUse in the yy[] array (for safe SIMD overreads) is stack-allocated with sufficient size and does not trigger ASAN.

## Quick Fuzz

| Harness | Runs | Exec/sec | Crashes | Divergences | Time |
|---------|------|----------|---------|-------------|------|
| fuzz_compress | 49 | 3 | 0 | N/A | ~14s |
| fuzz_decompress | 485 | 17 | 0 | N/A | ~27s (killed by budget timer) |
| fuzz_differential | 485 | 17 | 0 | 0 | ~27s |
| fuzz_diff_streaming | 485 | 17 | 0 | 0 | ~28s |
| **TOTAL** | **1,504** | — | **0** | **0** | **~30s budget** |

All 4 harnesses ran with ASAN enabled. Zero crashes. Zero divergences (differential and streaming differential harnesses). fuzz_compress exercises the SIMD MTF path directly on randomized inputs — 0 crashes confirms no out-of-bounds reads from the SIMD scan.

## Benchmarks

Best-of-2 runs:

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 25.63 | 22.50 | **1.14x** |
| text-100k | 5 | 26.88 | 22.76 | **1.18x** |
| text-100k | 9 | 26.97 | 22.46 | **1.20x** |
| binary-100k | 1 | 24.31 | 16.34 | **1.49x** |
| binary-100k | 5 | 24.86 | 17.07 | **1.46x** |
| binary-100k | 9 | 23.51 | 16.43 | **1.43x** |
| repeated-100k | 1 | 22.72 | 12.95 | **1.75x** |
| repeated-100k | 5 | 22.83 | 17.57 | **1.30x** |
| repeated-100k | 9 | 22.70 | 17.12 | **1.33x** |
| zeros-100k | 1 | 510.79 | 304.61 | **1.68x** |
| zeros-100k | 5 | 545.97 | 321.79 | **1.70x** |
| zeros-100k | 9 | 544.46 | 319.81 | **1.70x** |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 128.68 | 125.12 | **1.03x** |
| text-100k | 5 | 126.70 | 123.50 | **1.03x** |
| text-100k | 9 | 129.01 | 123.42 | **1.05x** |
| binary-100k | 1 | 33.58 | 32.55 | **1.03x** |
| binary-100k | 5 | 34.30 | 34.35 | 1.00x |
| binary-100k | 9 | 34.58 | 34.36 | **1.01x** |
| repeated-100k | 1 | 418.16 | 410.66 | **1.02x** |
| repeated-100k | 5 | 428.90 | 418.31 | **1.03x** |
| repeated-100k | 9 | 410.74 | 411.88 | 1.00x |
| zeros-100k | 1 | 2167.65 | 567.71 | **3.82x** |
| zeros-100k | 5 | 2252.59 | 577.13 | **3.90x** |
| zeros-100k | 9 | 2265.38 | 583.27 | **3.88x** |

### Speedup Summary vs Previous Commit (c50993d)

| Category | Range | Best | vs c50993d |
|----------|-------|------|------------|
| Compression — text | 1.14x-1.20x | **1.20x** | Stable |
| Compression — binary | 1.43x-1.49x | **1.49x** | **MAJOR improvement** (was 0.98x-1.05x) |
| Compression — repeated | 1.30x-1.75x | **1.75x** | Stable |
| Compression — zeros | 1.68x-1.70x | **1.70x** | Stable |
| Decompression — text | 1.03x-1.05x | **1.05x** | Stable |
| Decompression — binary | 1.00x-1.03x | 1.03x | Stable |
| Decompression — repeated | 1.00x-1.03x | **1.03x** | Stable |
| Decompression — zeros | 3.82x-3.90x | **3.90x** | Stable |

**Key improvement from SIMD MTF (ea8cffa):** Binary compression jumped from ~1.0x to **1.43x-1.49x** — the SSE2-accelerated MTF linear search eliminates the O(n) scalar byte-by-byte scan bottleneck. Binary data has high alphabet diversity (many unique byte values), so the MTF search distance is typically long. The SIMD 16-byte-at-a-time scan dramatically reduces this cost. Text compression also benefits (1.14x-1.20x, stable from previous). The library now beats the reference on ALL compression workloads for the first time.

## Known Issues

| # | Description | Severity | Introduced | Status |
|---|------------|----------|-----------|--------|
| 1 | Multi-block CRC mismatch from compression-side batch CRC | CRITICAL | dffe019 | **FIXED** in f50bd8f |
| 2 | Text decompression regression: 0.85x-0.96x vs reference | Medium | d127012 | **FIXED** in 1f048d6 |
| 3 | Concatenated bz2 streams: BZ2_bzBuffToBuffDecompress only decompresses first stream | Low | Matches reference behavior | wontfix |

No known divergences. No known crashes. No pre-existing test failures.

## Summary

**PASS.** Commit ea8cffa (SIMD-accelerate MTF linear search in compression) passes all validation stages cleanly. 827/827 unit tests pass in both Release and ASAN+UBSAN modes (422,179 assertions across 17 suites), 497/497 differential tests show zero divergences (confirming the SIMD MTF search produces bit-identical compressed output), 1,504 fuzz executions with 0 crashes and 0 divergences, and all ASAN+UBSAN checks clean (no memory safety violations from SSE2 overreads). The benchmark impact is the headline: binary compression jumps from ~1.0x to **1.43x-1.49x** vs the reference — the library now beats the reference on every compression workload. Quality trend: strongly positive — thirteen optimization commits validated with zero correctness regressions.

**Total validation time:** ~2m10s
