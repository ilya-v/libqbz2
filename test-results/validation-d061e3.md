# Validation Report: d061e3d — flatten decompression MTF decode

**Commit:** d061e3d
**Description:** perf: flatten decompression MTF decode to simple array with memmove
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
| test_api | 57 | 57 | 0 | 235 | 0.018s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.036s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.097s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.174s |
| test_roundtrip | 137 | 137 | 0 | 175 | 0.927s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.056s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.577s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 0.971s |
| test_oom | 22 | 22 | 0 | 318 | 0.020s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.010s |
| test_malformed | 32 | 32 | 0 | 99 | 0.012s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.026s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.204s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.141s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.899s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.008s |
| **Total** | **827** | **827** | **0** | **422,179** | **4.18s** |

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **Total** | **497** | **497** | **0** |

All differential tests compare byte-for-byte compressed output between qbz2 and reference libbz2. Zero divergences.

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Time |
|-------|-------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0.185s |
| test_edge_cases | 67 | 67 | 0 | 0.519s |
| test_advanced | 40 | 40 | 0 | 0.796s |
| test_streaming | 30 | 30 | 0 | 1.309s |
| test_roundtrip | 137 | 137 | 0 | 5.944s |
| test_error_paths | 60 | 60 | 0 | 0.009s |
| test_fileio | 58 | 58 | 0 | 0.818s |
| test_multiblock | 33 | 33 | 0 | 4.844s |
| test_blocksort_paths | 55 | 55 | 0 | 7.167s |
| test_oom | 22 | 22 | 0 | 0.256s |
| test_decompress_errors | 32 | 32 | 0 | 0.162s |
| test_malformed | 32 | 32 | 0 | 0.076s |
| test_streaming_edge | 25 | 25 | 0 | 0.232s |
| test_param_combos | 92 | 92 | 0 | 1.540s |
| test_rle_huffman_edge | 47 | 47 | 0 | 0.906s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 12.679s |
| test_concat_readahead | 20 | 20 | 0 | 0.134s |
| **Total** | **827** | **827** | **0** | **37.6s** |

Zero ASAN errors, zero UBSAN violations.

## 5. Quick Fuzz

| Harness | Result | Notes |
|---------|--------|-------|
| fuzz_compress | PASS | 0 crashes |
| fuzz_decompress | PASS | 0 crashes |
| fuzz_differential | PASS | 0 divergences |
| fuzz_diff_streaming | killed by budget | 0 crashes before kill |

4 harnesses, 3 completed within budget, 1 (fuzz_diff_streaming) killed at 30s budget limit. Zero crashes, zero divergences across all harnesses. ASAN-enabled throughout.

## 6. Benchmarks

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 25.61 MB/s | 21.43 MB/s | 1.20x | 129.61 MB/s | 113.96 MB/s | **1.14x** |
| text-100k | 5 | 25.69 MB/s | 21.43 MB/s | 1.20x | 128.97 MB/s | 118.66 MB/s | **1.09x** |
| text-100k | 9 | 25.54 MB/s | 21.24 MB/s | 1.20x | 131.38 MB/s | 119.17 MB/s | **1.10x** |
| binary-100k | 1 | 23.40 MB/s | 15.89 MB/s | 1.47x | 48.82 MB/s | 31.27 MB/s | **1.56x** |
| binary-100k | 5 | 23.97 MB/s | 15.93 MB/s | 1.50x | 49.72 MB/s | 29.61 MB/s | **1.68x** |
| binary-100k | 9 | 22.16 MB/s | 14.35 MB/s | 1.54x | 48.15 MB/s | 30.46 MB/s | **1.58x** |
| repeated-100k | 1 | 20.37 MB/s | 12.03 MB/s | 1.69x | 396.37 MB/s | 393.71 MB/s | 1.01x |
| repeated-100k | 5 | 17.78 MB/s | 16.22 MB/s | 1.10x | 401.43 MB/s | 397.59 MB/s | 1.01x |
| repeated-100k | 9 | 21.73 MB/s | 16.74 MB/s | 1.30x | 405.07 MB/s | 416.35 MB/s | 0.97x |
| zeros-100k | 1 | 485.14 MB/s | 286.70 MB/s | 1.69x | 2161.23 MB/s | 571.41 MB/s | 3.78x |
| zeros-100k | 5 | 561.07 MB/s | 319.94 MB/s | 1.75x | 2217.08 MB/s | 571.87 MB/s | 3.88x |
| zeros-100k | 9 | 554.05 MB/s | 316.06 MB/s | 1.75x | 2221.35 MB/s | 573.92 MB/s | 3.87x |

**Decompression speedup analysis for this commit (flat MTF decode):**
- Text: 1.09-1.14x vs reference (was ~1.01-1.07x at c50993d — meaningful improvement)
- Binary: **1.56-1.68x** vs reference (significant improvement — binary data has high MTF ranks, so the flat array + memmove eliminates cross-segment lookups)
- Repeated: ~1.0x (neutral — repetitive data has low MTF ranks, flat array doesn't help)
- Zeros: 3.78-3.88x (stable — dominated by CRC, not MTF)

## 7. Known Issues

No known pre-existing divergences, bugs, or test failures. All previous issues have been resolved.

## 8. Summary

Commit d061e3d is **clean**. The flat MTF decompression decode passes all 827 unit tests, 497 differential comparisons, and full ASAN+UBSAN coverage with zero errors. Quick fuzz found no crashes or divergences. The optimization delivers significant decompression speedup on binary/high-entropy data (1.56-1.68x vs reference, up from ~1.0x at baseline) by eliminating the two-level MTFA/mtfbase structure. Text decompression also improved to 1.09-1.14x. This is a high-impact, clean optimization.
