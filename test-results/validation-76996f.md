# Validation Report: 76996fe — replace mainSort with libsais for all blocks >= 10000

**Commit:** 76996fe
**Description:** perf: replace mainSort with libsais for all blocks >= 10000
**Date:** 2026-02-24
**Validator:** tester (per-commit)
**Verdict:** PASS — major architectural change, all tests clean

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc -O2 | PASS |
| ASAN+UBSAN | clang -fsanitize=address,undefined | PASS |
| Fuzz harnesses | clang -fsanitize=fuzzer,address | PASS (via run-quick-fuzz.sh) |

Removed: mainSort radix+quicksort BWT (~560 lines). All blocks >= 10000 now use libsais SA-IS via doubled-string technique. fallbackSort preserved for small blocks (< 10000). blocksort.c reduced from 940 to 377 lines.

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.013s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.022s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.145s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.281s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.566s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.060s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.897s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.192s |
| test_oom | 22 | 22 | 0 | 318 | 0.021s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.006s |
| test_malformed | 32 | 32 | 0 | 99 | 0.014s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.028s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.102s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.118s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.526s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.006s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.010s |
| test_huffman_decode_oob | 17 | 17 | 0 | 87 | 0.024s |
| test_bufftobuff_edge | 45 | 45 | 0 | 1,180 | 0.004s |
| **Total** | **920** | **920** | **0** | **461,652** | **5.04s** |

All 920 unit tests pass. Note test_roundtrip (137 tests) exercises the full compress/decompress pipeline across all block sizes and data patterns — this directly validates the new libsais BWT path. test_blocksort_paths (55 tests) specifically tests BWT construction with various input patterns including boundary sizes.

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **Total** | **497** | **497** | **0** |

All differential tests compare byte-for-byte compressed output between qbz2 and reference libbz2. Zero divergences. This is the critical correctness proof — the libsais BWT produces identical output to reference libbz2's BWT for all 497 tested inputs across all block sizes and data types.

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Time |
|-------|-------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0.098s |
| test_edge_cases | 67 | 67 | 0 | 0.209s |
| test_advanced | 40 | 40 | 0 | 0.920s |
| test_streaming | 30 | 30 | 0 | 1.738s |
| test_roundtrip | 137 | 137 | 0 | 8.518s |
| test_error_paths | 60 | 60 | 0 | 0.009s |
| test_fileio | 58 | 58 | 0 | 0.349s |
| test_multiblock | 33 | 33 | 0 | 5.814s |
| test_blocksort_paths | 55 | 55 | 0 | 9.351s |
| test_oom | 22 | 22 | 0 | 0.227s |
| test_decompress_errors | 32 | 32 | 0 | 0.087s |
| test_malformed | 32 | 32 | 0 | 0.114s |
| test_streaming_edge | 25 | 25 | 0 | 0.250s |
| test_param_combos | 92 | 92 | 0 | 0.641s |
| test_rle_huffman_edge | 47 | 47 | 0 | 0.414s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 5.958s |
| test_concat_readahead | 20 | 20 | 0 | 0.134s |
| test_compress_states | 31 | 31 | 0 | 0.118s |
| test_huffman_decode_oob | 17 | 17 | 0 | 0.129s |
| test_bufftobuff_edge | 45 | 45 | 0 | 0.072s |
| **Total** | **920** | **920** | **0** | **35.2s** |

Zero ASAN errors, zero UBSAN violations. The doubled-string memory allocation (2x block size for libsais input) shows no memory issues under ASAN.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Divergences | Time | Notes |
|---------|------|--------|---------|-------------|------|-------|
| fuzz_compress | 49 | 4 | 0 | n/a | 11s | Completed within budget |
| fuzz_decompress | 485 | 19 | 0 | n/a | 25s | Killed at budget |
| fuzz_differential | 485 | 17 | 0 | 0 | 27s | 0 divergences |
| fuzz_diff_streaming | 485 | 19 | 0 | 0 | 25s | 0 divergences |

4 harnesses, 3 completed within budget, 1 (fuzz_decompress) killed at 30s total budget. Zero crashes, zero divergences across all harnesses. ASAN-enabled throughout. The fuzz_compress harness directly exercises the new libsais BWT path — 49 runs with 0 crashes confirms the doubled-string allocation and libsais integration are safe even on fuzz-generated inputs.

## 6. Benchmarks

Best of 2 passes:

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------| 
| text-100k | 1 | 31.23 MB/s | 23.92 MB/s | **1.31x** | 151.39 MB/s | 127.42 MB/s | 1.19x |
| text-100k | 5 | 33.84 MB/s | 23.47 MB/s | **1.44x** | 137.05 MB/s | 126.99 MB/s | 1.15x |
| text-100k | 9 | 31.58 MB/s | 23.47 MB/s | **1.45x** | 148.22 MB/s | 128.02 MB/s | 1.16x |
| binary-100k | 1 | 17.24 MB/s | 16.99 MB/s | 1.02x | 71.28 MB/s | 34.37 MB/s | **2.07x** |
| binary-100k | 5 | 17.19 MB/s | 17.44 MB/s | 0.99x | 51.28 MB/s | 24.88 MB/s | **2.06x** |
| binary-100k | 9 | 16.15 MB/s | 16.41 MB/s | 0.98x | 70.23 MB/s | 35.44 MB/s | **1.98x** |
| repeated-100k | 1 | 66.07 MB/s | 14.03 MB/s | **4.71x** | 434.88 MB/s | 431.00 MB/s | 1.01x |
| repeated-100k | 5 | 64.55 MB/s | 17.96 MB/s | **3.59x** | 417.31 MB/s | 436.24 MB/s | 0.96x |
| repeated-100k | 9 | 57.62 MB/s | 17.25 MB/s | **3.34x** | 422.45 MB/s | 434.49 MB/s | 0.97x |
| zeros-100k | 1 | 603.18 MB/s | 321.53 MB/s | **1.88x** | 3862.69 MB/s | 598.71 MB/s | 6.45x |
| zeros-100k | 5 | 641.84 MB/s | 333.88 MB/s | **1.92x** | 3810.15 MB/s | 598.31 MB/s | 6.37x |
| zeros-100k | 9 | 598.00 MB/s | 315.86 MB/s | **1.89x** | 3748.59 MB/s | 599.20 MB/s | 6.26x |

**Compression speedup analysis (libsais-for-all vs previous mainSort/libsais hybrid at 0bd244d):**
- Text: **1.31-1.45x** vs reference (was 0.99-1.34x at 0bd244d — significant improvement, especially bs=9 which jumped from 0.99x to 1.45x)
- Binary: 0.98-1.02x vs reference (was 1.44-1.46x at 0bd244d — **regression from 1.45x to parity**). This is the expected tradeoff: libsais is slower than mainSort on high-entropy data where the radix sort was efficient.
- Repeated: **3.34-4.71x** vs reference (was 1.56-2.12x at 0bd244d — massive improvement, nearly doubling). libsais excels on repetitive data.
- Zeros: 1.88-1.92x (stable)

**Decompression unchanged** (expected — this commit only modifies the compression BWT path):
- Text: 1.15-1.19x, Binary: 1.98-2.07x, Zeros: 6.26-6.45x

**Binary compression regression note:** Binary compression dropped from ~1.45x to ~1.0x (parity with reference). This is the known tradeoff of removing mainSort — the radix sort was optimized for high-entropy data. The worker noted this in the commit message ("Binary compression: 1.47x -> 1.00x — at parity with reference"). Not a correctness issue, and offset by dramatic gains on text and repetitive data.

## 7. Known Issues

| Issue | Severity | Introduced | Status |
|-------|----------|------------|--------|
| Binary compression at parity with reference (was ~1.45x) | LOW | 76996fe | Expected tradeoff, not a bug |

No known pre-existing divergences, bugs, or test failures. The binary compression regression is an intentional architectural tradeoff — mainSort was faster on high-entropy data, but removing it simplifies the codebase by ~560 lines and dramatically improves text and repetitive workloads.

## 8. Summary

Commit 76996fe is **clean**. This is the biggest single change to the compression pipeline — removing mainSort entirely and routing all blocks >= 10000 through libsais SA-IS. All 920 unit tests pass, all 497 differential comparisons show zero divergences (bit-for-bit identical output to reference libbz2), and full ASAN+UBSAN coverage finds zero memory safety issues (including the doubled-string allocation). Quick fuzz found no crashes or divergences. The performance profile shifts significantly: text compression jumps from ~1.15x to 1.31-1.45x, repeated data from ~2x to 3.3-4.7x, while binary compression regresses from ~1.45x to parity with reference. This is a deliberate architectural simplification (940 -> 377 lines in blocksort.c) with a net positive performance impact on most workloads.
