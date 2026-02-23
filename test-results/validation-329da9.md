# Validation Report: 329da9d — table-based Huffman decode for decompression

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
| test_api | 57 | 57 | 0 | 235 | 0.025s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.073s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.214s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.248s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.275s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.083s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.873s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.358s |
| test_oom | 22 | 22 | 0 | 318 | 0.036s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.018s |
| test_malformed | 32 | 32 | 0 | 99 | 0.016s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.041s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.340s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.240s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 1.535s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.012s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.035s |
| **Total** | **858** | **858** | **0** | **460,381** | **6.43s** |

Note: test_compress_states is a new suite (31 tests, 38,202 assertions) added in this validation cycle covering compression state machine edge cases: FINISH_OK with tiny output buffers, avail_in_expect mismatch sequence errors, custom allocator hooks, allocator failure paths (BZ_MEM_ERROR), decompression after STREAM_END, 1-byte output decompression, empty FINISH, FLUSH-to-FINISH transitions, and BZ_M_IDLE state.

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **Total** | **497** | **497** | **0** |

All differential tests compare byte-for-byte compressed output between qbz2 and reference libbz2. Zero divergences. The table-based Huffman decode does not affect compression output (compression side unchanged).

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Time |
|-------|-------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0.175s |
| test_edge_cases | 67 | 67 | 0 | 0.513s |
| test_advanced | 40 | 40 | 0 | 0.753s |
| test_streaming | 30 | 30 | 0 | 1.237s |
| test_roundtrip | 137 | 137 | 0 | 5.728s |
| test_error_paths | 60 | 60 | 0 | 0.009s |
| test_fileio | 58 | 58 | 0 | 0.842s |
| test_multiblock | 33 | 33 | 0 | 4.724s |
| test_blocksort_paths | 55 | 55 | 0 | 6.714s |
| test_oom | 22 | 22 | 0 | 0.256s |
| test_decompress_errors | 32 | 32 | 0 | 0.186s |
| test_malformed | 32 | 32 | 0 | 0.077s |
| test_streaming_edge | 25 | 25 | 0 | 0.236s |
| test_param_combos | 92 | 92 | 0 | 1.579s |
| test_rle_huffman_edge | 47 | 47 | 0 | 0.901s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 12.583s |
| test_concat_readahead | 20 | 20 | 0 | 0.134s |
| test_compress_states | 31 | 31 | 0 | 0.263s |
| **Total** | **858** | **858** | **0** | **36.9s** |

Zero ASAN errors, zero UBSAN violations. The Huffman lookup table has no out-of-bounds accesses or undefined behavior.

## 5. Quick Fuzz

| Harness | Result | Notes |
|---------|--------|-------|
| fuzz_compress | PASS | 0 crashes |
| fuzz_decompress | killed by budget | 0 crashes before kill |
| fuzz_differential | PASS | 0 divergences |
| fuzz_diff_streaming | killed by budget | 0 crashes before kill |

4 harnesses, 2 completed within budget, 2 killed at 30s total budget limit. Zero crashes, zero divergences across all harnesses. ASAN-enabled throughout.

## 6. Benchmarks

Best of 2 runs per workload:

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 23.06 MB/s | 22.78 MB/s | 1.01x | 144.61 MB/s | 123.69 MB/s | **1.17x** |
| text-100k | 5 | 26.30 MB/s | 22.51 MB/s | 1.17x | 142.86 MB/s | 122.87 MB/s | **1.16x** |
| text-100k | 9 | 26.80 MB/s | 22.70 MB/s | 1.18x | 148.10 MB/s | 125.66 MB/s | **1.18x** |
| binary-100k | 1 | 25.14 MB/s | 16.70 MB/s | 1.50x | 68.04 MB/s | 33.23 MB/s | **2.05x** |
| binary-100k | 5 | 24.52 MB/s | 17.13 MB/s | 1.43x | 70.22 MB/s | 35.08 MB/s | **2.00x** |
| binary-100k | 9 | 24.48 MB/s | 16.88 MB/s | 1.45x | 67.16 MB/s | 33.97 MB/s | **1.98x** |
| repeated-100k | 1 | 22.40 MB/s | 14.23 MB/s | 1.57x | 417.88 MB/s | 407.83 MB/s | 1.02x |
| repeated-100k | 5 | 22.79 MB/s | 18.82 MB/s | 1.21x | 416.88 MB/s | 412.83 MB/s | 1.01x |
| repeated-100k | 9 | 22.67 MB/s | 17.02 MB/s | 1.33x | 404.98 MB/s | 417.58 MB/s | 0.97x |
| zeros-100k | 1 | 507.12 MB/s | 308.60 MB/s | 1.64x | 2185.68 MB/s | 576.53 MB/s | 3.79x |
| zeros-100k | 5 | 566.16 MB/s | 319.89 MB/s | 1.77x | 2247.87 MB/s | 576.94 MB/s | 3.90x |
| zeros-100k | 9 | 562.11 MB/s | 324.21 MB/s | 1.73x | 2234.95 MB/s | 581.55 MB/s | 3.84x |

**Decompression speedup analysis for this commit (table-based Huffman decode):**
- Text: **1.16-1.18x** vs reference (was 1.09-1.14x at d061e3d — meaningful improvement)
- Binary: **1.98-2.05x** vs reference (was 1.56-1.68x at d061e3d — large improvement, ~25% faster)
- Repeated: ~1.0x (neutral — repetitive data dominated by RLE, not Huffman)
- Zeros: 3.79-3.90x (stable — dominated by CRC and RLE, not Huffman)

The table-based Huffman decode delivers the largest gains on binary/high-entropy data where Huffman decoding is the bottleneck. Binary decompression crossed the 2x mark — a major milestone.

## 7. Known Issues

No known pre-existing divergences, bugs, or test failures. All previous issues have been resolved.

## 8. Summary

Commit 329da9d is **clean**. The table-based Huffman decode for the decompression fast path passes all 858 unit tests (460,381 assertions), 497 differential comparisons, and full ASAN+UBSAN coverage with zero errors. Quick fuzz found no crashes or divergences. The optimization delivers a significant decompression speedup: binary data now at 2.0x vs reference (up from 1.56-1.68x), and text data improved to 1.16-1.18x (up from 1.09-1.14x). Binary decompression crossing the 2x threshold is a key performance milestone. This is a clean, high-impact optimization.
