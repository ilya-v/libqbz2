# Validation Report — f56cb57

**Commit**: f56cb57 — perf: 64-bit bitstream writer for compression
**Date**: 2026-02-23
**Validator**: tester (per-commit validation specialist)
**Total validation time**: ~2.5 minutes

## Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc 15.2.1 | PASS |
| ASAN+UBSAN | clang 21.1.8 | PASS |
| Fuzz harnesses | clang 21.1.8 (libFuzzer) | PASS |

All three build variants compile with zero warnings under `-Wall -Wextra -Wpedantic`.

## Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.050s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.070s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.467s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.199s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.002s |
| test_roundtrip | 137 | 137 | 0 | 175 | 3.045s |
| test_oom | 22 | 22 | 0 | 318 | 0.026s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.862s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.478s |
| test_fileio | 58 | 58 | 0 | 952 | 0.068s |
| **Total** | **559** | **559** | **0** | **164,960** | **6.3s** |

## Differential Tests (deterministic suite)

| Suite | Tests | Passed | Divergences |
|-------|-------|--------|-------------|
| test_differential (single-block) | 206 | 206 | 0 |
| test_diff_multiblock (multi-block) | 129 | 129 | 0 |
| test_bzip2_corpus (bzip2-tests repo) | 162 | 162 | 0 |
| **Total** | **497** | **497** | **0** |

Differential testing covers: buffer-to-buffer compression/decompression across all block sizes 1-9, work factors 0/1/30/100/250, cross-decompression (compress with libqbz2/decompress with ref and vice versa), streaming API comparison, error code comparison on invalid/truncated/corrupted inputs, small decompress mode, multi-block data up to 2MB, all 46 files from bzip2-tests repository (38 valid + 8 bad).

## ASAN+UBSAN

| Suite | Tests | Passed | Failed | Errors | Time |
|-------|-------|--------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0 | 0.246s |
| test_edge_cases | 67 | 67 | 0 | 0 | 0.611s |
| test_streaming | 30 | 30 | 0 | 0 | 2.502s |
| test_advanced | 40 | 40 | 0 | 0 | 1.912s |
| test_error_paths | 60 | 60 | 0 | 0 | 0.014s |
| test_oom | 22 | 22 | 0 | 0 | 0.494s |
| test_multiblock | 33 | 33 | 0 | 0 | 10.266s |
| test_blocksort_paths | 55 | 55 | 0 | 0 | 14.346s |
| test_fileio | 58 | 58 | 0 | 0 | 0.882s |
| **Total** | **422** | **422** | **0** | **0** | **31.3s** |

Zero ASAN violations. Zero UBSAN violations.

## Quick Fuzz

| Harness | Runs | Exec/sec | Crashes | New Units | Corpus Size | Time |
|---------|------|----------|---------|-----------|-------------|------|
| fuzz_compress | 63 | 5 | 0 | 17 | — | ~10s |
| fuzz_decompress | 485 | 16 | 0 | 0 | — | ~10s |
| fuzz_differential | 485 | 16 | 0 | 0 | — | ~10s |
| fuzz_diff_streaming | 485 | 16 | 0 | 0 | — | ~10s |
| **Total** | **1,518** | — | **0** | **17** | **488** | **~30s** |

All 4 harnesses ran with ASAN enabled. Zero crashes. Zero divergences (differential and streaming differential harnesses). The fuzz_compress harness found 17 new coverage-guided corpus entries but no bugs. Script hit 30s budget limit.

## Benchmarks

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 18.75 MB/s | 15.56 MB/s | **1.21x** | 81.58 MB/s | 83.01 MB/s | 0.98x |
| text-100k | 5 | 22.98 MB/s | 18.74 MB/s | **1.23x** | 82.89 MB/s | 94.19 MB/s | 0.88x |
| text-100k | 9 | 26.39 MB/s | 22.51 MB/s | **1.17x** | 116.99 MB/s | 119.70 MB/s | 0.98x |
| binary-100k | 1 | 16.28 MB/s | 16.51 MB/s | 0.99x | 33.43 MB/s | 33.49 MB/s | 1.00x |
| binary-100k | 5 | 16.02 MB/s | 17.06 MB/s | 0.94x | 34.32 MB/s | 34.96 MB/s | 0.98x |
| binary-100k | 9 | 16.81 MB/s | 16.99 MB/s | 0.99x | 34.96 MB/s | 34.48 MB/s | 1.01x |
| repeated-100k | 1 | 23.16 MB/s | 13.78 MB/s | **1.68x** | 397.46 MB/s | 399.88 MB/s | 0.99x |
| repeated-100k | 5 | 23.55 MB/s | 17.40 MB/s | **1.35x** | 432.46 MB/s | 398.31 MB/s | 1.09x |
| repeated-100k | 9 | 22.62 MB/s | 17.01 MB/s | **1.33x** | 410.05 MB/s | 401.29 MB/s | 1.02x |
| zeros-100k | 1 | 290.75 MB/s | 302.53 MB/s | 0.96x | 1549.56 MB/s | 583.15 MB/s | **2.66x** |
| zeros-100k | 5 | 329.85 MB/s | 323.60 MB/s | 1.02x | 1574.25 MB/s | 581.46 MB/s | **2.71x** |
| zeros-100k | 9 | 319.93 MB/s | 319.67 MB/s | 1.00x | 1519.45 MB/s | 577.08 MB/s | **2.63x** |

**Compression highlights**: text 1.17x-1.23x, repeated 1.33x-1.68x. Binary at parity (~0.94x-0.99x). Zeros at parity.
**Decompression highlights**: zeros 2.63x-2.71x (batch CRC). Other workloads at parity.

**Comparison vs previous (ea4270b)**: Compression speedups on text and repeated data are maintained or slightly improved from the SA-IS+MTF commit. The 64-bit bitstream writer primarily helps throughput on compressible data where more bits are written. Binary data (incompressible) shows no change, as expected.

## Known Issues

| Issue | Severity | Introduced | Status |
|-------|----------|------------|--------|
| Concatenated bz2 streams: BZ2_bzBuffToBuffDecompress only decompresses first stream | Low | Matches reference behavior | wontfix |
| Binary compression ~0.94x-0.99x vs reference | Low | e6a09d5 | Open — incompressible data has overhead from cleaner code paths |

No known divergences. No known crashes. No pre-existing bugs.

## Summary

Commit f56cb57 (64-bit bitstream writer for compression) is **CLEAN**. All 559 unit tests pass, all 497 differential tests pass with 0 divergences, all 422 ASAN tests pass with 0 violations, 1,518 fuzz executions with 0 crashes and 0 divergences. Compression throughput shows meaningful improvements on text (1.17x-1.23x) and repeated data (1.33x-1.68x) compared to reference, with binary at parity. Decompression retains the 2.6x+ speedup on zeros from the batch CRC optimization. The bitstream writer optimization is working as expected — improving write-heavy compression paths without affecting correctness.
