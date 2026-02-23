# Validation Report: ea4270b

| Field | Value |
|-------|-------|
| **Commit** | `ea4270b` |
| **Description** | perf: hybrid blocksort — SA-IS fallback for repetitive blocks |
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
| test_api | 57 | 57 | 0 | 235 | 0.015s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.038s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.244s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.147s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.202s |
| test_error_paths | 60 | 60 | 0 | 227 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.050s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.844s |
| **Total** | **482** | **482** | **0** | **164,608** | **2.541s** |

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
| test_api | 57 | 57 | 0 | 0 | 0 | 0.198s |
| test_edge_cases | 67 | 67 | 0 | 0 | 0 | 0.508s |
| test_streaming | 30 | 30 | 0 | 0 | 0 | 2.053s |
| test_advanced | 40 | 40 | 0 | 0 | 0 | 1.165s |
| test_roundtrip | 137 | 137 | 0 | 0 | 0 | 9.505s |
| test_error_paths | 60 | 60 | 0 | 0 | 0 | 0.013s |
| test_fileio | 58 | 58 | 0 | 0 | 0 | 0.781s |
| test_multiblock | 33 | 33 | 0 | 0 | 0 | 7.075s |
| **Total** | **482** | **482** | **0** | **0** | **0** | **21.298s** |

No address sanitizer errors. No undefined behavior. No memory leaks.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Corpus | Time |
|---------|------|--------|---------|--------|------|
| fuzz_compress | 66 | 5 | 0 | 62 / 10 MB | ~13s |
| fuzz_decompress | 443 | 11 | 0 | 87 / 13 MB | ~40s |
| fuzz_differential | 478 | 17 | 0 | 211 / 26 MB | ~27s |
| fuzz_diff_streaming | 478 | 19 | 0 | 207 / 27 MB | ~25s |
| **Total** | **1,465** | -- | **0** | -- | ~105s |

Differential harness: 0 divergences (new or known).
Diff streaming harness: 0 divergences (new or known).

Note: fuzz_decompress ran over its 10s allocation (40s total) due to large corpus loading. It completed successfully with 0 crashes. The total budget was slightly exceeded but all harnesses completed.

## 6. Benchmarks

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 16.71 | 13.64 | **1.22x** |
| text-100k | 5 | 19.52 | 19.46 | 1.00x |
| text-100k | 9 | 22.76 | 17.38 | **1.31x** |
| binary-100k | 1 | 12.32 | 13.67 | 0.90x |
| binary-100k | 5 | 14.81 | 14.84 | 1.00x |
| binary-100k | 9 | 14.42 | 14.95 | 0.96x |
| repeated-100k | 1 | 20.65 | 11.80 | **1.75x** |
| repeated-100k | 5 | 20.60 | 15.62 | **1.32x** |
| repeated-100k | 9 | 20.67 | 15.01 | **1.38x** |
| zeros-100k | 1 | 271.99 | 250.38 | 1.09x |
| zeros-100k | 5 | 282.68 | 281.82 | 1.00x |
| zeros-100k | 9 | 280.66 | 275.38 | 1.02x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 75.68 | 78.93 | 0.96x |
| text-100k | 5 | 105.85 | 110.57 | 0.96x |
| text-100k | 9 | 105.23 | 106.85 | 0.98x |
| binary-100k | 1 | 29.62 | 29.87 | 0.99x |
| binary-100k | 5 | 30.73 | 28.32 | 1.08x |
| binary-100k | 9 | 30.67 | 29.78 | 1.03x |
| repeated-100k | 1 | 350.06 | 367.12 | 0.95x |
| repeated-100k | 5 | 381.63 | 374.02 | 1.02x |
| repeated-100k | 9 | 365.83 | 368.26 | 0.99x |
| zeros-100k | 1 | 1333.39 | 512.89 | **2.60x** |
| zeros-100k | 5 | 1374.39 | 514.18 | **2.67x** |
| zeros-100k | 9 | 1372.00 | 516.29 | **2.66x** |

### Speedup Summary

| Category | Range | Best | vs f50bd8f |
|----------|-------|------|------------|
| Compression — text | 1.00x–1.31x | **1.31x** | Stable |
| Compression — repeated | 1.32x–1.75x | **1.75x** | **+0.47x** (was 1.28x) |
| Compression — binary | 0.90x–1.00x | 1.00x | Stable |
| Compression — zeros | 1.00x–1.09x | 1.09x | Stable |
| Decompression — text | 0.96x–0.98x | 0.98x | Stable |
| Decompression — binary | 0.99x–1.08x | 1.08x | Stable |
| Decompression — repeated | 0.95x–1.02x | 1.02x | Stable |
| Decompression — zeros | 2.60x–2.67x | **2.67x** | Stable |

**Key improvement**: SA-IS fallback delivers **1.75x** compression speedup on repeated data at bs=1 (up from 1.26x in f50bd8f). This is the SA-IS path activating on repetitive blocks where the quicksort budget is exceeded.

## 7. Known Issues

| # | Description | Severity | Introduced | Status |
|---|------------|----------|-----------|--------|
| 1 | Multi-block CRC mismatch from compression-side batch CRC | CRITICAL | dffe019 | **FIXED** in f50bd8f |

No other known pre-existing divergences, bugs, or test failures.

## 8. Summary

**Commit ea4270b is CLEAN.** The hybrid blocksort with SA-IS fallback passes all 482 unit tests (164,608 assertions), 497 differential/conformance tests with 0 divergences, 482 ASAN+UBSAN tests with 0 errors and 0 leaks, and 1,465 fuzz runs across 4 harnesses with 0 crashes. The SA-IS fallback delivers a measurable compression speedup on repetitive data: **1.75x on repeated-100k bs=1** (up from 1.28x in f50bd8f), confirming that the SA-IS path is activating correctly on repetitive blocks. Text and binary workloads are stable. Decompression throughput is unchanged as expected (decompression does not use blocksort). All prior optimizations (word-at-a-time mainGtU, decompression-side CRC slicing-by-8) remain intact. Overall quality trend: strongly positive — the library now has three confirmed performance wins with zero correctness regressions.
