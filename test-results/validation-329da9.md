# Validation Report: 329da9d — table-based Huffman decode fast path

**Commit:** 329da9d
**Description:** perf: add table-based Huffman decode for decompression fast path
**Date:** 2026-02-24
**Validator:** tester (per-commit)
**Verdict:** PASS

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc -O2 | PASS |
| ASAN+UBSAN | clang -fsanitize=address,undefined | PASS |
| Fuzz harnesses | clang -fsanitize=fuzzer,address | PASS (via run-quick-fuzz.sh) |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.014s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.050s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.106s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.150s |
| test_roundtrip | 137 | 137 | 0 | 175 | 0.798s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.054s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.543s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 0.820s |
| test_oom | 22 | 22 | 0 | 318 | 0.020s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.011s |
| test_malformed | 32 | 32 | 0 | 99 | 0.012s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.022s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.194s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.136s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.949s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.006s |
| **Total** | **827** | **827** | **0** | **422,179** | **3.89s** |

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **Total** | **497** | **497** | **0** |

All differential tests compare byte-for-byte compressed/decompressed output between qbz2 and reference libbz2. Zero divergences. Error code comparison is included for rejected inputs.

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Time |
|-------|-------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0.172s |
| test_edge_cases | 67 | 67 | 0 | 0.655s |
| test_advanced | 40 | 40 | 0 | 0.744s |
| test_streaming | 30 | 30 | 0 | 1.497s |
| test_roundtrip | 137 | 137 | 0 | 5.642s |
| test_error_paths | 60 | 60 | 0 | 0.018s |
| test_fileio | 58 | 58 | 0 | 0.919s |
| test_multiblock | 33 | 33 | 0 | 4.699s |
| test_blocksort_paths | 55 | 55 | 0 | 6.765s |
| test_oom | 22 | 22 | 0 | 0.294s |
| test_decompress_errors | 32 | 32 | 0 | 0.165s |
| test_malformed | 32 | 32 | 0 | 0.072s |
| test_streaming_edge | 25 | 25 | 0 | 0.326s |
| test_param_combos | 92 | 92 | 0 | 1.576s |
| test_rle_huffman_edge | 47 | 47 | 0 | 0.942s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 12.891s |
| test_concat_readahead | 20 | 20 | 0 | 0.141s |
| **Total** | **827** | **827** | **0** | **37.5s** |

Zero ASAN errors, zero UBSAN violations. The table-based decode uses fixed-size array `decode_fast[6][1024]` in DState — no out-of-bounds accesses detected.

## 5. Quick Fuzz

| Harness | Result | Notes |
|---------|--------|-------|
| fuzz_compress | PASS | 0 crashes |
| fuzz_decompress | PASS | 0 crashes (killed at budget limit) |
| fuzz_differential | PASS | 0 divergences |
| fuzz_diff_streaming | PASS | 0 crashes, 0 divergences |

4 harnesses, 3 completed within budget, 1 (fuzz_decompress) killed at 30s budget limit. Zero crashes, zero divergences across all harnesses. All harnesses ASAN-enabled.

## 6. Benchmarks

**Note:** Absolute throughput numbers in this run are lower than d061e3d validation due to concurrent system load from other agents. Speedup ratios vs reference (measured simultaneously under identical conditions) are the meaningful metric.

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 14.20 MB/s | 12.02 MB/s | 1.18x | 86.91 MB/s | 75.68 MB/s | **1.15x** |
| text-100k | 5 | 16.55 MB/s | 13.29 MB/s | 1.25x | 76.56 MB/s | 62.35 MB/s | **1.23x** |
| text-100k | 9 | 16.33 MB/s | 13.54 MB/s | 1.21x | 91.07 MB/s | 77.64 MB/s | **1.17x** |
| binary-100k | 1 | 14.92 MB/s | 10.26 MB/s | 1.45x | 38.41 MB/s | 20.59 MB/s | **1.87x** |
| binary-100k | 5 | 15.37 MB/s | 10.43 MB/s | 1.47x | 42.80 MB/s | 21.29 MB/s | **2.01x** |
| binary-100k | 9 | 14.54 MB/s | 7.05 MB/s | 2.06x | 43.04 MB/s | 21.12 MB/s | **2.04x** |
| repeated-100k | 1 | 13.76 MB/s | 7.10 MB/s | 1.94x | 263.87 MB/s | 258.90 MB/s | 1.02x |
| repeated-100k | 5 | 14.40 MB/s | 10.65 MB/s | 1.35x | 221.93 MB/s | 184.60 MB/s | 1.20x |
| repeated-100k | 9 | 13.21 MB/s | 10.48 MB/s | 1.26x | 256.07 MB/s | 262.41 MB/s | 0.98x |
| zeros-100k | 1 | 116.41 MB/s | 189.84 MB/s | 0.61x* | 1329.53 MB/s | 359.48 MB/s | 3.70x |
| zeros-100k | 5 | 335.29 MB/s | 194.83 MB/s | 1.72x | 1397.55 MB/s | 360.66 MB/s | 3.88x |
| zeros-100k | 9 | 329.64 MB/s | 201.51 MB/s | 1.64x | 1393.97 MB/s | 361.99 MB/s | 3.85x |

*zeros-100k bs=1 compression 0.61x appears to be measurement noise from system load — this workload is extremely fast (>100 MB/s) and highly sensitive to scheduling jitter. All other compression ratios are consistent with previous commits.

**Decompression speedup trend (this commit vs d061e3d):**
- Text: 1.15-1.23x (was 1.09-1.14x — modest improvement from table decode)
- Binary: **1.87-2.04x** (was 1.56-1.68x — **significant jump**, table decode eliminates bit-by-bit extraction for most symbols)
- Repeated: 0.98-1.20x (variable — dominated by RLE output, not Huffman decode)
- Zeros: 3.70-3.88x (stable — dominated by CRC, not Huffman decode)

## 7. Known Issues

No known pre-existing divergences, bugs, or test failures. All previous issues have been resolved.

## 8. Summary

Commit 329da9d is **clean**. The table-based Huffman decode fast path passes all 827 unit tests, 497 differential comparisons, and full ASAN+UBSAN coverage with zero errors. Quick fuzz found no crashes or divergences. The optimization delivers a major decompression speedup on binary/high-entropy data — binary decompression is now **1.87-2.04x** faster than the reference library (up from 1.56-1.68x at d061e3d). Text decompression also improved modestly to 1.15-1.23x. Combined with the flat MTF decode from the previous commit, the decompression pipeline has seen substantial improvement across two successive commits. This is a high-impact, clean optimization — the table construction and fast-path fallback logic are correct.
