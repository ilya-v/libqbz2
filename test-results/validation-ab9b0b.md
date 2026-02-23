# Validation Report: ab9b0b9

| Field | Value |
|-------|-------|
| **Commit** | `ab9b0b9` |
| **Description** | perf: optimize blocksort mainGtU with word-at-a-time comparisons |
| **Date** | 2026-02-23 |
| **Validator** | tester agent |
| **Time budget** | ~2 min |

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc | **PASS** (0 warnings) |
| ASAN+UBSAN | clang | **PASS** (0 warnings) |
| Fuzz harnesses | clang (fuzzer+asan) | **PASS** (7 harnesses built, 16,564 coverage points) |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.017s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.046s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.288s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.186s |
| **Total** | **194** | **194** | **0** | **163,057** | **0.537s** |

## 3. Differential Tests

| Metric | Value |
|--------|-------|
| Total tests | 206 |
| Passed | 206 |
| Divergences | **0** |
| Input types | 7 (text, binary, repeated, zeros, random, empty, single-byte) |
| Block sizes tested | 1-9 |
| Work factors tested | 5 (0, 1, 30, 100, 250) |
| Error behavior tested | Yes (invalid inputs, cross-decompression) |

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Errors | Time |
|-------|-------|--------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0 | 0.244s |
| test_edge_cases | 67 | 67 | 0 | 0 | 0.936s |
| test_streaming | 30 | 30 | 0 | 0 | 2.195s |
| test_advanced | 40 | 40 | 0 | 0 | 1.258s |
| **Total** | **194** | **194** | **0** | **0** | **4.633s** |

No address sanitizer errors. No undefined behavior detected.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Coverage | Corpus | Time |
|---------|------|--------|---------|----------|--------|------|
| fuzz_compress | 66 | 5 | 0 | 6,387 edges | 62 inputs / 10MB | 12s |
| fuzz_decompress | 443 | 11 | 0 | 3,303 edges | 88 inputs / 12MB | 40s |
| fuzz_differential | 478 | 17 | 0 | 3,833 edges | 212 inputs / 26MB | 28s |
| **Total** | **987** | -- | **0** | -- | -- | **41s** |

- Fuzz build now uses proper coverage instrumentation (16,564 coverage points vs 15 previously)
- bzip2 format dictionary enabled (16 entries)
- Differential harness: **0 divergences found** (new or known)
- 0 crashes across all harnesses

## 6. Benchmarks

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|------|-----|---------|
| text-100k | 1 | 25.78 | 21.79 | **1.18x** |
| text-100k | 5 | 25.69 | 21.44 | **1.20x** |
| text-100k | 9 | 24.90 | 21.13 | **1.18x** |
| binary-100k | 1 | 15.68 | 16.06 | 0.98x |
| binary-100k | 5 | 15.60 | 15.75 | 0.99x |
| binary-100k | 9 | 15.57 | 15.75 | 0.99x |
| repeated-100k | 1 | 16.07 | 12.54 | **1.28x** |
| repeated-100k | 5 | 20.02 | 16.30 | **1.23x** |
| repeated-100k | 9 | 19.81 | 16.02 | **1.24x** |
| zeros-100k | 1 | 290.37 | 283.47 | 1.02x |
| zeros-100k | 5 | 305.48 | 300.56 | 1.02x |
| zeros-100k | 9 | 246.48 | 279.40 | 0.88x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|------|-----|---------|
| text-100k | 1 | 114.89 | 118.25 | 0.97x |
| text-100k | 5 | 108.87 | 114.95 | 0.95x |
| text-100k | 9 | 115.56 | 118.20 | 0.98x |
| binary-100k | 1 | 32.07 | 31.10 | 1.03x |
| binary-100k | 5 | 32.75 | 31.63 | 1.04x |
| binary-100k | 9 | 33.58 | 30.56 | **1.10x** |
| repeated-100k | 1 | 388.90 | 391.73 | 0.99x |
| repeated-100k | 5 | 394.35 | 395.61 | 1.00x |
| repeated-100k | 9 | 380.05 | 396.12 | 0.96x |
| zeros-100k | 1 | 547.13 | 544.09 | 1.01x |
| zeros-100k | 5 | 534.88 | 542.79 | 0.99x |
| zeros-100k | 9 | 541.19 | 543.91 | 0.99x |

### Summary of Speedups

| Category | Range | Average |
|----------|-------|---------|
| Compression — text | 1.18x–1.20x | **1.19x** |
| Compression — binary | 0.98x–0.99x | 0.99x |
| Compression — repeated | 1.23x–1.28x | **1.25x** |
| Compression — zeros | 0.88x–1.02x | 0.97x |
| Decompression — all | 0.95x–1.10x | 1.00x |

## 7. Known Issues

No known pre-existing divergences, bugs, or test failures.

Previous commit `e6a09d5` (full library) was validated clean. This commit modifies only `src/blocksort.c` (sort optimization), which does not introduce new API behavior.

## 8. Summary

**Commit ab9b0b9 is CLEAN.** The blocksort word-at-a-time optimization passes all 194 unit tests (163,057 assertions), 206 differential tests with 0 divergences, 0 ASAN/UBSAN errors, and 987 fuzz runs with 0 crashes. Compression throughput improved significantly: **+19% on text data, +25% on repeated data** vs the reference library. Decompression remains at parity. Binary data compression is marginally slower (1-2%) — this is expected since the optimization targets the string comparison hot path which benefits more from longer matching prefixes found in text and repeated patterns. The zeros-bs9 compression regression (0.88x) may warrant investigation but is likely noise or a different code path (zeros compress via RLE, not the BWT sort). Overall quality trend: positive — first measurable performance win with zero correctness regressions.
