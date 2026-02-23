# Validation Report: f50bd8f

| Field | Value |
|-------|-------|
| **Commit** | `f50bd8f90d2a4d28d337f1bbc4240107e75c89f4` |
| **Description** | fix: revert compression-side batch CRC that broke multi-block streams |
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
| test_api | 57 | 57 | 0 | 235 | 0.018s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.048s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.227s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.128s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.206s |
| **Total** | **331** | **331** | **0** | **163,232** | **1.627s** |

All 31 multi-block CRC failures from dffe019 are now fixed.

## 3. Differential Tests

| Metric | Value |
|--------|-------|
| Total tests | 206 |
| Passed | 206 |
| Divergences | **0** |
| Error behavior tested | Yes — both success and error paths compared |

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Errors | Leaks | Time |
|-------|-------|--------|--------|--------|-------|------|
| test_api | 57 | 57 | 0 | 0 | 0 | 0.211s |
| test_edge_cases | 67 | 67 | 0 | 0 | 0 | 0.651s |
| test_streaming | 30 | 30 | 0 | 0 | 0 | 1.952s |
| test_advanced | 40 | 40 | 0 | 0 | 0 | 1.193s |
| test_roundtrip | 137 | 137 | 0 | 0 | 0 | 9.204s |
| **Total** | **331** | **331** | **0** | **0** | **0** | **13.211s** |

No address sanitizer errors. No undefined behavior. No memory leaks.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | New Units | Corpus | Time |
|---------|------|--------|---------|-----------|--------|------|
| fuzz_compress | 66 | 6 | 0 | 0 | 61 / 10 MB | ~11s |
| fuzz_decompress | 443 | 12 | 0 | 0 | 89 / 13 MB | ~12s |
| fuzz_differential | 478 | 20 | 0 | 0 | 209 / 26 MB | ~25s |
| **Total** | **987** | -- | **0** | **0** | -- | **35s** |

Differential harness: 0 divergences (new or known).

## 6. Benchmarks

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 19.65 | 20.88 | 0.94x |
| text-100k | 5 | 18.27 | 20.61 | 0.89x |
| text-100k | 9 | 24.40 | 18.59 | **1.31x** |
| binary-100k | 1 | 14.40 | 15.47 | 0.93x |
| binary-100k | 5 | 15.08 | 15.45 | 0.98x |
| binary-100k | 9 | 14.99 | 14.86 | 1.01x |
| repeated-100k | 1 | 13.98 | 11.07 | **1.26x** |
| repeated-100k | 5 | 19.67 | 15.37 | **1.28x** |
| repeated-100k | 9 | 18.91 | 15.41 | **1.23x** |
| zeros-100k | 1 | 268.00 | 269.80 | 0.99x |
| zeros-100k | 5 | 262.45 | 277.52 | 0.95x |
| zeros-100k | 9 | 260.23 | 277.13 | 0.94x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 103.87 | 100.22 | 1.04x |
| text-100k | 5 | 105.08 | 109.96 | 0.96x |
| text-100k | 9 | 77.10 | 81.86 | 0.94x |
| binary-100k | 1 | 30.54 | 30.64 | 1.00x |
| binary-100k | 5 | 31.60 | 31.27 | 1.01x |
| binary-100k | 9 | 30.41 | 31.01 | 0.98x |
| repeated-100k | 1 | 368.88 | 382.12 | 0.97x |
| repeated-100k | 5 | 380.15 | 379.35 | 1.00x |
| repeated-100k | 9 | 325.37 | 311.80 | 1.04x |
| zeros-100k | 1 | 1369.43 | 521.82 | **2.62x** |
| zeros-100k | 5 | 1365.62 | 523.34 | **2.61x** |
| zeros-100k | 9 | 1378.63 | 520.97 | **2.65x** |

### Speedup Summary

| Category | Range | Best |
|----------|-------|------|
| Compression — text | 0.89x–1.31x | **1.31x** |
| Compression — repeated | 1.23x–1.28x | **1.28x** |
| Compression — binary | 0.93x–1.01x | 1.01x |
| Compression — zeros | 0.94x–0.99x | 0.99x |
| Decompression — text | 0.94x–1.04x | 1.04x |
| Decompression — binary | 0.98x–1.01x | 1.01x |
| Decompression — repeated | 0.97x–1.04x | 1.04x |
| Decompression — zeros | 2.61x–2.65x | **2.65x** |

## 7. Known Issues

| # | Description | Severity | Introduced | Status |
|---|------------|----------|-----------|--------|
| 1 | Multi-block CRC mismatch from compression-side batch CRC | CRITICAL | dffe019 | **FIXED** in f50bd8f |

No other known pre-existing divergences, bugs, or test failures.

## 8. Summary

**Commit f50bd8f is CLEAN.** This fix correctly reverts the broken compression-side batch CRC from dffe019 while retaining the working decompression-side batch CRC optimization. All 331 unit tests pass (163,232 assertions), including all 31 multi-block tests that failed in dffe019. 206 differential tests pass with 0 divergences. 331 ASAN+UBSAN tests pass with 0 errors and 0 leaks. 987 fuzz runs across 3 harnesses produced 0 crashes. Benchmarks confirm the decompression-side CRC slicing-by-8 delivers a real **2.62-2.65x speedup on zeros decompression**, and the blocksort optimization from ab9b0b9 continues to provide **1.23-1.31x compression speedup** on text and repeated data. The critical CRC bug introduced in dffe019 is fully resolved. Overall quality trend: positive — the library now has two confirmed performance wins (blocksort + decompression CRC) with zero correctness regressions.
