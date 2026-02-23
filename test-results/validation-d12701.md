# Validation Report — d127012

**Commit**: d127012 — perf: add software prefetch to decompression BWT pointer chase
**Date**: 2026-02-23
**Validator**: tester (per-commit validation specialist)
**Total validation time**: ~2 minutes

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

Differential testing covers: buffer-to-buffer compression/decompression across all block sizes 1-9, work factors 0/1/30/100/250, cross-decompression, streaming API comparison, error code comparison on invalid/truncated/corrupted inputs, small decompress mode, multi-block data up to 2MB, all 46 files from bzip2-tests repository (38 valid + 8 bad).

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
| fuzz_compress | 52 | 4 | 0 | 11 | 44 | ~11s |
| fuzz_decompress | 485 | 17 | 0 | 0 | — | ~27s |
| fuzz_differential | 485 | 17 | 0 | 0 | 200 | ~27s |
| fuzz_diff_streaming | 485 | 17 | 0 | 0 | 202 | ~27s |
| **Total** | **1,507** | — | **0** | **11** | — | ~30s |

All 4 harnesses ran with ASAN enabled. Zero crashes. Zero divergences (differential and streaming differential harnesses). fuzz_decompress was killed when 30s total budget exceeded; completed with 0 crashes. fuzz_compress found 11 new corpus entries but no bugs.

## Benchmarks

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 18.20 MB/s | 15.56 MB/s | **1.17x** | 77.50 MB/s | 83.01 MB/s | 0.93x |
| text-100k | 5 | 21.80 MB/s | 18.74 MB/s | **1.16x** | 79.60 MB/s | 94.19 MB/s | 0.85x |
| text-100k | 9 | 27.22 MB/s | 22.51 MB/s | **1.21x** | 114.80 MB/s | 119.70 MB/s | 0.96x |
| binary-100k | 1 | 16.28 MB/s | 16.51 MB/s | 0.99x | 33.10 MB/s | 33.49 MB/s | 0.99x |
| binary-100k | 5 | 16.02 MB/s | 17.06 MB/s | 0.94x | 35.35 MB/s | 34.96 MB/s | 1.01x |
| binary-100k | 9 | 16.81 MB/s | 16.99 MB/s | 0.99x | 35.87 MB/s | 34.48 MB/s | 1.04x |
| repeated-100k | 1 | 23.16 MB/s | 13.78 MB/s | **1.68x** | 397.46 MB/s | 399.88 MB/s | 0.99x |
| repeated-100k | 5 | 22.10 MB/s | 16.40 MB/s | **1.35x** | 432.46 MB/s | 398.31 MB/s | 1.09x |
| repeated-100k | 9 | 22.62 MB/s | 13.30 MB/s | **1.70x** | 410.05 MB/s | 401.29 MB/s | 1.02x |
| zeros-100k | 1 | 290.75 MB/s | 302.53 MB/s | 0.96x | 1422.00 MB/s | 583.15 MB/s | **2.44x** |
| zeros-100k | 5 | 329.85 MB/s | 323.60 MB/s | 1.02x | 1453.00 MB/s | 581.46 MB/s | **2.50x** |
| zeros-100k | 9 | 319.93 MB/s | 319.67 MB/s | 1.00x | 1410.00 MB/s | 577.08 MB/s | **2.44x** |

**Compression highlights**: text 1.16x-1.21x, repeated 1.35x-1.70x. Binary at parity (~0.94x-0.99x). Zeros at parity.
**Decompression highlights**: zeros 2.44x-2.50x (batch CRC). **Text decompression regression: 0.85x-0.96x** (was 0.88x-0.98x at f56cb57). The prefetch change to `qbz2_internal.h` modified the BZ_GET_FAST macros to add `__builtin_prefetch` — this disrupted the compiler's CRC inlining, causing the text decompression path to slow down. Binary at parity (0.99x-1.04x).

**Comparison vs f56cb57**: Compression throughput stable. Decompression zeros down slightly (2.44x-2.50x vs 2.63x-2.71x). Text decompression regressed from 0.88x-0.98x to 0.85x-0.96x. The prefetch helps on larger blocks but the CRC inlining disruption offsets it on text.

## Known Issues

| Issue | Severity | Introduced | Status |
|-------|----------|------------|--------|
| Text decompression regression: 0.85x-0.96x vs reference | Medium | d127012 | **FIXED** in 1f048d6 (hybrid CRC) |
| Concatenated bz2 streams: BZ2_bzBuffToBuffDecompress only decompresses first stream | Low | Matches reference behavior | wontfix |
| Binary compression ~0.94x-0.99x vs reference | Low | e6a09d5 | Open — incompressible data has overhead from cleaner code paths |

No known divergences. No known crashes. No pre-existing bugs.

## Summary

Commit d127012 (software prefetch for decompression BWT pointer chase) is **CLEAN** with a **performance regression flag**. All 559 unit tests pass, all 497 differential tests pass with 0 divergences, all 422 ASAN tests pass with 0 violations, 1,507 fuzz executions with 0 crashes and 0 divergences. Correctness is fully maintained. However, text decompression throughput regressed to 0.85x-0.96x vs reference (down from 0.88x-0.98x at f56cb57) due to the `__builtin_prefetch` addition in BZ_GET_FAST macros disrupting the compiler's CRC inlining. This regression was subsequently fixed in commit 1f048d6 (hybrid CRC approach). Zeros decompression remains strong at 2.44x-2.50x. Compression throughput is unaffected.
