# Validation Report: dffe019

| Field | Value |
|-------|-------|
| **Commit** | `dffe019` |
| **Description** | perf: optimize CRC computation with slicing-by-8 |
| **Date** | 2026-02-23 |
| **Validator** | tester agent |
| **Time budget** | ~2 min |
| **RESULT** | **FAIL -- CRITICAL BUG** |

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc | **PASS** (0 warnings) |
| ASAN+UBSAN | clang | **PASS** (0 warnings) |
| Fuzz harnesses | clang (fuzzer+asan) | **PASS** (7 harnesses built, 16,623 coverage points) |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 56 | **1** | 235 | 0.018s |
| test_edge_cases | 67 | 62 | **5** | 60,425 | 0.046s |
| test_streaming | 30 | 26 | **4** | 1,521 | 0.209s |
| test_advanced | 40 | 38 | **2** | 100,866 | 0.128s |
| **Total** | **194** | **182** | **12** | **163,047** | **0.401s** |

### Failing Tests (all multi-block data)

| Suite | Test | Input | Error |
|-------|------|-------|-------|
| test_api | roundtrip_large_repeated | 200,000 bytes, bs=1 | BZ_DATA_ERROR |
| test_edge_cases | roundtrip_100001_bs1 | 100,001 bytes, bs=1 | BZ_DATA_ERROR |
| test_edge_cases | roundtrip_200000_bs1 | 200,000 bytes, bs=1 | BZ_DATA_ERROR |
| test_edge_cases | streaming_compress_multiblock | 150,000 bytes, bs=1 | BZ_DATA_ERROR |
| test_edge_cases | (2 additional boundary tests) | ~100,000 bytes | BZ_DATA_ERROR |
| test_streaming | multiblock_bs1_200k | 200,000 bytes, bs=1 | BZ_DATA_ERROR |
| test_streaming | multiblock_bs2_500k | 500,000 bytes, bs=2 | BZ_DATA_ERROR |
| test_streaming | multiblock_bs9_1M | 1,000,000 bytes, bs=9 | BZ_DATA_ERROR |
| test_streaming | small_decompress_multiblock | 200,000 bytes | BZ_DATA_ERROR |
| test_advanced | decompress_small_mode_large | 200,000 bytes | BZ_DATA_ERROR |
| test_advanced | roundtrip_1mb | 1,048,576 bytes, bs=9 | BZ_DATA_ERROR |

**Pattern:** Every test with input data exceeding one block (blockSize100k * 100,000 bytes) fails. Single-block inputs pass.

## 3. Differential Tests

| Metric | Value |
|--------|-------|
| Total tests | 206 |
| Passed | 206 |
| Divergences | 0 |

Note: The deterministic differential suite uses small inputs that fit within a single block, so it does not detect this bug. The fuzz differential harness (section 5) did detect it.

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | ASAN Errors | Time |
|-------|-------|--------|--------|-------------|------|
| test_api | 57 | 56 | 1 | 1 secondary leak (200KB) | 0.244s |
| Other suites | 137 | 126 | 11 | 0 | -- |

The 200KB leak is a secondary effect of the test failure (malloc'd buffer not freed on error path). No primary ASAN or UBSAN errors -- the CRC bug is a logic error, not a memory safety issue.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Coverage | Time |
|---------|------|--------|---------|----------|------|
| fuzz_compress | 66 | 6 | 0 | 6,386 edges | 11s |
| fuzz_decompress | -- | -- | 0 | -- | -- |
| fuzz_differential | 389 | 97 | **1 CRASH** | -- | -- |
| **Total** | **455+** | -- | **1** | -- | -- |

### Differential Fuzz Crash Details

```
DIVERGENCE: compressed output content mismatch!
  input size: 100791, blockSize=1
  both produced 101778 bytes but content differs
  first diff at byte 10: qbz2=0x20 ref=0xb9
```

Crash artifact: `crash-b00c1c25de6ea98dece976b411f0287035ab9ac2`
Trigger: Any input >= 100,000 bytes at blockSize=1 (multi-block compression).
The divergence is in CRC bytes embedded in the compressed bz2 stream. The compressed data structure is otherwise identical (same size).

## 6. Benchmarks

**WARNING: Benchmark numbers are unreliable because CRC values are incorrect for multi-block data.**

### Compression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|------|-----|---------|
| text-100k | 1 | 18.70 | 16.63 | 1.12x |
| text-100k | 5 | 23.19 | 18.31 | 1.27x |
| text-100k | 9 | 18.79 | 15.42 | 1.22x |
| binary-100k | 1 | 15.96 | 16.01 | 1.00x |
| binary-100k | 5 | 16.08 | 15.90 | 1.01x |
| binary-100k | 9 | 15.92 | 15.33 | 1.04x |
| repeated-100k | 1 | 15.89 | 13.06 | 1.22x |
| repeated-100k | 5 | 15.31 | 15.42 | 0.99x |
| repeated-100k | 9 | 20.07 | 16.12 | 1.25x |
| zeros-100k | 1 | 478.33 | 290.18 | 1.65x |
| zeros-100k | 5 | 517.64 | 274.82 | 1.88x |
| zeros-100k | 9 | 491.95 | 297.12 | 1.66x |

### Decompression Throughput (MB/s)

| Workload | BS | qbz2 | ref | Speedup |
|----------|----|------|-----|---------|
| text-100k | 1 | 112.68 | 118.53 | 0.95x |
| text-100k | 5 | 80.63 | 84.93 | 0.95x |
| text-100k | 9 | 110.59 | 118.38 | 0.93x |
| binary-100k | 1 | 31.38 | 31.48 | 1.00x |
| binary-100k | 5 | 32.75 | 32.77 | 1.00x |
| binary-100k | 9 | 32.71 | 32.86 | 1.00x |
| repeated-100k | 1 | 389.65 | 330.11 | 1.18x |
| repeated-100k | 5 | 407.14 | 398.34 | 1.02x |
| repeated-100k | 9 | 390.13 | 402.16 | 0.97x |
| zeros-100k | 1 | 1407.60 | 550.76 | 2.56x |
| zeros-100k | 5 | 1426.91 | 549.77 | 2.60x |
| zeros-100k | 9 | 1457.69 | 551.49 | 2.64x |

The zeros decompression showing 2.5x+ is suspicious and likely reflects the bug.

## 7. Known Issues

| # | Description | Severity | Introduced | Status |
|---|-------------|----------|------------|--------|
| 1 | **CRC slicing-by-8 produces incorrect CRC for multi-block data** | CRITICAL | dffe019 | **OPEN** |

**Root cause:** The batch CRC in `copy_input_until_stop` (bzlib.c) does not correctly handle CRC state across block boundaries. The per-block CRC is correct for the first block but wrong for subsequent blocks. The divergence is purely in CRC bytes -- the rest of the compressed stream is identical.

## 8. Summary

**Commit dffe019 FAILS validation.** The CRC slicing-by-8 optimization introduces a critical data integrity bug: compressed output for multi-block data contains incorrect CRC values, causing decompression to reject with BZ_DATA_ERROR. 12/194 unit tests fail, fuzz differential found a divergence on 100,791-byte input. All failures on inputs exceeding one block. Single-block inputs unaffected. Worker must fix the CRC batch computation before this optimization can be accepted.
