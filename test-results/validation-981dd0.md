# Validation Report: 981dd00 — fix bounds checks in Huffman decode table builder

**Commit:** 981dd00
**Description:** fix: add bounds checks to Huffman decode table builder
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
| test_api | 57 | 57 | 0 | 235 | 0.022s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.054s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.099s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.244s |
| test_roundtrip | 137 | 137 | 0 | 175 | 1.239s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.081s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.852s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 1.408s |
| test_oom | 22 | 22 | 0 | 318 | 0.036s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.019s |
| test_malformed | 32 | 32 | 0 | 99 | 0.011s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.028s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.292s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.071s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,899 | 1.051s |
| test_concat_readahead | 20 | 20 | 0 | 1,194 | 0.004s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.044s |
| **Total** | **858** | **858** | **0** | **459,463** | **5.56s** |

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| **Total** | **497** | **497** | **0** |

All differential tests compare byte-for-byte output between qbz2 and reference libbz2. Zero divergences.

## 4. ASAN+UBSAN

| Suite | Tests | Passed | Failed | Time |
|-------|-------|--------|--------|------|
| test_api | 57 | 57 | 0 | 0.200s |
| test_edge_cases | 67 | 67 | 0 | 0.652s |
| test_advanced | 40 | 40 | 0 | 1.049s |
| test_streaming | 30 | 30 | 0 | 1.523s |
| test_roundtrip | 137 | 137 | 0 | 7.010s |
| test_error_paths | 60 | 60 | 0 | 0.010s |
| test_fileio | 58 | 58 | 0 | 0.914s |
| test_multiblock | 33 | 33 | 0 | 5.688s |
| test_blocksort_paths | 55 | 55 | 0 | 8.223s |
| test_oom | 22 | 22 | 0 | 0.323s |
| test_decompress_errors | 32 | 32 | 0 | 0.255s |
| test_malformed | 32 | 32 | 0 | 0.111s |
| test_streaming_edge | 25 | 25 | 0 | 0.319s |
| test_param_combos | 92 | 92 | 0 | 1.891s |
| test_rle_huffman_edge | 47 | 47 | 0 | 1.182s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 15.007s |
| test_concat_readahead | 20 | 20 | 0 | 0.116s |
| test_compress_states | 31 | 31 | 0 | 0.384s |
| **Total** | **858** | **858** | **0** | **44.9s** |

Zero ASAN errors, zero UBSAN violations.

### Crash Reproducer Verification (ASAN-enabled)

Both crash reproducers from bug-huffman-decode-oob-8513bc3 were tested under ASAN:

| Reproducer | Size | Result | Return Code |
|------------|------|--------|-------------|
| huffman-overflow-subtable.bz2 | 63,414 bytes | BZ_DATA_ERROR (-4) | Clean, no ASAN errors |
| huffman-overflow-primary.bz2 | 500,000 bytes | BZ_DATA_ERROR (-4) | Clean, no ASAN errors |

Both reproducers that previously caused heap buffer overflows (SEGV / heap-buffer-overflow WRITE) now return BZ_DATA_ERROR cleanly, matching reference libbz2 behavior. The bounds checks in the Huffman decode table builder correctly reject the malformed Huffman code length arrays.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Corpus Size | Time | Notes |
|---------|------|--------|---------|-------------|------|-------|
| fuzz_compress | 49 | 3 | 0 | 46 files/10MB | 13s | Completed within budget |
| fuzz_decompress | 485 | 16 | 0 | — | 30s | Killed at budget (ran twice: 443+485 runs) |
| fuzz_differential | 485 | 16 | 0 | 195 files/24MB | 30s | 0 divergences |
| fuzz_diff_streaming | 485 | 13 | 0 | 201 files/26MB | 35s | 0 divergences, killed at total budget |

4 harnesses, 2 completed within per-harness budget, 2 killed at total 30s budget. Zero crashes, zero divergences across all harnesses. All ASAN-enabled. Updated seed corpus includes bzip2-tests repo files (d855276, d3ea17c).

## 6. Benchmarks

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 16.41 MB/s | 13.98 MB/s | 1.17x | 93.90 MB/s | 83.27 MB/s | **1.13x** |
| text-100k | 5 | 16.49 MB/s | 14.04 MB/s | 1.17x | 99.30 MB/s | 87.41 MB/s | **1.14x** |
| text-100k | 9 | 16.42 MB/s | 13.76 MB/s | 1.19x | 100.44 MB/s | 87.70 MB/s | **1.15x** |
| binary-100k | 1 | 16.68 MB/s | 9.27 MB/s | 1.80x | 41.89 MB/s | 21.96 MB/s | **1.91x** |
| binary-100k | 5 | 16.79 MB/s | 9.27 MB/s | 1.81x | 44.43 MB/s | 22.21 MB/s | **2.00x** |
| binary-100k | 9 | 16.22 MB/s | 9.04 MB/s | 1.79x | 43.58 MB/s | 22.18 MB/s | **1.96x** |
| repeated-100k | 1 | 10.53 MB/s | 6.65 MB/s | 1.58x | 262.60 MB/s | 235.89 MB/s | 1.11x |
| repeated-100k | 5 | 10.71 MB/s | 8.68 MB/s | 1.23x | 271.89 MB/s | 233.69 MB/s | 1.16x |
| repeated-100k | 9 | 10.36 MB/s | 8.54 MB/s | 1.21x | 262.84 MB/s | 224.72 MB/s | 1.17x |
| zeros-100k | 1 | 256.74 MB/s | 187.66 MB/s | 1.37x | 1678.37 MB/s | 478.19 MB/s | 3.51x |
| zeros-100k | 5 | 274.87 MB/s | 189.93 MB/s | 1.45x | 1758.81 MB/s | 482.47 MB/s | 3.65x |
| zeros-100k | 9 | 268.09 MB/s | 192.59 MB/s | 1.39x | 1704.94 MB/s | 484.42 MB/s | 3.52x |

**Note on absolute numbers:** This benchmark run reports lower absolute throughput than the 8513bc3 validation (e.g., text-100k bs=9 decompress: 100.44 MB/s vs 143.94 MB/s previously). This is due to system load variability, not a code regression — the speedup ratios vs reference are consistent (1.13-1.15x text, 1.91-2.00x binary, 3.51-3.65x zeros). The bounds checks add negligible overhead since they are only hit on the code-building path (once per Huffman group), not the hot decode loop.

**Speedup summary:**
- Text decompression: 1.13-1.15x vs reference (consistent with prior commits)
- Binary decompression: 1.91-2.00x vs reference (consistent with prior commits)
- Repeated decompression: 1.11-1.17x vs reference
- Zeros decompression: 3.51-3.65x vs reference (CRC-dominated)
- Compression: 1.17-1.81x across workloads

## 7. Known Issues

| Issue | Severity | Introduced | Status |
|-------|----------|------------|--------|
| Huffman decode table OOB (primary + overflow) | CRITICAL | 8513bc3 | **FIXED** by 981dd00 |

The two heap buffer overflow vulnerabilities reported in bug-huffman-decode-oob-8513bc3.md are now resolved. Both crash reproducers return BZ_DATA_ERROR under ASAN with no memory safety violations. No other known pre-existing divergences, bugs, or test failures.

## 8. Summary

Commit 981dd00 is **clean** and resolves a critical security bug. The fix adds bounds checks to three locations in the Huffman decode table builder in decompress.c: (1) primary table base_idx + fill_count vs tbl_size, (2) overflow prefix index vs tbl_size, (3) overflow sub-table sub_offset + base_idx2 + fill2 vs 512, plus a pad2 negativity check. All 858 unit tests pass in both Release and ASAN+UBSAN modes. All 497 differential comparisons show zero divergences. Both crash reproducers that previously triggered SEGV/heap-buffer-overflow now cleanly return BZ_DATA_ERROR, matching reference libbz2 behavior. Quick fuzz (4 harnesses, ASAN-enabled, bzip2-tests seeds) found no new crashes or divergences. Performance is unchanged — the bounds checks are on the cold table-building path, not the hot decode loop.
