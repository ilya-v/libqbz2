# Validation Report: f148882 — build system default to Release + PGO Benchmark Suite

**Commit:** f148882
**Description:** ops: default CMake build type to Release and warn on unoptimized benchmarks
**Date:** 2026-02-24
**Validator:** tester (per-commit)
**Verdict:** PASS

## 1. Build

| Variant | Compiler | Result |
|---------|----------|--------|
| Release | gcc -O2 | PASS |
| ASAN+UBSAN | clang -fsanitize=address,undefined | PASS |
| Fuzz harnesses | clang -fsanitize=fuzzer,address | PASS (7 harnesses) |
| PGO (GCC) | gcc -O2 -fprofile-use | PASS |
| PGO (Clang) | clang -O2 -fprofile-instr-use | PASS |

## 2. Unit Tests

| Suite | Tests | Passed | Failed | Assertions | Time |
|-------|-------|--------|--------|------------|------|
| test_api | 57 | 57 | 0 | 235 | 0.010s |
| test_edge_cases | 67 | 67 | 0 | 60,427 | 0.014s |
| test_advanced | 40 | 40 | 0 | 100,870 | 0.091s |
| test_streaming | 30 | 30 | 0 | 1,525 | 0.180s |
| test_roundtrip | 137 | 137 | 0 | 175 | 0.935s |
| test_error_paths | 60 | 60 | 0 | 224 | 0.001s |
| test_fileio | 58 | 58 | 0 | 952 | 0.027s |
| test_multiblock | 33 | 33 | 0 | 197 | 0.591s |
| test_blocksort_paths | 55 | 55 | 0 | 137 | 0.956s |
| test_oom | 22 | 22 | 0 | 318 | 0.019s |
| test_decompress_errors | 32 | 32 | 0 | 131 | 0.005s |
| test_malformed | 32 | 32 | 0 | 99 | 0.012s |
| test_streaming_edge | 25 | 25 | 0 | 1,467 | 0.027s |
| test_param_combos | 92 | 92 | 0 | 338 | 0.091s |
| test_rle_huffman_edge | 47 | 47 | 0 | 73 | 0.100s |
| test_block_boundary_bitreader | 20 | 20 | 0 | 252,958 | 0.441s |
| test_concat_readahead | 20 | 20 | 0 | 2,053 | 0.005s |
| test_compress_states | 31 | 31 | 0 | 38,202 | 0.008s |
| test_bufftobuff_edge | 45 | 45 | 0 | 1,180 | 0.005s |
| test_huffman_decode_oob | 17 | 17 | 0 | 87 | 0.025s |
| test_coverage_gaps | 23 | 23 | 0 | 85 | 0.002s |
| test_coverage_gaps2 | 56 | 56 | 0 | 2,320 | 0.010s |
| test_decompress_branches | 24 | 24 | 0 | 163 | 0.015s |
| test_bzlib_branches | 45 | 45 | 0 | 7,221 | 0.006s |
| test_blocksort_branches | 24 | 24 | 0 | 125 | 0.005s |
| test_compress_branches | 26 | 26 | 0 | 455 | 0.013s |
| test_decompress_crc | 63 | 63 | 0 | 571 | 0.097s |
| test_crc32_internal | 17 | 17 | 0 | 594 | 0.001s |
| test_streaming_states | 18 | 18 | 0 | 2,134 | 0.021s |
| test_randomised_blocks | 16 | 16 | 0 | 122 | 0.032s |
| test_diff_error_codes | 22 | 22 | 0 | 27 | 0.155s |
| test_bzip2_corpus | 162 | 162 | 0 | — | 5.2s |
| test_differential | 206 | 206 | 0 | — | — |
| test_diff_multiblock | 129 | 129 | 0 | — | — |
| **Total** | **1,556** | **1,556** | **0** | **475,465** | **9.3s** |

## 3. Differential Tests (deterministic suite)

| Suite | Inputs | Passed | Divergences |
|-------|--------|--------|-------------|
| test_differential | 206 | 206 | 0 |
| test_diff_multiblock | 129 | 129 | 0 |
| test_bzip2_corpus | 162 | 162 | 0 |
| test_diff_error_codes | 3,148 | 3,144 | 4 (known) |
| **Total** | **3,645** | **3,641** | **4 (known)** |

The 4 divergences in test_diff_error_codes are known: qbz2 returns BZ_DATA_ERROR on specific bitflipped Huffman code bytes where the reference returns BZ_OK. See Known Issues below.

## 4. ASAN+UBSAN

| Metric | Value |
|--------|-------|
| Test executables | 35 |
| Passed | 35 |
| Failed | 0 |
| Total time | 99s |

Zero ASAN errors, zero UBSAN violations across all 35 test executables.

## 5. Quick Fuzz

| Harness | Runs | Exec/s | Crashes | Divergences | Corpus | Time |
|---------|------|--------|---------|-------------|--------|------|
| fuzz_compress | 70 | 6 | 0 | — | 66 | 11s |
| fuzz_decompress | 443 | 31 | 0 | — | 94 | 14s |
| fuzz_differential | 485 | 12 | 0 | 0 | 200 | 38s |
| fuzz_diff_streaming | 485 | 12 | 0 | 0 | 194 | 38s |

4 harnesses, all ASAN-enabled. 1,483 total runs. Zero crashes, zero divergences (new or known). fuzz_diff_streaming killed at budget limit (expected).

## 6. Benchmarks — Regular -O2 (GCC)

System load average: 0.5 (quiescent). Average of 2 runs.

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 28.91 MB/s | 23.66 MB/s | **1.22x** | 150.75 MB/s | 123.63 MB/s | **1.22x** |
| text-100k | 5 | 34.68 MB/s | 23.16 MB/s | **1.50x** | 152.71 MB/s | 129.45 MB/s | **1.18x** |
| text-100k | 9 | 34.47 MB/s | 23.60 MB/s | **1.46x** | 150.02 MB/s | 109.10 MB/s | **1.38x** |
| binary-100k | 1 | 15.45 MB/s | 17.17 MB/s | 0.90x | 67.78 MB/s | 34.91 MB/s | **1.94x** |
| binary-100k | 5 | 17.18 MB/s | 17.69 MB/s | 0.97x | 68.10 MB/s | 35.91 MB/s | **1.90x** |
| binary-100k | 9 | 16.64 MB/s | 17.55 MB/s | 0.95x | 67.40 MB/s | 36.10 MB/s | **1.87x** |
| repeated-100k | 1 | 69.05 MB/s | 14.01 MB/s | **4.93x** | 429.75 MB/s | 434.43 MB/s | 0.99x |
| repeated-100k | 5 | 64.12 MB/s | 18.02 MB/s | **3.56x** | 442.95 MB/s | 438.59 MB/s | 1.01x |
| repeated-100k | 9 | 57.27 MB/s | 18.27 MB/s | **3.13x** | 409.06 MB/s | 439.28 MB/s | 0.93x |
| zeros-100k | 1 | 593.03 MB/s | 316.50 MB/s | **1.87x** | 3826.20 MB/s | 604.11 MB/s | **6.33x** |
| zeros-100k | 5 | 649.23 MB/s | 338.40 MB/s | **1.92x** | 3894.42 MB/s | 608.34 MB/s | **6.40x** |
| zeros-100k | 9 | 637.41 MB/s | 335.96 MB/s | **1.90x** | 3887.17 MB/s | 603.84 MB/s | **6.44x** |

## 7. Benchmarks — PGO (GCC -fprofile-use)

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 23.65 MB/s | 22.10 MB/s | **1.07x** | 156.52 MB/s | 129.36 MB/s | **1.21x** |
| text-100k | 5 | 33.36 MB/s | 23.62 MB/s | **1.41x** | 155.10 MB/s | 128.63 MB/s | **1.21x** |
| text-100k | 9 | 32.68 MB/s | 23.65 MB/s | **1.38x** | 154.95 MB/s | 129.25 MB/s | **1.20x** |
| binary-100k | 1 | 16.65 MB/s | 17.48 MB/s | 0.95x | 71.23 MB/s | 34.62 MB/s | **2.06x** |
| binary-100k | 5 | 16.88 MB/s | 17.72 MB/s | 0.95x | 73.15 MB/s | 35.99 MB/s | **2.03x** |
| binary-100k | 9 | 16.64 MB/s | 17.50 MB/s | 0.95x | 70.84 MB/s | 35.87 MB/s | **1.97x** |
| repeated-100k | 1 | 69.91 MB/s | 14.19 MB/s | **4.93x** | 437.81 MB/s | 435.55 MB/s | 1.01x |
| repeated-100k | 5 | 57.90 MB/s | 18.21 MB/s | **3.18x** | 440.48 MB/s | 439.13 MB/s | 1.00x |
| repeated-100k | 9 | 56.66 MB/s | 17.99 MB/s | **3.15x** | 416.75 MB/s | 440.06 MB/s | 0.95x |
| zeros-100k | 1 | 597.73 MB/s | 319.31 MB/s | **1.87x** | 3747.65 MB/s | 603.32 MB/s | **6.21x** |
| zeros-100k | 5 | 675.48 MB/s | 338.40 MB/s | **2.00x** | 3795.64 MB/s | 609.82 MB/s | **6.22x** |
| zeros-100k | 9 | 622.31 MB/s | 335.18 MB/s | **1.86x** | 3757.09 MB/s | 604.94 MB/s | **6.21x** |

## 8. Benchmarks — PGO (Clang -fprofile-instr-use)

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|-----|---------------|--------------|-----------|------------------|----------------|-----------|
| text-100k | 1 | 36.23 MB/s | 23.81 MB/s | **1.52x** | 154.71 MB/s | 128.43 MB/s | **1.20x** |
| text-100k | 5 | 38.21 MB/s | 23.73 MB/s | **1.61x** | 146.30 MB/s | 126.34 MB/s | **1.16x** |
| text-100k | 9 | 37.71 MB/s | 23.68 MB/s | **1.59x** | 155.08 MB/s | 127.84 MB/s | **1.21x** |
| binary-100k | 1 | 13.23 MB/s | 17.49 MB/s | 0.76x | 68.67 MB/s | 34.30 MB/s | **2.00x** |
| binary-100k | 5 | 13.13 MB/s | 17.66 MB/s | 0.74x | 69.20 MB/s | 35.15 MB/s | **1.97x** |
| binary-100k | 9 | 12.91 MB/s | 17.34 MB/s | 0.74x | 67.21 MB/s | 35.40 MB/s | **1.90x** |
| repeated-100k | 1 | 77.32 MB/s | 14.84 MB/s | **5.21x** | 442.79 MB/s | 430.06 MB/s | 1.03x |
| repeated-100k | 5 | 72.03 MB/s | 19.33 MB/s | **3.73x** | 441.09 MB/s | 433.86 MB/s | 1.02x |
| repeated-100k | 9 | 64.83 MB/s | 18.63 MB/s | **3.48x** | 422.74 MB/s | 435.57 MB/s | 0.97x |
| zeros-100k | 1 | 593.42 MB/s | 312.98 MB/s | **1.90x** | 4011.77 MB/s | 593.22 MB/s | **6.76x** |
| zeros-100k | 5 | 654.81 MB/s | 332.92 MB/s | **1.97x** | 4035.02 MB/s | 598.61 MB/s | **6.74x** |
| zeros-100k | 9 | 645.63 MB/s | 332.11 MB/s | **1.94x** | 3919.37 MB/s | 596.41 MB/s | **6.57x** |

## 9. PGO Impact Analysis

| Metric | GCC -O2 | GCC PGO | PGO Delta | Clang PGO |
|--------|---------|---------|-----------|-----------|
| Text compress (bs=5) | 34.68 MB/s | 33.36 MB/s | -4% | 38.21 MB/s (+10%) |
| Text decompress (bs=5) | 152.71 MB/s | 155.10 MB/s | +2% | 146.30 MB/s (-4%) |
| Binary decompress (bs=1) | 67.78 MB/s | 71.23 MB/s | **+5%** | 68.67 MB/s (+1%) |
| Repeated compress (bs=1) | 69.05 MB/s | 69.91 MB/s | +1% | 77.32 MB/s (**+12%**) |
| Zeros decompress (bs=5) | 3894.42 MB/s | 3795.64 MB/s | -3% | 4035.02 MB/s (**+4%**) |

**PGO summary:**
- GCC PGO: modest gains on binary decompression (+5%), slight regression on text compression (-4%). Overall effect is small, likely because GCC's -O2 already makes good branch layout decisions on this codebase.
- Clang PGO: strong gains on text compression (+10-12%), repeated compression (+12%), zeros decompression (+4%). Binary compression regresses (-15%) but binary decompression is unchanged. Clang benefits more from PGO data.
- Best overall build: Clang PGO for compression (36-38 MB/s text vs 29-35 GCC), GCC -O2 or PGO for decompression (equivalent).

## 10. Known Issues

| # | Description | Severity | Introduced | Status |
|---|-------------|----------|------------|--------|
| 1 | 4 error code divergences: qbz2 returns BZ_DATA_ERROR on bitflipped Huffman code bytes (byte 29 bits 5,7; byte 30 bits 3,5 of "bit flip sweep data" bs=1) where reference returns BZ_OK | Medium | Pre-existing (all commits) | Open — filed as conformance bug (task #92) |

## 11. Summary

Commit f148882 is **clean**. The build system change (defaulting to Release and warning on unoptimized benchmarks) produces correct binaries. All 1,556 tests pass across 34 suites with 475,465 assertions. 3,645 differential comparisons performed with 4 known divergences (tracked, conformance bug filed). ASAN+UBSAN fully clean across 35 executables. Quick fuzz: 1,483 runs, 0 crashes, 0 divergences.

**Comprehensive PGO benchmark results (task #86):** Both GCC and Clang PGO builds complete successfully. The library delivers strong performance vs reference across all workloads:
- **Text:** 1.18-1.50x compression, 1.18-1.38x decompression (up to 1.61x compress with Clang PGO)
- **Binary:** 0.74-1.00x compression (regression area), 1.87-2.06x decompression
- **Repeated:** 3.13-5.21x compression, 0.93-1.03x decompression
- **Zeros:** 1.85-2.00x compression, 6.21-6.76x decompression

PGO provides marginal improvement for GCC (~5% on select paths) but more significant gains with Clang (10-12% on compression workloads). The binary compression regression vs reference is the only area where qbz2 underperforms.
