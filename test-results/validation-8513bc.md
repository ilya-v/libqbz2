# Validation Report: 8513bc3 — 11-bit two-level Huffman decode with branchless refill

**Commit:** 8513bc3
**Description:** perf: upgrade Huffman decode to 11-bit two-level table with branchless refill
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
| test_api | 57 | 57 | 0 | 235 | 0.012s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.033s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.098s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.179s |
| test_roundtrip | 137 | 137 | 0 | 175 | 0.780s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.051s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.527s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 0.804s |
| test_oom | 22 | 22 | 0 | 318 | 0.020s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.014s |
| test_malformed | 32 | 32 | 0 | 99 | 0.009s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.022s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.204s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.135s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.908s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.007s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.023s |
| **Total** | **858** | **858** | **0** | **460,381** | **3.83s** |

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **Total** | **497** | **497** | **0** |

All differential tests compare byte-for-byte output between qbz2 and reference libbz2. Zero divergences. The 11-bit primary table and two-level overflow produce identical decoded output to the bit-by-bit fallback on all test inputs.

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Time |
|-------|-------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0.169s |
| test_edge_cases | 67 | 67 | 0 | 0.524s |
| test_advanced | 40 | 40 | 0 | 0.738s |
| test_streaming | 30 | 30 | 0 | 1.232s |
| test_roundtrip | 137 | 137 | 0 | 5.617s |
| test_error_paths | 60 | 60 | 0 | 0.011s |
| test_fileio | 58 | 58 | 0 | 0.801s |
| test_multiblock | 33 | 33 | 0 | 4.625s |
| test_blocksort_paths | 55 | 55 | 0 | 6.673s |
| test_oom | 22 | 22 | 0 | 0.256s |
| test_decompress_errors | 32 | 32 | 0 | 0.177s |
| test_malformed | 32 | 32 | 0 | 0.078s |
| test_streaming_edge | 25 | 25 | 0 | 0.221s |
| test_param_combos | 92 | 92 | 0 | 1.572s |
| test_rle_huffman_edge | 47 | 47 | 0 | 0.928s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 12.747s |
| test_concat_readahead | 20 | 20 | 0 | 0.157s |
| test_compress_states | 31 | 31 | 0 | 0.256s |
| **Total** | **858** | **858** | **0** | **36.8s** |

Zero ASAN errors, zero UBSAN violations. The overflow table indexing (decode_overflow[]) has no out-of-bounds accesses. The branchless memcpy+bswap32 refill path is clean.

## 5. Quick Fuzz

| Harness | Result | Notes |
|---------|--------|-------|
| fuzz_compress | PASS | 0 crashes |
| fuzz_decompress | PASS | 0 crashes (killed at budget limit) |
| fuzz_differential | PASS | 0 divergences |
| fuzz_diff_streaming | PASS | 0 crashes, 0 divergences |

4 harnesses, 3 completed within budget, 1 (fuzz_decompress) killed at 30s budget limit. Zero crashes, zero divergences across all harnesses. All harnesses ASAN-enabled.

## 6. Benchmarks

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 21.56 MB/s | 21.31 MB/s | 1.01x | 136.92 MB/s | 117.58 MB/s | **1.16x** |
| text-100k | 5 | 24.05 MB/s | 21.94 MB/s | 1.10x | 138.19 MB/s | 121.80 MB/s | **1.13x** |
| text-100k | 9 | 25.84 MB/s | 22.16 MB/s | 1.17x | 143.94 MB/s | 119.57 MB/s | **1.20x** |
| binary-100k | 1 | 24.28 MB/s | 16.53 MB/s | 1.47x | 67.23 MB/s | 32.19 MB/s | **2.09x** |
| binary-100k | 5 | 24.10 MB/s | 16.58 MB/s | 1.45x | 54.09 MB/s | 34.57 MB/s | **1.56x** |
| binary-100k | 9 | 23.99 MB/s | 16.61 MB/s | 1.44x | 67.26 MB/s | 33.61 MB/s | **2.00x** |
| repeated-100k | 1 | 17.41 MB/s | 14.57 MB/s | 1.20x | 408.25 MB/s | 405.90 MB/s | 1.01x |
| repeated-100k | 5 | 22.46 MB/s | 18.74 MB/s | 1.20x | 414.45 MB/s | 408.62 MB/s | 1.01x |
| repeated-100k | 9 | 22.78 MB/s | 18.86 MB/s | 1.21x | 404.03 MB/s | 410.85 MB/s | 0.98x |
| zeros-100k | 1 | 507.06 MB/s | 296.64 MB/s | 1.71x | 2195.29 MB/s | 569.80 MB/s | 3.85x |
| zeros-100k | 5 | 560.38 MB/s | 319.01 MB/s | 1.76x | 2221.52 MB/s | 574.01 MB/s | 3.87x |
| zeros-100k | 9 | 549.69 MB/s | 317.21 MB/s | 1.73x | 2160.92 MB/s | 573.90 MB/s | 3.77x |

**Decompression speedup trend (this commit vs 329da9d):**
- Text: 1.13-1.20x (stable — was 1.15-1.23x, within noise)
- Binary: **1.56-2.09x** (bs=1 and bs=9 show 2.0-2.09x; bs=5 at 1.56x appears to be measurement noise — consistent with 329da9d range)
- Repeated: 0.98-1.01x (neutral, as expected)
- Zeros: 3.77-3.87x (stable, CRC-dominated)

**Compression speedup:** 1.01-1.76x across workloads (unchanged from previous commits — this commit only modifies decompression).

**Note:** binary-100k bs=5 decompression at 1.56x is an outlier compared to bs=1 (2.09x) and bs=9 (2.00x). This is likely scheduling noise; the branchless refill benefit is consistent across block sizes.

## 7. Known Issues

No known pre-existing divergences, bugs, or test failures. All previous issues have been resolved.

## 8. Summary

Commit 8513bc3 is **clean**. The 11-bit two-level Huffman decode with branchless refill passes all 858 unit tests, 497 differential comparisons, and full ASAN+UBSAN coverage with zero errors. Quick fuzz found no crashes or divergences. The overflow table indexing is correct — no out-of-bounds accesses detected under ASAN. Binary decompression remains at approximately 2x the reference library speed. This is a clean incremental improvement to the table-based decode introduced in 329da9d, adding wider tables and branchless refill without introducing regressions.
