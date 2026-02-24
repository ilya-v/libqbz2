# Coverage Report — 617fb39

**Commit**: 617fb39 — test: add FILE* error paths, byte-at-a-time, and RLE run-length to coverage driver
**Date**: 2026-02-24
**Tool**: GCC 15.2.1 --coverage + lcov 2.4.1

## Summary

| Metric | Rate | Covered | Total |
|--------|------|---------|-------|
| Lines | 97.2% | 1700 | 1749 |
| Functions | 98.4% | 60 | 61 |
| Branches | 79.9% | 1694 | 2121 |

## Per-Module Breakdown

| Module | Line% | Lines | Branch% | Branches | Uncovered Branches |
|--------|-------|-------|---------|----------|-------------------|
| blocksort.c | 98.1% | 160 | 92.8% | 140 | 10 |
| bzlib.c | 93.3% | 787 | 81.0% | 873 | 165 |
| compress.c | 100% | 305 | 95.5% | 204 | 9 |
| crc32_pclmul.c | 100% | 63 | 100% | 0* | 0 |
| crctable.c | 100% | 23 | 100% | 8 | 0 |
| decompress.c | 96.8% | 407 | 77.9% | 830 | 183 |
| huffman.c | 93.5% | 62 | 93.9% | 66 | 4 |

*crc32_pclmul.c uses intrinsics that GCC coverage doesn't instrument as branches.

## Test Suite Run

29 test executables + coverage driver with ~60 seed files:
- 29/29 tests PASS
- Coverage driver exercises all APIs: compress, decompress, streaming, FILE*, B2B, parameter errors, byte-at-a-time, RLE run-length, randomised blocks

## Uncovered Branch Analysis

### Structurally Unreachable (~80-100 branches)
- **total_in_lo32/total_out_lo32 overflow checks**: Each GET_BITS macro expansion produces 2 overflow branches requiring >4GB of input to trigger. ~48 GET_BITS calls in decompress.c = ~96 overflow branches. Similar pattern in bzlib.c output loops.

### Randomised Block Deep RLE (bzlib.c lines 586-596, 804-811, ~40 branches)
- The randomised block decompression path has 4 RLE run-length levels (1, 2, 3, 4+). Our randomised block test reaches level 2 but CRC mismatch terminates before level 3+.
- Covering these requires synthesizing a randomised block with correct CRC — feasible but complex.

### Huffman Decode Table Overflow (decompress.c lines 559-577, ~15 branches)
- The two-level Huffman overflow table path. Requires inputs with Huffman codes longer than 11 bits.

### FILE* IO Error Paths (bzlib.c, ~30 branches)
- ferror() checks on file handles during read/write operations. Requires injecting file errors mid-stream.
- Most are defensive checks that are correct to have but hard to trigger in tests.

## Improvement Since Last Report

| Metric | Previous (76996f) | Current (617fb3) | Delta |
|--------|-------------------|-------------------|-------|
| Lines | 93.1% | 97.2% | +4.1pp |
| Functions | 98.7% | 98.4% | -0.3pp |
| Branches | ~78.5% | 79.9% | +1.4pp |

## New Coverage Driver Exercises (this commit)

1. **exercise_fileio_error_paths()**: 20+ FILE* API error scenarios
2. **exercise_rle_run_length()**: Long RLE run decompression in FAST+SMALL modes with 1-byte output drain
3. **exercise_byte_at_a_time_decompress()**: Input fed 1 byte at a time
4. **exercise_byte_at_a_time_compress()**: Input and output 1 byte at a time
