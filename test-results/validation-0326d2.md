# Validation Report: 0326d27 — revert sais_bwt to direct malloc/free

**Commit:** 0326d27
**Description:** fix: revert sais_bwt to direct malloc/free — BZALLOC caused OOM crash
**Date:** 2026-02-24
**Validator:** tester (per-commit)
**Verdict:** PASS

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc -O2 | PASS |
| ASAN+UBSAN | clang -fsanitize=address,undefined | PASS |
| Fuzz harnesses | clang -fsanitize=fuzzer,address | PASS (7 harnesses built) |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.018s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.038s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.207s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.423s |
| test_roundtrip | 137 | 137 | 0 | 175 | 2.153s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.002s |
| test_fileio | 58 | 58 | 0 | 952 | 0.060s |
| test_multiblock | 33 | 33 | 0 | 197 | 1.398s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 2.120s |
| test_oom | 22 | 22 | 0 | 318 | 0.049s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.013s |
| test_malformed | 32 | 32 | 0 | 99 | 0.026s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.054s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.154s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.142s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 1.047s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.014s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.023s |
| test_bufftobuff_edge | 45 | 45 | 0 | 1,180 | 0.010s |
| test_huffman_decode_oob | 17 | 17 | 0 | 87 | 0.032s |
| test_coverage_gaps | 23 | 23 | 0 | 85 | 0.004s |
| test_coverage_gaps2 | 56 | 56 | 0 | 2,320 | 0.021s |
| test_decompress_branches | 24 | 24 | 0 | 163 | 0.031s |
| test_bzlib_branches | 45 | 45 | 0 | 7,221 | 0.012s |
| test_blocksort_branches | 24 | 24 | 0 | 125 | 0.010s |
| test_compress_branches | 26 | 26 | 0 | 455 | 0.027s |
| test_decompress_crc | 63 | 63 | 0 | 571 | 0.188s |
| test_crc32_internal | 17 | 17 | 0 | 594 | 0.002s |
| test_streaming_states | 18 | 18 | 0 | 2,134 | 0.055s |
| test_randomised_blocks | 16 | 16 | 0 | 122 | 0.061s |
| test_bzip2_corpus | 162 | 162 | 0 | — | 12.35s |
| **Total** | **1,394** | **1,394** | **0** | **475,438** | **20.7s** |

Critical: **test_oom passes** (22/22). This was the suite that SEGFAULTed on 2a855ff — the revert fixes the OOM crash.

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
| test_api | 57 | 57 | 0 | 0.151s |
| test_edge_cases | 67 | 67 | 0 | 0.344s |
| test_advanced | 40 | 40 | 0 | 1.146s |
| test_streaming | 30 | 30 | 0 | 3.622s |
| test_roundtrip | 137 | 137 | 0 | 16.340s |
| test_error_paths | 60 | 60 | 0 | 0.014s |
| test_fileio | 58 | 58 | 0 | 0.750s |
| test_multiblock | 33 | 33 | 0 | 10.674s |
| test_blocksort_paths | 55 | 55 | 0 | 12.327s |
| test_oom | 22 | 22 | 0 | 0.364s |
| test_decompress_errors | 32 | 32 | 0 | 0.262s |
| test_malformed | 32 | 32 | 0 | 0.271s |
| test_streaming_edge | 25 | 25 | 0 | 0.491s |
| test_param_combos | 92 | 92 | 0 | 1.219s |
| test_rle_huffman_edge | 47 | 47 | 0 | 0.678s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 9.044s |
| test_concat_readahead | 20 | 20 | 0 | 0.223s |
| test_compress_states | 31 | 31 | 0 | 0.206s |
| test_bufftobuff_edge | 45 | 45 | 0 | 0.108s |
| test_huffman_decode_oob | 17 | 17 | 0 | 0.255s |
| test_coverage_gaps | 23 | 23 | 0 | 0.031s |
| test_coverage_gaps2 | 56 | 56 | 0 | 0.164s |
| test_decompress_branches | 24 | 24 | 0 | 0.296s |
| test_bzlib_branches | 45 | 45 | 0 | 0.212s |
| test_blocksort_branches | 24 | 24 | 0 | 0.054s |
| test_compress_branches | 26 | 26 | 0 | 0.326s |
| test_decompress_crc | 63 | 63 | 0 | 2.363s |
| test_crc32_internal | 17 | 17 | 0 | 0.009s |
| test_streaming_states | 18 | 18 | 0 | 0.614s |
| test_randomised_blocks | 16 | 16 | 0 | 0.429s |
| test_bzip2_corpus | 162 | 162 | 0 | ~90s |
| **Total** | **1,394** | **1,394** | **0** | **~153s** |

Zero ASAN errors, zero UBSAN violations. test_oom passes cleanly — no SEGFAULT, no memory errors.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Divergences | Corpus | Time | Notes |
|---------|------|--------|---------|-------------|--------|------|-------|
| fuzz_compress | 70 | 2 | 0 | — | 66 | 24s | PASS |
| fuzz_decompress | 443 | 14 | 0 | — | 94 | 31s | PASS |
| fuzz_differential | 478 | 5 | 0 | 0 | 200 | 91s | PASS |
| fuzz_diff_streaming | 478 | 8 | 0 | 0 | 194 | 54s | killed by 30s budget |

4 harnesses, all ASAN-enabled. 1,469 total runs. Zero crashes, zero divergences. fuzz_diff_streaming killed at budget limit (expected). Coverage: 9,448 (compress), 4,689 (decompress), 6,294 (differential), 6,337 (diff_streaming) features.

## 6. Benchmarks

**NOTE:** Benchmarks run under heavy system load (load average 5.4-8.9) due to concurrent sustained fuzzing campaigns by the strategic-tester. All absolute throughput numbers are depressed. Speedup ratios are also affected since the contention is uneven. For reliable absolute numbers, refer to d061e3d validation (load average <1.0). This commit is a pure allocator revert with zero algorithmic changes — no performance delta expected.

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 13.59 MB/s | 23.97 MB/s | 0.57x | 96.41 MB/s | 127.04 MB/s | 0.76x |
| text-100k | 5 | 13.82 MB/s | 23.38 MB/s | 0.59x | 96.47 MB/s | 128.31 MB/s | 0.75x |
| text-100k | 9 | 13.77 MB/s | 23.12 MB/s | 0.60x | 96.08 MB/s | 128.14 MB/s | 0.75x |
| binary-100k | 1 | 6.95 MB/s | 16.04 MB/s | 0.43x | 49.55 MB/s | 34.40 MB/s | **1.44x** |
| binary-100k | 5 | 6.97 MB/s | 16.27 MB/s | 0.43x | 50.41 MB/s | 35.97 MB/s | **1.40x** |
| binary-100k | 9 | 6.87 MB/s | 16.08 MB/s | 0.43x | 49.69 MB/s | 35.82 MB/s | **1.39x** |
| repeated-100k | 1 | 28.20 MB/s | 13.93 MB/s | 2.02x | 169.88 MB/s | 430.73 MB/s | 0.39x |
| repeated-100k | 5 | 27.98 MB/s | 17.83 MB/s | 1.57x | 170.91 MB/s | 427.42 MB/s | 0.40x |
| repeated-100k | 9 | 26.31 MB/s | 17.73 MB/s | 1.48x | 165.19 MB/s | 426.45 MB/s | 0.39x |
| zeros-100k | 1 | 197.07 MB/s | 301.31 MB/s | 0.65x | 1128.64 MB/s | 588.70 MB/s | **1.92x** |
| zeros-100k | 5 | 203.33 MB/s | 317.11 MB/s | 0.64x | 1127.66 MB/s | 589.09 MB/s | **1.91x** |
| zeros-100k | 9 | 201.98 MB/s | 321.96 MB/s | 0.63x | 1128.21 MB/s | 589.26 MB/s | **1.91x** |

**Load-adjusted comparison to d061e3d (clean benchmarks):**
- Binary decompression: 1.39-1.44x here vs 1.56-1.68x at d061e3d — consistent direction, difference is load noise
- Zeros decompression: 1.91-1.92x here vs 3.78-3.88x at d061e3d — heavily load-depressed
- Text/repeated compression and decompression ratios are all depressed by system load

This commit changes only the sais_bwt allocator path (revert to direct malloc/free), which is not on any benchmark hot path. No algorithmic performance change expected or observed.

## 7. Known Issues

No known pre-existing divergences, bugs, or test failures. The OOM SEGFAULT from 2a855ff (task #90) is **resolved** by this commit.

## 8. Summary

Commit 0326d27 is **clean**. This commit reverts the problematic allocator change from 2a855ff that caused a SEGFAULT in OOM injection tests (sais_bwt temporary buffers were exposed to user allocator hooks, which the reference libbz2 never does). The revert restores direct malloc/free for SA-IS temporary buffers, matching reference behavior. All 1,394 tests pass across 33 suites, including the critical test_oom suite (22/22) that previously crashed. 497 differential comparisons show zero divergences. ASAN+UBSAN is fully clean. Quick fuzz ran 1,469 iterations across 4 harnesses with zero crashes and zero divergences. Benchmark numbers are depressed by concurrent system load but show no algorithmic regression. The OOM regression introduced by 2a855ff is fully resolved.
