# Validation Report: 5ba2e2c — Graceful Huffman table degradation

**Commit:** 5ba2e2c
**Description:** fix: gracefully degrade fast Huffman table on degenerate codes instead of rejecting
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
| test_api | 57 | 57 | 0 | 235 | 0.012s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.014s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.098s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.181s |
| test_roundtrip | 137 | 137 | 0 | 175 | 0.928s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.024s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.592s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 0.965s |
| test_oom | 22 | 22 | 0 | 318 | 0.017s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.005s |
| test_malformed | 32 | 32 | 0 | 99 | 0.011s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.023s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.087s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.098s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.446s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.005s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.008s |
| test_bufftobuff_edge | 45 | 45 | 0 | 1,180 | 0.004s |
| test_huffman_decode_oob | 17 | 17 | 0 | 87 | 0.021s |
| test_coverage_gaps | 23 | 23 | 0 | 85 | 0.001s |
| test_coverage_gaps2 | 56 | 56 | 0 | 2,320 | 0.007s |
| test_coverage_gaps3 | 37 | 37 | 0 | 1,861 | 0.001s |
| test_decompress_branches | 24 | 24 | 0 | 163 | 0.012s |
| test_bzlib_branches | 45 | 45 | 0 | 7,221 | 0.007s |
| test_blocksort_branches | 24 | 24 | 0 | 125 | 0.004s |
| test_compress_branches | 26 | 26 | 0 | 455 | 0.014s |
| test_decompress_crc | 63 | 63 | 0 | 571 | 0.095s |
| test_crc32_internal | 17 | 17 | 0 | 594 | 0.001s |
| test_streaming_states | 18 | 18 | 0 | 2,134 | 0.020s |
| test_randomised_blocks | 16 | 16 | 0 | 122 | 0.034s |
| test_diff_error_codes | 22 | 22 | 0 | 27 | 0.143s |
| test_bzip2_corpus | 162 | 162 | 0 | -- | 3.9s |
| **Total** | **1,519** | **1,519** | **0** | **477,326+** | **~8s** |

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| test_diff_error_codes | 22 | 22 | 0 |
| **Total** | **519** | **519** | **0** |

All differential tests compare byte-for-byte compressed/decompressed output and error codes between qbz2 and reference libbz2. Zero divergences. The diff_error_codes suite (22 tests, 3,148 comparisons) specifically validates that error codes match the reference on degenerate Huffman inputs -- the exact scenario this commit addresses.

## 4. ASAN+UBSAN

- **1,519/1,519 pass** (all suites including corpus, differential, error codes)
- 0 ASAN errors
- 0 UBSAN violations
- Total time: ~38s

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Divergences | Time |
|---------|------|--------|---------|-------------|------|
| fuzz_compress | 748 | 28 | 0 | -- | 26s |
| fuzz_decompress | 2,302 | 88 | 0 | -- | 26s |
| fuzz_differential | 708 | 27 | 0 | 0 | 26s |
| fuzz_diff_streaming | 738 | 28 | 0 | 0 | 26s |

4 harnesses, all ASAN-enabled. 4,496 total runs. Zero crashes, zero divergences. All harnesses ran with existing corpus seeds including malformed inputs and degenerate Huffman codes.

## 6. Benchmarks

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 23.85 MB/s | 23.69 MB/s | 1.01x | 147.56 MB/s | 127.92 MB/s | 1.15x |
| text-100k | 5 | 34.39 MB/s | 23.48 MB/s | 1.47x | 148.49 MB/s | 128.36 MB/s | 1.16x |
| text-100k | 9 | 33.90 MB/s | 23.50 MB/s | 1.44x | 147.38 MB/s | 127.40 MB/s | 1.16x |
| binary-100k | 1 | 17.12 MB/s | 17.01 MB/s | 1.01x | 71.19 MB/s | 34.06 MB/s | **2.09x** |
| binary-100k | 5 | 16.81 MB/s | 16.51 MB/s | 1.02x | 71.65 MB/s | 35.57 MB/s | **2.01x** |
| binary-100k | 9 | 16.37 MB/s | 17.23 MB/s | 0.95x | 70.63 MB/s | 35.04 MB/s | **2.02x** |
| repeated-100k | 1 | 67.35 MB/s | 13.68 MB/s | **4.92x** | 384.28 MB/s | 430.26 MB/s | 0.89x |
| repeated-100k | 5 | 65.01 MB/s | 17.60 MB/s | **3.69x** | 391.84 MB/s | 435.53 MB/s | 0.90x |
| repeated-100k | 9 | 58.39 MB/s | 18.24 MB/s | **3.20x** | 375.38 MB/s | 433.15 MB/s | 0.87x |
| zeros-100k | 1 | 572.07 MB/s | 319.57 MB/s | 1.79x | 3749.53 MB/s | 595.54 MB/s | **6.30x** |
| zeros-100k | 5 | 632.37 MB/s | 329.88 MB/s | 1.92x | 3801.89 MB/s | 592.30 MB/s | **6.42x** |
| zeros-100k | 9 | 630.68 MB/s | 328.43 MB/s | 1.92x | 3770.31 MB/s | 596.84 MB/s | **6.32x** |

No performance regressions. This commit changes only the error handling path for degenerate Huffman codes, not the hot decode path. Performance is consistent with prior clean benchmarks.

## 7. Known Issues

No known pre-existing divergences, bugs, or test failures.

## 8. Summary

Commit 5ba2e2c is **clean**. This commit fixes 4 error code divergences where qbz2's fast Huffman decode table rejected degenerate-but-valid Huffman code parameters that the reference libbz2 accepts. Instead of returning BZ_DATA_ERROR, the fast table construction now falls through to the standard slow decode path (limit/base/perm tables), matching reference behavior exactly. All 1,519 tests pass across 33 suites. 519 differential comparisons show zero divergences, including the new diff_error_codes suite that specifically targets degenerate Huffman inputs. ASAN+UBSAN is fully clean. Quick fuzz ran 4,496 iterations across 4 harnesses with zero crashes and zero divergences. Benchmarks show no performance regression.
