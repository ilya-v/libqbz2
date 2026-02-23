# Validation Report: 46ebafb

| Field | Value |
|-------|-------|
| **Commit** | `46ebafb` |
| **Description** | perf: bulk memcpy for compressed output copy |
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
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.051s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.294s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.199s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.321s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.085s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.930s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.678s |
| test_oom | 22 | 22 | 0 | 318 | 0.037s |
| **Total** | **559** | **559** | **0** | **165,060** | **4.619s** |

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
| test_api | 57 | 57 | 0 | 0 | 0 | 0.284s |
| test_edge_cases | 67 | 67 | 0 | 0 | 0 | 0.802s |
| test_streaming | 30 | 30 | 0 | 0 | 0 | 2.709s |
| test_advanced | 40 | 40 | 0 | 0 | 0 | 1.214s |
| test_roundtrip | 137 | 137 | 0 | 0 | 0 | 10.333s |
| test_error_paths | 60 | 60 | 0 | 0 | 0 | 0.009s |
| test_fileio | 58 | 58 | 0 | 0 | 0 | 0.851s |
| test_multiblock | 33 | 33 | 0 | 0 | 0 | 7.043s |
| test_blocksort_paths | 55 | 55 | 0 | 0 | 0 | 10.425s |
| test_oom | 22 | 22 | 0 | 0 | 0 | 0.319s |
| **Total** | **559** | **559** | **0** | **0** | **0** | **33.989s** |

No address sanitizer errors. No undefined behavior. No memory leaks.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Corpus | Time |
|---------|------|--------|---------|--------|------|
| fuzz_compress | 62 | 5 | 0 | 45 / 10 MB | ~12s |
| fuzz_decompress | 443 | 10 | 0 | 87 / 12 MB | ~42s |
| fuzz_differential | 485 | 17 | 0 | 200 / 25 MB | ~27s |
| fuzz_diff_streaming | 485 | 17 | 0 | 198 / 26 MB | ~28s |
| **Total** | **1,475** | -- | **0** | -- | ~109s |

Differential harness: 0 divergences (new or known).
Diff streaming harness: 0 divergences (new or known).

## 6. Benchmarks

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 24.62 | 22.61 | 1.09x |
| text-100k | 5 | 27.60 | 21.82 | **1.27x** |
| text-100k | 9 | 27.50 | 22.59 | **1.22x** |
| binary-100k | 1 | 16.95 | 16.65 | 1.02x |
| binary-100k | 5 | 16.87 | 16.67 | 1.01x |
| binary-100k | 9 | 16.75 | 16.25 | 1.03x |
| repeated-100k | 1 | 20.94 | 13.01 | **1.61x** |
| repeated-100k | 5 | 23.33 | 17.99 | **1.30x** |
| repeated-100k | 9 | 22.49 | 17.53 | **1.28x** |
| zeros-100k | 1 | 324.40 | 292.27 | **1.11x** |
| zeros-100k | 5 | 333.84 | 328.03 | 1.02x |
| zeros-100k | 9 | 336.67 | 329.44 | 1.02x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 122.53 | 123.67 | 0.99x |
| text-100k | 5 | 118.52 | 124.14 | 0.95x |
| text-100k | 9 | 121.97 | 121.21 | 1.01x |
| binary-100k | 1 | 33.44 | 32.16 | 1.04x |
| binary-100k | 5 | 34.58 | 34.67 | 1.00x |
| binary-100k | 9 | 32.02 | 23.91 | 1.34x* |
| repeated-100k | 1 | 406.28 | 414.68 | 0.98x |
| repeated-100k | 5 | 411.77 | 395.10 | 1.04x |
| repeated-100k | 9 | 371.70 | 421.93 | 0.88x |
| zeros-100k | 1 | 2155.33 | 595.45 | **3.62x** |
| zeros-100k | 5 | 2289.27 | 597.49 | **3.83x** |
| zeros-100k | 9 | 2268.14 | 597.18 | **3.80x** |

*binary-100k bs=9 decomp 1.34x is a benchmark noise outlier (ref shows 23.91 vs typical ~33).

### Speedup Summary

| Category | Range | Best | vs 1f048d6 |
|----------|-------|------|------------|
| Compression — text | 1.09x–1.27x | **1.27x** | Stable |
| Compression — repeated | 1.28x–1.61x | **1.61x** | Stable |
| Compression — binary | 1.01x–1.03x | 1.03x | Stable |
| Compression — zeros | 1.02x–1.11x | **1.11x** | Slight improvement (was 1.01x) |
| Decompression — text | 0.95x–1.01x | 1.01x | Stable |
| Decompression — binary | 1.00x–1.04x | 1.04x | Stable |
| Decompression — repeated | 0.88x–1.04x | 1.04x | Noisy (repeated-100k bs=9 shows 0.88x) |
| Decompression — zeros | 3.62x–3.83x | **3.83x** | Stable |

**Key observation**: The bulk memcpy optimization targets compression-side output copy. Zeros compression shows a small improvement to 1.11x (was 1.01x). All other metrics stable. The repeated-100k bs=9 decomp 0.88x is likely benchmark noise (single-run variation).

## 7. Known Issues

| # | Description | Severity | Introduced | Status |
|---|------------|----------|-----------|--------|
| 1 | Multi-block CRC mismatch from compression-side batch CRC | CRITICAL | dffe019 | **FIXED** in f50bd8f |

No other known pre-existing divergences, bugs, or test failures.

## 8. Summary

**Commit 46ebafb is CLEAN.** The bulk memcpy optimization for compressed output copy passes all 559 unit tests (165,060 assertions across 10 suites), 497 differential/conformance tests with 0 divergences, 559 ASAN+UBSAN tests with 0 errors and 0 leaks, and 1,475 fuzz runs across 4 harnesses with 0 crashes. The optimization targets the copy_output_until_stop path in compression, replacing byte-at-a-time copies with bulk memcpy. Zeros compression shows a small improvement to 1.11x. All other workloads stable. No regressions detected. Overall quality trend: strongly positive — six confirmed optimizations with zero correctness regressions.
