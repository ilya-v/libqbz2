# Validation Report: 9dbcccd — batch CRC for compression run encoding

**Commit:** 9dbcccd  
**Description:** perf: batch CRC for compression run encoding  
**Date:** 2026-02-23  
**Validator:** tester (per-commit validation specialist)

## Build

| Variant | Compiler | Status |
|---------|----------|--------|
| Release | gcc 15.2.1 | PASS |
| ASAN+UBSAN | clang 21.1.8 | PASS |
| Fuzz harnesses | clang 21.1.8 (via run-quick-fuzz.sh) | PASS |

## Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time (Release) | Time (ASAN) |
|-------|-------|--------|--------|------------|----------------|-------------|
| test_api | 57 | 57 | 0 | 235 | 0.014s | 0.222s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.036s | 0.497s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.226s | 1.915s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.125s | 1.105s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.241s | 9.213s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s | 0.010s |
| test_fileio | 58 | 58 | 0 | 952 | 0.049s | 0.786s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.764s | 6.955s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.232s | 10.553s |
| test_malformed | 32 | 32 | 0 | 99 | 0.013s | 0.124s |
| test_oom | 22 | 22 | 0 | 318 | 0.023s | 0.290s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.014s | 0.168s |
| **TOTAL** | **623** | **623** | **0** | **165,290** | **3.74s** | **31.84s** |

## Differential Tests

| Suite | Inputs Tested | Pass | Divergences |
|-------|---------------|------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **TOTAL** | **497** | **497** | **0** |

Differential testing covers all block sizes (1-9), multiple work factors, streaming/buffer-to-buffer/FILE* APIs, and the bzip2-tests external corpus. Compressed output is bit-for-bit identical to reference libbz2 — the batch CRC optimization produces identical CRC values to the per-byte loop.

## ASAN+UBSAN

- **Tests run:** 623 (full suite under clang ASAN+UBSAN)
- **ASAN errors:** 0
- **UBSAN errors:** 0
- **Time:** 31.84s

## Quick Fuzz

| Harness | Runs (approx) | Crashes | Divergences | Time |
|---------|---------------|---------|-------------|------|
| fuzz_compress | ~750 | 0 | N/A | 10s |
| fuzz_decompress | ~750 | 0 | N/A | 10s (killed by budget timer on 2nd pass) |
| fuzz_differential | ~750 | 0 | 0 | 10s |
| fuzz_diff_streaming | ~750 | 0 | 0 | 10s |
| **TOTAL** | **~3000** | **0** | **0** | **~60s (2 passes)** |

Fuzz script ran 2 complete passes within its 30s budget per pass. fuzz_decompress was killed by the budget timer on the second pass (not an error). All ASAN-enabled, 0 crashes, 0 divergences.

## Benchmarks

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|----|--------------|-------------|-----------|-----------------|----------------|-----------|
| text-100k | 1 | 25.78 MB/s | 21.54 MB/s | **1.20x** | 120.60 MB/s | 118.57 MB/s | **1.02x** |
| text-100k | 5 | 26.08 MB/s | 21.47 MB/s | **1.21x** | 118.61 MB/s | 119.51 MB/s | 0.99x |
| text-100k | 9 | 25.63 MB/s | 21.71 MB/s | **1.18x** | 124.33 MB/s | 119.61 MB/s | **1.04x** |
| binary-100k | 1 | 16.54 MB/s | 16.49 MB/s | 1.00x | 32.09 MB/s | 32.28 MB/s | 0.99x |
| binary-100k | 5 | 16.55 MB/s | 16.30 MB/s | **1.02x** | 33.29 MB/s | 33.48 MB/s | 0.99x |
| binary-100k | 9 | 12.44 MB/s | 11.58 MB/s | **1.07x** | 33.34 MB/s | 33.00 MB/s | **1.01x** |
| repeated-100k | 1 | 17.32 MB/s | 12.73 MB/s | **1.36x** | 408.04 MB/s | 398.85 MB/s | **1.02x** |
| repeated-100k | 5 | 22.23 MB/s | 16.68 MB/s | **1.33x** | 404.34 MB/s | 392.67 MB/s | **1.03x** |
| repeated-100k | 9 | 21.07 MB/s | 16.03 MB/s | **1.31x** | 388.53 MB/s | 393.42 MB/s | 0.99x |
| zeros-100k | 1 | 432.43 MB/s | 280.06 MB/s | **1.54x** | 2107.38 MB/s | 546.01 MB/s | **3.86x** |
| zeros-100k | 5 | 526.99 MB/s | 298.70 MB/s | **1.76x** | 2142.83 MB/s | 548.46 MB/s | **3.91x** |
| zeros-100k | 9 | 527.82 MB/s | 306.46 MB/s | **1.72x** | 2125.34 MB/s | 550.76 MB/s | **3.86x** |

### Benchmark Comparison vs Previous Commit (b6f97e2)

Compression speedups are stable or slightly improved:
- Text compression: 1.18-1.21x (previously 1.18-1.21x) — no change
- Repeated compression: 1.31-1.36x (previously 1.31-1.33x) — marginal improvement at bs=1
- Zeros compression: 1.54-1.76x (previously 1.54-1.76x) — stable
- Binary compression: 1.00-1.07x (previously 1.00-1.07x) — stable

Decompression unchanged (expected — this is a compression-only change).

## Known Issues

No known pre-existing divergences, bugs, or test failures. All prior known issues have been resolved in previous commits.

## Summary

**PASS.** Commit 9dbcccd (batch CRC for compression run encoding) passes all validation stages cleanly. 623/623 unit tests pass in both Release and ASAN+UBSAN modes, 497/497 differential tests show zero divergences (confirming the batch CRC produces identical results to the per-byte CRC loop), ~3000 fuzz runs with 0 crashes and 0 divergences, and benchmarks show stable or marginally improved compression throughput. The optimization is correctness-safe.

**Total validation time:** ~3m52s (ASAN suite takes ~32s due to blocksort paths and roundtrip tests under instrumentation).
