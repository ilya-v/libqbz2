# Validation Report: 5ba2e2c — fix Huffman table degenerate code rejection

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
| Fuzz harnesses | clang -fsanitize=fuzzer,address | PASS |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.010s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.015s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.095s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.190s |
| test_roundtrip | 137 | 137 | 0 | 175 | 0.933s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.023s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.612s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 0.956s |
| test_oom | 22 | 22 | 0 | 318 | 0.019s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.008s |
| test_malformed | 32 | 32 | 0 | 99 | 0.016s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.024s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.091s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.104s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.462s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.006s |
| test_coverage_gaps | 23 | 23 | 0 | 85 | 0.002s |
| test_coverage_gaps2 | 56 | 56 | 0 | 2,320 | 0.007s |
| test_coverage_gaps3 | 37 | 37 | 0 | 1,861 | 0.001s |
| test_crc32_internal | 17 | 17 | 0 | 594 | 0.002s |
| test_decompress_branches | 24 | 24 | 0 | 163 | 0.017s |
| test_decompress_crc | 63 | 63 | 0 | 571 | 0.096s |
| test_huffman_decode_oob | 17 | 17 | 0 | 87 | 0.021s |
| test_blocksort_branches | 24 | 24 | 0 | 125 | 0.005s |
| test_bufftobuff_edge | 45 | 45 | 0 | 1,180 | 0.005s |
| test_bzlib_branches | 45 | 45 | 0 | 7,221 | 0.006s |
| test_compress_branches | 26 | 26 | 0 | 455 | 0.016s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.008s |
| test_randomised_blocks | 16 | 16 | 0 | 122 | 0.034s |
| test_streaming_states | 18 | 18 | 0 | 2,134 | 0.021s |
| test_diff_error_codes | 22 | 22 | 0 | 27 | 0.144s |
| **Framework subtotal** | **1,291** | **1,291** | **0** | **477,326** | **3.97s** |

| Suite (differential format) | Inputs | Passed | Divergences |
|-----------------------------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **Differential subtotal** | **497** | **497** | **0** |

**Grand total: 1,788 tests, 1,788 passed, 0 failed.**

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| test_diff_error_codes | 3,148 | 3,148 | 0 |
| **Total** | **3,645** | **3,645** | **0** |

All differential tests compare byte-for-byte compressed/decompressed output and error codes between qbz2 and reference libbz2. The 4 previously known divergences (bitflip positions where qbz2 rejected degenerate Huffman codes that the reference accepted) are now **resolved** by this commit. Zero divergences remain across all 3,645 comparisons.

## 4. ASAN+UBSAN

| Metric | Value |
|--------|-------|
| Total tests | 1,788 |
| Passed | 1,788 |
| Failed | 0 |
| ASAN errors | 0 |
| UBSAN violations | 0 |
| Total time | ~37s |

All 35 test executables ran under ASAN+UBSAN with zero memory errors and zero undefined behavior violations.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Divergences | Time |
|---------|------|--------|---------|-------------|------|
| fuzz_compress | 120 | 5 | 0 | N/A | 22s |
| fuzz_decompress | 540 | 33 | 0 | N/A | 16s |
| fuzz_differential | 514 | 36 | 0 | 0 | 14s |
| fuzz_diff_streaming | 528 | 37 | 0 | 0 | 14s |
| **Total** | **1,702** | -- | **0** | **0** | ~30s |

4 harnesses, all ASAN-enabled. Zero crashes, zero divergences. The low exec/s for fuzz_compress is expected (large seed corpus with multi-block inputs up to 1MB).

## 6. Benchmarks

System load 2.5/16 cores at time of measurement.

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 31.05 MB/s | 23.66 MB/s | 1.31x | 145.86 MB/s | 125.89 MB/s | **1.16x** |
| text-100k | 5 | 33.79 MB/s | 23.13 MB/s | 1.46x | 145.89 MB/s | 127.17 MB/s | **1.15x** |
| text-100k | 9 | 33.66 MB/s | 23.10 MB/s | 1.46x | 146.99 MB/s | 126.75 MB/s | **1.16x** |
| binary-100k | 1 | 17.13 MB/s | 16.16 MB/s | 1.06x | 71.02 MB/s | 34.40 MB/s | **2.06x** |
| binary-100k | 5 | 17.05 MB/s | 17.10 MB/s | 1.00x | 70.99 MB/s | 35.47 MB/s | **2.00x** |
| binary-100k | 9 | 16.44 MB/s | 17.08 MB/s | 0.96x | 69.91 MB/s | 35.42 MB/s | **1.97x** |
| repeated-100k | 1 | 66.52 MB/s | 14.06 MB/s | 4.73x | 384.69 MB/s | 427.86 MB/s | 0.90x |
| repeated-100k | 5 | 62.81 MB/s | 17.83 MB/s | 3.52x | 390.95 MB/s | 432.00 MB/s | 0.90x |
| repeated-100k | 9 | 58.46 MB/s | 16.35 MB/s | 3.58x | 371.79 MB/s | 431.47 MB/s | 0.86x |
| zeros-100k | 1 | 585.64 MB/s | 306.46 MB/s | 1.91x | 3739.25 MB/s | 587.38 MB/s | **6.37x** |
| zeros-100k | 5 | 637.62 MB/s | 330.76 MB/s | 1.93x | 3764.63 MB/s | 591.35 MB/s | **6.37x** |
| zeros-100k | 9 | 624.77 MB/s | 328.48 MB/s | 1.90x | 3752.86 MB/s | 586.29 MB/s | **6.40x** |

**Performance analysis:**
- No measurable performance regression from the Huffman table fix. The graceful degradation path only triggers for degenerate code tables, never during normal operation.
- Text: 1.31-1.46x compress, 1.15-1.16x decompress (stable)
- Binary: 1.00-1.06x compress, 1.97-2.06x decompress (stable)
- Repeated: 3.52-4.73x compress, 0.86-0.90x decompress (repeated decompression regression is pre-existing)
- Zeros: 1.90-1.93x compress, 6.37-6.40x decompress (stable)

## 7. Known Issues

**No known pre-existing divergences, bugs, or test failures.**

The 4 Huffman code validation divergences discovered in the previous validation cycle (f14888) are now **resolved** by this commit. qbz2 previously rejected degenerate Huffman code tables at 4 specific bitflip positions (byte 29 bits 5,7; byte 30 bits 3,5 of compressed "bit flip sweep data" at bs=1) where the reference library accepted and decoded successfully. The fix gracefully degrades the fast Huffman lookup table to bit-by-bit decoding for degenerate codes instead of returning BZ_DATA_ERROR.

The repeated-data decompression regression (0.86-0.90x vs reference) is a pre-existing performance characteristic, not a correctness issue. It has been stable across many commits.

## 8. Summary

Commit 5ba2e2c is **clean** and resolves a critical conformance bug. The fix corrects 4 error code divergences where qbz2's fast Huffman decode table rejected degenerate-but-valid Huffman code parameters that the reference libbz2 accepts. Instead of returning BZ_DATA_ERROR, the fast table construction now falls through to the standard slow decode path. All 1,788 unit tests pass, 3,645 differential comparisons show zero divergences (first time in project history), ASAN+UBSAN finds zero errors across all suites, and quick fuzz (1,702 runs across 4 harnesses) finds zero crashes and zero divergences. Performance is unchanged -- the graceful degradation path never triggers on well-formed input.
