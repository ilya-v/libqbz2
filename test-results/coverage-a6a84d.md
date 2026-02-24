# Coverage Report: a6a84d3

**Commit**: a6a84d3 (test: validation report for d3d790a)
**Date**: 2026-02-24
**Tool**: gcc --coverage + lcov (excluding vendored libsais.c and crc32_pclmul.c)
**Input**: 29 tester test suites + coverage_driver (3 seed files) + coverage_byte_driver (870 bz2 files)

## Summary

| Metric | Value | Previous (04b3ae6) | Delta |
|--------|-------|--------------------|-------|
| Line coverage | **96.0%** (1679/1749) | 91.1% (1588/1744) | **+4.9pp** |
| Function coverage | **98.4%** (60/61) | 96.7% (59/61) | +1.7pp |
| Branch coverage | **78.5%** (1666/2121) | 66.4% (1408/2121) | **+12.1pp** |

The +12.1pp branch improvement comes from combining the tester's 29 new test suites (targeting NULL params, state machine transitions, FILE* I/O, streaming edge cases, error paths) with the strategic-tester's byte-at-a-time and coverage drivers.

## Per-Module Breakdown

| Module | Lines | Functions | Branches | Previous Branches | Delta |
|--------|-------|-----------|----------|-------------------|-------|
| blocksort.c | 98.1% (157/160) | 100% (5/5) | **92.9%** (130/140) | 81.4% | **+11.5pp** |
| bzlib.c | 93.5% (736/787) | 97.6% (40/41) | **71.7%** (626/873) | 52.1% | **+19.6pp** |
| compress.c | **100%** (305/305) | 100% (9/9) | **95.6%** (195/204) | 88.7% | **+6.9pp** |
| crctable.c | 100% (23/23) | 100% (1/1) | 100% (8/8) | 100% | 0 |
| decompress.c | 97.1% (395/407) | 100% (2/2) | **78.0%** (647/830) | 71.1% | **+6.9pp** |
| huffman.c | 93.5% (58/62) | 100% (3/3) | 90.9% (60/66) | 90.9% | 0 |

## Remaining Coverage Gaps (455 untaken branches)

### bzlib.c — 247 untaken branches (71.7% branch coverage)

| Region | Untaken | Description |
|--------|---------|-------------|
| BZ2_bzRead/Open/Close | 84 | ferror() checks, NULL params, file state transitions |
| bzopen/bzdopen | 47 | Mode string parsing, stdin/stdout paths, error branches |
| unRLE randomised fast | 30 | Randomised block decode (fast path) — needs valid randomised bz2 |
| unRLE randomised small | 33 | Randomised block decode (small path) — needs valid randomised bz2 |
| Write API | 10 | Parameter validation, flush paths |
| Compress state machine | 8 | State transitions not reached |
| Other | 35 | Init, decompress control, buffToBuffAPI |

### decompress.c — 183 untaken branches (78.0% branch coverage)

| Region | Untaken | Description |
|--------|---------|-------------|
| Header decode save/restore | 65 | GET_UCHAR/GET_BITS input exhaustion paths in small-mode decode |
| Block output | 30 | Output loop branches in small-mode decompression |
| MTF/Huffman decode | 29 | Table edge cases in Huffman tree construction |
| Small-mode paths | 18 | Additional small-mode specific branches |
| Other | 41 | Scattered GET_BITS resume points |

### Other modules — 25 untaken branches

- blocksort.c: 10 (small-block fallback, SA-IS edge cases)
- compress.c: 9 (rare RLE edge cases, encoding overflow)
- huffman.c: 6 (code length overflow handling)

## Path to 85% Branch Coverage

Currently at 78.5%. Need 1803/2121 branches for 85% — 137 more branches.

**Highest-value targets:**
1. **Randomised block decode** (63 branches, bzlib.c lines 550-800) — Requires a valid bz2 stream with the randomisation flag set and correct CRC. The reference bzip2 never produces these, but valid ones exist in the wild. A synthetic stream generator could produce one.
2. **Small-mode byte-at-a-time** (65+ branches, decompress.c lines 220-300) — The existing byte-at-a-time driver does exercise small mode, but many GET_BITS resume points need input exhaustion at positions only reachable when multiple blocks are decoded in small mode. More diverse multi-block seeds could help.
3. **Read API error paths** (84 branches, bzlib.c lines 950-1150) — Needs mock FILE* error injection (ferror returning nonzero, fread returning short). Could use pipes that close mid-read.

**Diminishing returns:**
- Many of the bzopen/bzdopen branches are mode-string parsing edge cases that require unusual combinations (e.g., mode "rb0" vs "rb1" vs "r", path NULL vs empty vs "-")
- Some decompress.c branches are in code paths that require specific internal state combinations that are extremely hard to trigger externally

## Test Suites Used

29 tester test binaries (all PASS):
- test_api, test_advanced, test_block_boundary_bitreader, test_blocksort_branches, test_blocksort_paths
- test_bufftobuff_edge, test_bzlib_branches, test_compress_branches, test_compress_states
- test_concat_readahead, test_coverage_gaps, test_coverage_gaps2, test_crc32_internal
- test_decompress_branches, test_decompress_crc, test_decompress_errors
- test_differential, test_diff_multiblock, test_edge_cases, test_error_paths
- test_fileio, test_malformed, test_multiblock, test_oom, test_param_combos
- test_rle_huffman_edge, test_roundtrip, test_streaming, test_streaming_edge, test_streaming_states

2 coverage drivers:
- coverage_driver: 3 seed files (pyflate_hello-world.bz2, small_4char_5k.bin, go_compress_pass-random1.bz2)
- coverage_byte_driver: 870 small bz2 files from all corpus directories
