# Validation Report: f50bd8f

| Field | Value |
|-------|-------|
| **Commit** | `f50bd8f` |
| **Description** | fix: revert compression-side batch CRC that broke multi-block streams |
| **Date** | 2026-02-23 |
| **Validator** | tester agent |
| **Time budget** | ~2 min |
| **RESULT** | **PASS** |

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc | **PASS** (0 warnings) |
| ASAN+UBSAN | clang | **PASS** (0 warnings) |
| Fuzz harnesses | clang (fuzzer+asan) | **PASS** (7 harnesses built, 16,623 coverage points) |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.018s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.045s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.226s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.132s |
| **Total** | **194** | **194** | **0** | **163,057** | **0.421s** |

All 12 tests that failed on dffe019 (multi-block data) now pass.

## 3. Differential Tests

| Metric | Value |
|--------|-------|
| Total tests | 206 |
| Passed | 206 |
| Divergences | **0** |
| Input types | 7 (text, binary, repeated, zeros, random, empty, single-byte) |
| Block sizes tested | 1-9 |
| Work factors tested | 5 (0, 1, 30, 100, 250) |
| Error behavior tested | Yes |

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Errors | Time |
|-------|-------|--------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0 | 0.216s |
| test_edge_cases | 67 | 67 | 0 | 0 | 0.687s |
| test_streaming | 30 | 30 | 0 | 0 | 2.024s |
| test_advanced | 40 | 40 | 0 | 0 | 1.172s |
| **Total** | **194** | **194** | **0** | **0** | **4.099s** |

No address sanitizer errors. No undefined behavior. No leaks.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Coverage | Corpus | Time |
|---------|------|--------|---------|----------|--------|------|
| fuzz_compress | 66 | 6 | 0 | 6,417 edges | 61 inputs / 10MB | 11s |
| fuzz_decompress | 443 | 12 | 0 | 3,333 edges | 88 inputs / 13MB | 36s |
| fuzz_differential | 478 | 20 | 0 | 3,833 edges | 215 inputs / 26MB | 23s |
| **Total** | **987** | -- | **0** | -- | -- | **36s** |

- Differential harness: **0 divergences** (new or known). The crash from dffe019 is resolved.
- 0 crashes across all harnesses.

## 6. Benchmarks

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup | vs ab9b0b9 |
|----------|----|------|-----|---------|------------|
| text-100k | 1 | 18.57 | 17.59 | 1.06x | was 1.18x |
| text-100k | 5 | 27.22 | 21.83 | **1.25x** | was 1.20x |
| text-100k | 9 | 18.48 | 15.44 | **1.20x** | was 1.18x |
| binary-100k | 1 | 15.52 | 15.49 | 1.00x | was 0.98x |
| binary-100k | 5 | 16.95 | 17.04 | 0.99x | was 0.99x |
| binary-100k | 9 | 16.54 | 16.73 | 0.99x | was 0.99x |
| repeated-100k | 1 | 16.40 | 13.30 | **1.23x** | was 1.28x |
| repeated-100k | 5 | 21.48 | 17.59 | **1.22x** | was 1.23x |
| repeated-100k | 9 | 19.77 | 16.51 | **1.20x** | was 1.24x |
| zeros-100k | 1 | 304.72 | 257.02 | **1.19x** | was 1.02x |
| zeros-100k | 5 | 311.34 | 308.30 | 1.01x | was 1.02x |
| zeros-100k | 9 | 310.64 | 314.29 | 0.99x | was 0.88x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup | vs ab9b0b9 |
|----------|----|------|-----|---------|------------|
| text-100k | 1 | 120.13 | 126.42 | 0.95x | was 0.97x |
| text-100k | 5 | 111.78 | 114.62 | 0.98x | was 0.95x |
| text-100k | 9 | 115.46 | 125.28 | 0.92x | was 0.98x |
| binary-100k | 1 | 33.79 | 33.69 | 1.00x | was 1.03x |
| binary-100k | 5 | 34.25 | 34.61 | 0.99x | was 1.04x |
| binary-100k | 9 | 34.05 | 34.83 | 0.98x | was 1.10x |
| repeated-100k | 1 | 408.77 | 421.46 | 0.97x | was 0.99x |
| repeated-100k | 5 | 425.68 | 419.73 | 1.01x | was 1.00x |
| repeated-100k | 9 | 401.61 | 411.70 | 0.98x | was 0.96x |
| zeros-100k | 1 | 1471.37 | 564.10 | **2.61x** | was 1.01x |
| zeros-100k | 5 | 1531.73 | 568.91 | **2.69x** | was 0.99x |
| zeros-100k | 9 | 1228.82 | 543.69 | **2.26x** | was 0.99x |

### Performance Summary

| Category | Range | Trend vs previous |
|----------|-------|-------------------|
| Compression -- text | 1.06x--1.25x | Blocksort optimization retained |
| Compression -- repeated | 1.20x--1.23x | Blocksort optimization retained |
| Compression -- zeros | 0.99x--1.19x | Slight improvement |
| Decompression -- zeros | **2.26x--2.69x** | NEW: batch CRC at return_notr |
| Decompression -- other | 0.92x--1.01x | Parity |

The decompression-side batch CRC (kept from dffe019) delivers a massive **2.3-2.7x speedup on zeros decompression**. This is the retained benefit from the CRC optimization. Compression side is back to blocksort-only improvements since the compression batch CRC was reverted.

## 7. Known Issues

| # | Description | Severity | Introduced | Status |
|---|-------------|----------|------------|--------|
| 1 | CRC slicing-by-8 broke multi-block compression | CRITICAL | dffe019 | **FIXED in f50bd8f** |

No other known pre-existing divergences, bugs, or test failures.

## 8. Summary

**Commit f50bd8f PASSES validation.** The CRC multi-block bug from dffe019 is fully resolved. All 194 unit tests pass (163,057 assertions), 206 differential tests show 0 divergences, 0 ASAN/UBSAN errors, and 987 fuzz runs produce 0 crashes. The decompression-side batch CRC delivers a genuine 2.3-2.7x speedup on zeros workloads. Compression performance retains the blocksort optimization gains (1.06-1.25x on text, 1.20-1.23x on repeated). Overall quality trend: positive -- the CRC bug was caught immediately by unit tests and fuzz, and the fix is clean.
