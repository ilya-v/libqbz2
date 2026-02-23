# Validation Report: 1f048d6

| Field | Value |
|-------|-------|
| **Commit** | `1f048d6` |
| **Description** | perf: hybrid CRC in decompression — inline for single bytes, batch for runs |
| **Date** | 2026-02-23 |
| **Validator** | tester agent |
| **Verdict** | **PASS** |

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc -O2 | **PASS** |
| ASAN+UBSAN | clang -fsanitize=address,undefined | **PASS** |
| Fuzz harnesses | clang -fsanitize=fuzzer,address | **PASS** |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.023s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.054s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.366s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.196s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.401s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.078s |
| test_multiblock | 33 | 33 | 0 | 197 | 1.081s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.930s |
| test_oom | 22 | 22 | 0 | 318 | 0.039s |
| **Total** | **559** | **559** | **0** | **165,060** | **5.169s** |

## 3. Differential Tests

| Suite | Tests | Passed | Divergences |
|-------|-------|--------|-------------|
| test_differential | 206 | 206 | **0** |
| test_diff_multiblock | 129 | 129 | **0** |
| test_bzip2_corpus | 162 | 162 | **0** |
| **Total** | **497** | **497** | **0** |

Error behavior tested: Yes — both success and error paths compared.

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Errors | Leaks | Time |
|-------|-------|--------|--------|--------|-------|------|
| test_api | 57 | 57 | 0 | 0 | 0 | 0.213s |
| test_edge_cases | 67 | 67 | 0 | 0 | 0 | 0.599s |
| test_streaming | 30 | 30 | 0 | 0 | 0 | 2.311s |
| test_advanced | 40 | 40 | 0 | 0 | 0 | 1.329s |
| test_roundtrip | 137 | 137 | 0 | 0 | 0 | 10.243s |
| test_error_paths | 60 | 60 | 0 | 0 | 0 | 0.010s |
| test_fileio | 58 | 58 | 0 | 0 | 0 | 0.757s |
| test_multiblock | 33 | 33 | 0 | 0 | 0 | 7.098s |
| test_blocksort_paths | 55 | 55 | 0 | 0 | 0 | 10.708s |
| test_oom | 22 | 22 | 0 | 0 | 0 | 0.286s |
| **Total** | **559** | **559** | **0** | **0** | **0** | **33.554s** |

No address sanitizer errors. No undefined behavior. No memory leaks.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Corpus | Time |
|---------|------|--------|---------|--------|------|
| fuzz_compress | 52 | 4 | 0 | 44 / 8 MB | ~11s |
| fuzz_decompress | 443 | 10 | 0 | 87 / 12 MB | ~41s |
| fuzz_differential | 485 | 17 | 0 | 200 / 25 MB | ~27s |
| fuzz_diff_streaming | 485 | 17 | 0 | 202 / 27 MB | ~27s |
| **Total** | **1,465** | -- | **0** | -- | ~106s |

Differential harness: 0 divergences (new or known).
Diff streaming harness: 0 divergences (new or known).

Note: fuzz_decompress exceeded its 10s allocation (41s total) due to large corpus loading. Completed successfully with 0 crashes.

## 6. Benchmarks

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 26.37 | 22.64 | **1.16x** |
| text-100k | 5 | 26.52 | 21.19 | **1.25x** |
| text-100k | 9 | 25.91 | 21.92 | **1.18x** |
| binary-100k | 1 | 16.86 | 16.83 | 1.00x |
| binary-100k | 5 | 16.72 | 16.91 | 0.99x |
| binary-100k | 9 | 16.17 | 16.10 | 1.00x |
| repeated-100k | 1 | 22.15 | 12.91 | **1.72x** |
| repeated-100k | 5 | 21.57 | 16.20 | **1.33x** |
| repeated-100k | 9 | 21.91 | 15.96 | **1.37x** |
| zeros-100k | 1 | 297.53 | 295.53 | 1.01x |
| zeros-100k | 5 | 318.66 | 316.73 | 1.01x |
| zeros-100k | 9 | 312.83 | 313.57 | 1.00x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 121.14 | 122.50 | 0.99x |
| text-100k | 5 | 120.48 | 123.30 | 0.98x |
| text-100k | 9 | 122.87 | 120.91 | 1.02x |
| binary-100k | 1 | 33.30 | 33.12 | 1.01x |
| binary-100k | 5 | 34.78 | 33.89 | 1.03x |
| binary-100k | 9 | 34.06 | 33.46 | 1.02x |
| repeated-100k | 1 | 391.82 | 385.90 | 1.02x |
| repeated-100k | 5 | 404.01 | 397.71 | 1.02x |
| repeated-100k | 9 | 389.21 | 403.03 | 0.97x |
| zeros-100k | 1 | 2117.07 | 568.41 | **3.72x** |
| zeros-100k | 5 | 2215.10 | 570.02 | **3.89x** |
| zeros-100k | 9 | 2182.18 | 566.87 | **3.85x** |

### Speedup Summary

| Category | Range | Best | vs f56cb57 |
|----------|-------|------|------------|
| Compression — text | 1.16x–1.25x | **1.25x** | Stable |
| Compression — repeated | 1.33x–1.72x | **1.72x** | Stable |
| Compression — binary | 0.99x–1.00x | 1.00x | Stable |
| Compression — zeros | 1.00x–1.01x | 1.01x | Stable |
| Decompression — text | 0.98x–1.02x | 1.02x | **Improved** (was 0.93x) |
| Decompression — binary | 1.01x–1.03x | 1.03x | Stable |
| Decompression — repeated | 0.97x–1.02x | 1.02x | Stable |
| Decompression — zeros | 3.72x–3.89x | **3.89x** | **+1.20x** (was 2.69x) |

**Key improvement**: Hybrid CRC decompression delivers massive zeros decompression speedup: **3.89x** (up from 2.69x in f56cb57). The batch CRC for RLE runs is highly effective on run-heavy data. Text decompression is now at parity (0.98x-1.02x, recovered from the 0.93x dip in f56cb57). Binary and repeated decompression stable.

## 7. Known Issues

| # | Description | Severity | Introduced | Status |
|---|------------|----------|-----------|--------|
| 1 | Multi-block CRC mismatch from compression-side batch CRC | CRITICAL | dffe019 | **FIXED** in f50bd8f |

No other known pre-existing divergences, bugs, or test failures.

## 8. Summary

**Commit 1f048d6 is CLEAN.** The hybrid CRC decompression optimization passes all 559 unit tests (165,060 assertions across 10 suites), 497 differential/conformance tests with 0 divergences, 559 ASAN+UBSAN tests with 0 errors and 0 leaks, and 1,465 fuzz runs across 4 harnesses with 0 crashes. The optimization targets decompression-side CRC computation: RLE run drains now use memset + batch CRC instead of byte-at-a-time, while single-byte outputs use inline CRC. This delivers a major improvement on zeros decompression: **3.89x** (up from 2.69x), confirming the batch CRC path is activating correctly on run-heavy data. Text decompression recovered to parity (0.98x-1.02x). Compression throughput is unaffected as expected. Overall quality trend: strongly positive — five confirmed optimizations (3 compression, 2 decompression) with zero correctness regressions.
