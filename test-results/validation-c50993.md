# Validation Report: c50993d — branch prediction hints for hot decompression paths

**Commit:** c50993d  
**Description:** perf: add branch prediction hints to hot decompression paths  
**Date:** 2026-02-23  
**Validator:** tester (per-commit validation specialist)

## Build

| Variant | Compiler | Status |
|---------|----------|--------|
| Release | gcc 15.2.1 | PASS |
| ASAN+UBSAN | clang 21.1.8 | PASS |
| Fuzz harnesses | clang 21.1.8 (libFuzzer) | PASS |

All three build variants compile with zero warnings under `-Wall -Wextra -Wpedantic`.

## Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time (Release) | Time (ASAN) |
|-------|-------|--------|--------|------------|----------------|-------------|
| test_api | 57 | 57 | 0 | 235 | 0.017s | 0.188s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.037s | 0.517s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.222s | 2.059s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.149s | 1.127s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.167s | 9.259s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s | 0.014s |
| test_fileio | 58 | 58 | 0 | 952 | 0.052s | 0.786s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.742s | 6.953s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.198s | 10.186s |
| test_malformed | 32 | 32 | 0 | 99 | 0.019s | 0.128s |
| test_oom | 22 | 22 | 0 | 318 | 0.025s | 0.297s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.011s | 0.200s |
| **TOTAL** | **623** | **623** | **0** | **165,290** | **3.64s** | **31.71s** |

## Differential Tests

| Suite | Inputs Tested | Pass | Divergences |
|-------|---------------|------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **TOTAL** | **497** | **497** | **0** |

Differential testing covers all block sizes (1-9), multiple work factors, streaming/buffer-to-buffer/FILE* APIs, and the bzip2-tests external corpus. Error behavior tested: when both libraries reject an input, error codes are compared. Zero divergences — branch prediction hints produce identical output and identical error behavior.

## ASAN+UBSAN

- **Tests run:** 623 (full suite under clang ASAN+UBSAN)
- **ASAN errors:** 0
- **UBSAN errors:** 0
- **Time:** 31.71s

## Quick Fuzz

| Harness | Runs | Exec/sec | Crashes | Divergences | Time |
|---------|------|----------|---------|-------------|------|
| fuzz_compress | 49 | 4 | 0 | N/A | ~12s |
| fuzz_decompress | 485 | 17 | 0 | N/A | ~27s (killed by budget timer) |
| fuzz_differential | 485 | 18 | 0 | 0 | ~26s |
| fuzz_diff_streaming | 485 | 17 | 0 | 0 | ~27s |
| **TOTAL** | **1,504** | — | **0** | **0** | **~30s budget** |

All 4 harnesses ran with ASAN enabled. Zero crashes. Zero divergences (differential and streaming differential harnesses). fuzz_decompress was killed by the 30s budget timer on the second pass (not an error — expected behavior).

## Benchmarks

Best-of-3 runs (system under varying load, so some variance across runs):

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 21.26 | 19.36 | **1.10x** |
| text-100k | 5 | 26.94 | 23.02 | **1.17x** |
| text-100k | 9 | 27.82 | 22.28 | **1.25x** |
| binary-100k | 1 | 16.75 | 17.07 | 0.98x |
| binary-100k | 5 | 16.96 | 16.18 | **1.05x** |
| binary-100k | 9 | 16.24 | 16.37 | 0.99x |
| repeated-100k | 1 | 23.44 | 13.44 | **1.74x** |
| repeated-100k | 5 | 23.43 | 17.26 | **1.36x** |
| repeated-100k | 9 | 23.00 | 16.85 | **1.37x** |
| zeros-100k | 1 | 514.51 | 313.05 | **1.64x** |
| zeros-100k | 5 | 530.61 | 313.95 | **1.69x** |
| zeros-100k | 9 | 521.21 | 316.47 | **1.65x** |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 127.42 | 123.80 | **1.03x** |
| text-100k | 5 | 129.28 | 126.81 | **1.02x** |
| text-100k | 9 | 125.66 | 124.02 | **1.01x** |
| binary-100k | 1 | 32.98 | 32.74 | **1.01x** |
| binary-100k | 5 | 33.58 | 33.64 | 1.00x |
| binary-100k | 9 | 34.22 | 34.15 | 1.00x |
| repeated-100k | 1 | 421.16 | 413.67 | **1.02x** |
| repeated-100k | 5 | 427.14 | 416.71 | **1.03x** |
| repeated-100k | 9 | 415.14 | 424.89 | 0.98x |
| zeros-100k | 1 | 2228.61 | 582.38 | **3.83x** |
| zeros-100k | 5 | 2253.11 | 574.99 | **3.92x** |
| zeros-100k | 9 | 2234.77 | 587.84 | **3.80x** |

### Speedup Summary vs Previous Commit (a8609e9)

| Category | Range | Best | vs a8609e9 |
|----------|-------|------|------------|
| Compression — text | 1.10x-1.25x | **1.25x** | Stable (was 1.17x-1.56x; noise-dominated at BS1) |
| Compression — repeated | 1.36x-1.74x | **1.74x** | Stable |
| Compression — binary | 0.98x-1.05x | 1.05x | Stable (at parity) |
| Compression — zeros | 1.64x-1.69x | **1.69x** | Stable (was 1.67x-1.74x) |
| Decompression — text | 1.01x-1.03x | **1.03x** | Stable |
| Decompression — binary | 1.00x-1.01x | 1.01x | Stable |
| Decompression — repeated | 0.98x-1.03x | **1.03x** | Stable |
| Decompression — zeros | 3.80x-3.92x | **3.92x** | Stable (was 3.88x-3.90x) |

The branch prediction hints are expected to have marginal impact on microbenchmarks — the main benefit is in hot loop code layout which is more visible in sustained workloads. No measurable regression and no measurable improvement in this benchmark suite (within noise). This is expected for `__builtin_expect` annotations which primarily help the compiler's static branch predictor.

## Known Issues

| # | Description | Severity | Introduced | Status |
|---|------------|----------|-----------|--------|
| 1 | Multi-block CRC mismatch from compression-side batch CRC | CRITICAL | dffe019 | **FIXED** in f50bd8f |
| 2 | Text decompression regression: 0.85x-0.96x vs reference | Medium | d127012 | **FIXED** in 1f048d6 |
| 3 | Concatenated bz2 streams: BZ2_bzBuffToBuffDecompress only decompresses first stream | Low | Matches reference behavior | wontfix |
| 4 | Binary compression ~0.98x-1.05x vs reference | Low | e6a09d5 | Open — incompressible data at parity |

No known divergences. No known crashes. No pre-existing test failures.

## Summary

**PASS.** Commit c50993d (branch prediction hints for hot decompression paths) passes all validation stages cleanly. 623/623 unit tests pass in both Release and ASAN+UBSAN modes (165,290 assertions across 12 suites), 497/497 differential tests show zero divergences, 1,504 fuzz executions with 0 crashes and 0 divergences, and all ASAN+UBSAN checks clean. Benchmarks show no regressions and no measurable improvement — this is expected for `__builtin_expect` annotations which primarily help the compiler's code layout decisions rather than producing large throughput changes on short benchmarks. The commit changes only `__builtin_expect` wrappers on existing conditionals in `src/bzlib.c` and `src/qbz2_internal.h`, with zero algorithmic changes, so the correctness risk is negligible and the results confirm this.

**Total validation time:** ~2m15s
