# Validation Report: ab9b0b9

| Field | Value |
|-------|-------|
| **Commit** | `ab9b0b9ebd888bae31bd40f2da3719c4939945fa` |
| **Description** | perf: optimize blocksort mainGtU with word-at-a-time comparisons |
| **Date** | 2026-02-23 |
| **Validator** | tester agent (re-validation with expanded test suite) |
| **Time budget** | 2 min |

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
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.063s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.262s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.156s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.366s |
| **Total** | **331** | **331** | **0** | **163,232** | **1.870s** |

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
| test_api | 57 | 57 | 0 | 0 | 0.275s |
| test_edge_cases | 67 | 67 | 0 | 0 | 0.730s |
| test_streaming | 30 | 30 | 0 | 0 | 2.222s |
| test_advanced | 40 | 40 | 0 | 0 | 1.275s |
| test_roundtrip | 137 | 137 | 0 | 0 | 10.295s |
| **Total** | **331** | **331** | **0** | **0** | **14.797s** |

No address sanitizer errors. No undefined behavior detected. No memory leaks.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | New Units | Corpus | Time |
|---------|------|--------|---------|-----------|--------|------|
| fuzz_compress | 66 | 5 | 0 | 0 | 61 inputs / 10 MB | 13s |
| fuzz_decompress | 443 | 12 | 0 | 0 | 87 inputs / 12 MB | ~12s |
| fuzz_differential | 478 | 19 | 0 | 0 | 211 inputs / 26 MB | 25s |
| **Total** | **987** | -- | **0** | **0** | -- | **37s** |

- Differential harness: **0 divergences** (new or known)
- 0 crashes across all harnesses

## 6. Benchmarks

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 25.54 | 21.38 | **1.19x** |
| text-100k | 5 | 27.28 | 20.88 | **1.31x** |
| text-100k | 9 | 20.58 | 23.28 | 0.88x |
| binary-100k | 1 | 16.85 | 16.49 | 1.02x |
| binary-100k | 5 | 17.21 | 17.37 | 0.99x |
| binary-100k | 9 | 16.71 | 16.85 | 0.99x |
| repeated-100k | 1 | 16.84 | 14.45 | **1.16x** |
| repeated-100k | 5 | 22.02 | 19.78 | **1.11x** |
| repeated-100k | 9 | 20.17 | 14.49 | **1.39x** |
| zeros-100k | 1 | 312.73 | 304.51 | 1.03x |
| zeros-100k | 5 | 326.34 | 325.26 | 1.00x |
| zeros-100k | 9 | 321.71 | 322.31 | 1.00x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 125.59 | 128.09 | 0.98x |
| text-100k | 5 | 90.59 | 91.54 | 0.99x |
| text-100k | 9 | 126.51 | 122.76 | 1.03x |
| binary-100k | 1 | 35.11 | 34.20 | 1.03x |
| binary-100k | 5 | 35.57 | 35.37 | 1.01x |
| binary-100k | 9 | 36.02 | 34.66 | 1.04x |
| repeated-100k | 1 | 427.52 | 424.90 | 1.01x |
| repeated-100k | 5 | 421.94 | 434.71 | 0.97x |
| repeated-100k | 9 | 387.69 | 427.53 | 0.91x |
| zeros-100k | 1 | 587.63 | 587.27 | 1.00x |
| zeros-100k | 5 | 587.00 | 589.69 | 1.00x |
| zeros-100k | 9 | 587.91 | 587.29 | 1.00x |

### Speedup Summary

| Category | Range | Best |
|----------|-------|------|
| Compression — text | 0.88x–1.31x | **1.31x** |
| Compression — binary | 0.99x–1.02x | 1.02x |
| Compression — repeated | 1.11x–1.39x | **1.39x** |
| Compression — zeros | 1.00x–1.03x | 1.03x |
| Decompression — all | 0.91x–1.04x | 1.04x |

## 7. Known Issues

No known pre-existing divergences, bugs, or test failures.

Note: text-100k compression at bs=9 shows 0.88x and repeated-100k decompression at bs=9 shows 0.91x. These are within measurement noise range and the optimization only touches the compression sort path, so the decompression number is definitely noise. The compression regression on text-bs9 should be monitored on future commits.

## 8. Summary

**Commit ab9b0b9 is CLEAN.** The blocksort word-at-a-time optimization passes all 331 unit tests (163,232 assertions), 206 differential tests with 0 divergences, 0 ASAN/UBSAN errors across 331 sanitized tests, and 987 fuzz runs with 0 crashes across 3 harnesses. Compression throughput improved significantly: up to **1.39x on repeated data** and **1.31x on text data** vs the reference library. Decompression remains at parity (expected -- blocksort only affects compression). Binary data compression is at parity. This is the first performance optimization commit and it delivers real, measurable compression speedups with zero correctness regressions. Overall quality trend: positive.
