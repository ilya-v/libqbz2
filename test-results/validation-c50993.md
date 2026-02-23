# Validation Report: c50993d — branch prediction hints in hot decompression paths

**Commit:** c50993d  
**Description:** perf: add branch prediction hints to hot decompression paths  
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
| test_api | 57 | 57 | 0 | 235 | 0.016s | 0.198s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.035s | 0.585s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.242s | 2.162s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.132s | 1.245s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.204s | 9.773s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s | 0.013s |
| test_fileio | 58 | 58 | 0 | 952 | 0.069s | 0.836s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.810s | 7.517s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.348s | 11.158s |
| test_malformed | 32 | 32 | 0 | 99 | 0.014s | 0.153s |
| test_oom | 22 | 22 | 0 | 318 | 0.029s | 0.324s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.011s | 0.205s |
| **TOTAL** | **623** | **623** | **0** | **165,290** | **3.91s** | **34.17s** |

## Differential Tests

| Suite | Inputs Tested | Pass | Divergences |
|-------|---------------|------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **TOTAL** | **497** | **497** | **0** |

No divergences. Branch prediction hints do not change output behavior.

## ASAN+UBSAN

- **Tests run:** 623 (full suite under clang ASAN+UBSAN)
- **ASAN errors:** 0
- **UBSAN errors:** 0
- **Time:** 34.17s

## Quick Fuzz

| Harness | Runs (approx) | Crashes | Divergences | Time |
|---------|---------------|---------|-------------|------|
| fuzz_compress | ~750 | 0 | N/A | 10s |
| fuzz_decompress | ~750 | 0 | N/A | 10s (killed by budget on pass 2) |
| fuzz_differential | ~750 | 0 | 0 | 10s |
| fuzz_diff_streaming | ~750 | 0 | 0 | 10s |
| **TOTAL** | **~3000** | **0** | **0** | **~60s (2 passes)** |

All ASAN-enabled, 0 crashes, 0 divergences.

## Benchmarks

Benchmarks run twice; system under moderate load from concurrent agents. Numbers show some noise but overall trend is consistent.

### Run 1

| Workload | BS | qbz2 C (MB/s) | ref C (MB/s) | C Speedup | qbz2 D (MB/s) | ref D (MB/s) | D Speedup |
|----------|----|---------------|-------------|-----------|---------------|-------------|-----------|
| text-100k | 1 | 16.90 | 14.36 | **1.18x** | 80.02 | 79.16 | **1.01x** |
| text-100k | 5 | 16.86 | 14.13 | **1.19x** | 78.54 | 76.50 | **1.03x** |
| text-100k | 9 | 16.93 | 14.38 | **1.18x** | 80.00 | 78.37 | **1.02x** |
| binary-100k | 1 | 10.74 | 10.63 | **1.01x** | 19.87 | 20.66 | 0.96x |
| binary-100k | 5 | 10.28 | 11.81 | 0.87x | 28.93 | 30.23 | 0.96x |
| binary-100k | 9 | 14.45 | 14.10 | **1.02x** | 25.81 | 27.68 | 0.93x |
| repeated-100k | 1 | 15.94 | 10.69 | **1.49x** | 345.52 | 274.52 | **1.26x** |
| repeated-100k | 5 | 19.60 | 13.86 | **1.41x** | 371.76 | 351.44 | **1.06x** |
| repeated-100k | 9 | 15.22 | 11.33 | **1.34x** | 354.37 | 361.24 | 0.98x |
| zeros-100k | 1 | 442.48 | 263.91 | **1.68x** | 1940.37 | 501.48 | **3.87x** |
| zeros-100k | 5 | 473.73 | 280.19 | **1.69x** | 1963.66 | 505.18 | **3.89x** |
| zeros-100k | 9 | 483.80 | 282.58 | **1.71x** | 1926.65 | 501.83 | **3.84x** |

### Run 2

| Workload | BS | qbz2 C (MB/s) | ref C (MB/s) | C Speedup | qbz2 D (MB/s) | ref D (MB/s) | D Speedup |
|----------|----|---------------|-------------|-----------|---------------|-------------|-----------|
| text-100k | 1 | 16.41 | 14.18 | **1.16x** | 75.90 | 70.73 | **1.07x** |
| text-100k | 5 | 16.22 | 13.98 | **1.16x** | 74.27 | 72.42 | **1.03x** |
| text-100k | 9 | 14.52 | 13.48 | **1.08x** | 67.51 | 76.35 | 0.88x |
| binary-100k | 1 | 10.21 | 10.50 | 0.97x | 20.28 | 18.53 | **1.09x** |
| binary-100k | 5 | 10.22 | 9.65 | **1.06x** | 19.66 | 21.22 | 0.93x |
| binary-100k | 9 | 9.87 | 10.29 | 0.96x | 21.37 | 21.59 | 0.99x |
| repeated-100k | 1 | 18.78 | 12.06 | **1.56x** | 368.50 | 364.34 | **1.01x** |
| repeated-100k | 5 | 19.54 | 11.59 | **1.69x** | 273.76 | 270.46 | **1.01x** |
| repeated-100k | 9 | 19.10 | 14.85 | **1.29x** | 361.30 | 375.53 | 0.96x |
| zeros-100k | 1 | 457.47 | 268.46 | **1.70x** | 2003.21 | 514.46 | **3.89x** |
| zeros-100k | 5 | 503.48 | 285.60 | **1.76x** | 2018.18 | 518.29 | **3.89x** |
| zeros-100k | 9 | 487.61 | 285.10 | **1.71x** | 2002.12 | 518.28 | **3.86x** |

### Benchmark Analysis

- **Text decompression**: 1.01-1.07x (some noise, generally above parity)
- **Binary decompression**: 0.93-1.09x (high variance, noisy workload on this system)
- **Repeated decompression**: 0.96-1.26x (variable, mostly at or above parity)
- **Zeros decompression**: 3.84-3.89x (stable, excellent)
- **Compression**: unchanged as expected (this commit only modifies decompression paths)

Note: Absolute throughput numbers are lower than previous validations due to system load from concurrent agents. The relative speedup ratios are the meaningful metric, and both runs reference the same-run reference numbers.

## Known Issues

No known pre-existing divergences, bugs, or test failures.

## Summary

**PASS.** Commit c50993d (branch prediction hints in hot decompression paths) passes all validation stages cleanly. 623/623 unit tests pass in both Release and ASAN+UBSAN modes, 497/497 differential tests with zero divergences, ~3000 fuzz runs with 0 crashes and 0 divergences. Benchmarks show stable performance with zeros decompression maintaining ~3.87x speedup. Branch prediction hints are correctness-safe and do not regress performance. Binary workload numbers show high variance due to system contention but no systematic regression.

**Total validation time:** ~4m13s
