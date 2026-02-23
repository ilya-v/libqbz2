# Validation Report: dffe019

| Field | Value |
|-------|-------|
| **Commit** | `dffe0196cd50c39f008e5f8aaaf2233093fbeb54` |
| **Description** | perf: optimize CRC computation with slicing-by-8 |
| **Date** | 2026-02-23 |
| **Validator** | tester agent |
| **Verdict** | **FAIL — CRITICAL BUG** |

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc -O2 | **PASS** |
| ASAN+UBSAN | clang -fsanitize=address,undefined | **PASS** |
| Fuzz harnesses | clang -fsanitize=fuzzer,address | **PASS** |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 56 | **1** | 235 | 0.015s |
| test_edge_cases | 67 | 62 | **5** | 60,425 | 0.042s |
| test_streaming | 30 | 26 | **4** | 1,521 | 0.207s |
| test_advanced | 40 | 38 | **2** | 100,866 | 0.131s |
| test_roundtrip | 137 | 118 | **19** | 175 | 1.125s |
| **Total** | **331** | **300** | **31** | **163,222** | **1.520s** |

### Failure Details

All 31 failures share the same root cause: **BZ_DATA_ERROR (-4) during decompression of multi-block streams**. This is a CRC mismatch error. Every failing test involves input data large enough to span multiple bzip2 blocks.

| Failed Test | Suite | Input Size | BS | Error |
|------------|-------|-----------|----|----|
| roundtrip_large_repeated | test_api | 200,000 | 1 | BZ_DATA_ERROR |
| roundtrip_99999_bs1 | test_edge_cases | 99,999 | 1 | BZ_DATA_ERROR |
| roundtrip_100000_bs1 | test_edge_cases | 100,000 | 1 | BZ_DATA_ERROR |
| roundtrip_100001_bs1 | test_edge_cases | 100,001 | 1 | BZ_DATA_ERROR |
| roundtrip_200000_bs1 | test_edge_cases | 200,000 | 1 | BZ_DATA_ERROR |
| streaming_compress_multiblock | test_edge_cases | 200,000 | 1 | BZ_DATA_ERROR |
| multiblock_bs1_200k | test_streaming | 200,000 | 1 | decompress fail |
| multiblock_bs2_500k | test_streaming | 500,000 | 2 | decompress fail |
| multiblock_bs9_1M | test_streaming | 1,000,000 | 9 | decompress fail |
| small_decompress_multiblock | test_streaming | 200,000 | 1 | decompress fail |
| decompress_small_mode_large | test_advanced | >100K | - | BZ_DATA_ERROR |
| roundtrip_1mb | test_advanced | 1,000,000 | - | BZ_DATA_ERROR |
| rt_blockboundary_* (4 tests) | test_roundtrip | 99K-200K | 1 | roundtrip fail |
| rt_bs9_boundary_* (3 tests) | test_roundtrip | 899K-900K | 9 | roundtrip fail |
| rt_multiblock_bs*_* (9 tests) | test_roundtrip | 150K-1M | 1-9 | roundtrip fail |
| rt_multiblock_small_bs* (3 tests) | test_roundtrip | 200K-1M | 1,5,9 | roundtrip fail |

**Root cause**: The batch CRC computation in `copy_input_until_stop` (bzlib.c) computes incorrect CRC when input spans multiple blocks. The CRC over bytes consumed before a block flush is miscounted or the running CRC is not properly continued across block boundaries.

## 3. Differential Tests

| Metric | Value |
|--------|-------|
| Total tests | 206 |
| Passed | 206 |
| Divergences | **0** |

Note: differential tests pass because their inputs are small enough to fit in a single block. The CRC bug only manifests on multi-block streams.

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | ASAN Errors | Leaks | Time |
|-------|-------|--------|--------|-------------|-------|------|
| test_api | 57 | 56 | 1 | 0 | 200,000 bytes | - |
| test_edge_cases | 67 | 62 | 5 | 0 | 952,200 bytes | - |
| test_streaming | 30 | 26 | 4 | 0 | 5,721,400 bytes | - |
| test_advanced | 40 | 38 | 2 | 0 | 3,759,613 bytes | - |
| test_roundtrip | 137 | 118 | 19 | 0 | 10,300,000 bytes | - |

**No ASAN memory safety errors** (no buffer overruns, no use-after-free). The memory leaks are secondary — caused by test code not cleaning up buffers after decompression failures. The slicing-by-8 function itself does not have memory safety issues.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | New Units | Corpus | Time |
|---------|------|--------|---------|-----------|--------|------|
| fuzz_compress | 66 | 6 | 0 | 0 | 62 / 10 MB | ~12s |
| fuzz_decompress | 443 | 11 | 0 | 0 | 89 / 12 MB | ~12s |
| fuzz_differential | 478 | 19 | 0 | 0 | 211 / 26 MB | 25s |
| **Total** | **987** | -- | **0** | **0** | -- | **37s** |

Note: fuzz did not catch the CRC bug because corpus inputs are likely too small to trigger multi-block compression. The fuzz corpus needs larger inputs (>100KB) to exercise this code path.

## 6. Benchmarks

**CAVEAT: These benchmarks are unreliable because the CRC bug corrupts multi-block streams. Performance numbers are provided for reference only — the optimization cannot be evaluated until the correctness bug is fixed.**

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 18.13 | 14.93 | 1.21x |
| text-100k | 5 | 17.61 | 14.93 | 1.18x |
| text-100k | 9 | 18.17 | 14.99 | 1.21x |
| binary-100k | 1 | 11.08 | 9.60 | 1.15x |
| binary-100k | 5 | 14.89 | 12.47 | 1.19x |
| binary-100k | 9 | 10.90 | 11.10 | 0.98x |
| repeated-100k | 1 | 11.17 | 8.96 | 1.25x |
| repeated-100k | 5 | 14.29 | 11.63 | 1.23x |
| repeated-100k | 9 | 13.81 | 12.22 | 1.13x |
| zeros-100k | 1 | 268.29 | 200.16 | 1.34x |
| zeros-100k | 5 | 369.18 | 213.34 | 1.73x |
| zeros-100k | 9 | 351.49 | 199.75 | 1.76x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|-----:|----:|--------:|
| text-100k | 1 | 78.98 | 82.98 | 0.95x |
| text-100k | 5 | 78.60 | 82.68 | 0.95x |
| text-100k | 9 | 78.55 | 82.55 | 0.95x |
| binary-100k | 1 | 26.34 | 30.14 | 0.87x |
| binary-100k | 5 | 22.85 | 22.87 | 1.00x |
| binary-100k | 9 | 22.95 | 22.40 | 1.02x |
| repeated-100k | 1 | 270.51 | 279.19 | 0.97x |
| repeated-100k | 5 | 277.15 | 275.95 | 1.00x |
| repeated-100k | 9 | 264.23 | 261.47 | 1.01x |
| zeros-100k | 1 | 916.13 | 384.51 | **2.38x** |
| zeros-100k | 5 | 964.87 | 388.82 | **2.48x** |
| zeros-100k | 9 | 935.57 | 389.44 | **2.40x** |

Note: The 2.38-2.48x decompression speedup on zeros may be partially due to the CRC bug (skipping work on broken streams). These numbers must be re-validated after the fix.

## 7. Known Issues

| # | Description | Severity | Introduced | Status |
|---|------------|----------|-----------|--------|
| 1 | **Multi-block CRC mismatch** — batch CRC in copy_input_until_stop produces wrong CRC when data spans multiple blocks, causing BZ_DATA_ERROR on decompression | **CRITICAL** | dffe019 | **OPEN** |

## 8. Summary

**Commit dffe019 FAILS validation.** The CRC slicing-by-8 optimization introduces a critical bug: multi-block streams produce incorrect CRC values, causing decompression to reject them with BZ_DATA_ERROR. 31 out of 331 unit tests fail, all on inputs large enough to span multiple bzip2 blocks. Single-block inputs are unaffected (300/331 pass, 206/206 differential pass). No memory safety errors found — the slicing function itself is safe, but the CRC accounting at block boundaries is wrong. The worker has been notified and asked to fix the multi-block CRC boundary handling.
