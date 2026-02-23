# Coverage Report — 7a7341d

**Commit**: 7a7341d
**Date**: 2026-02-23
**Tool**: gcov + lcov with gcc -O0 --coverage

## Summary

| Metric | Rate | Covered | Total |
|--------|------|---------|-------|
| Lines | 90.9% | 2,022 | 2,224 |
| Functions | 97.3% | 73 | 75 |
| Branches | 65.9% | 1,677 | 2,543 |

## Per-File Breakdown

| File | Lines | Functions | Branches |
|------|-------|-----------|----------|
| blocksort.c | 97.8% (632/647) | 100% (19/19) | 88.5% (508/574) |
| bzlib.c | 80.3% (632/787) | 95.1% (39/41) | 48.2% (421/873) |
| compress.c | 96.1% (293/305) | 100% (9/9) | 90.2% (184/204) |
| crctable.c | 100% (23/23) | 100% (1/1) | 100% (8/8) |
| decompress.c | 95.8% (383/400) | 100% (2/2) | 60.6% (496/818) |
| huffman.c | 93.5% (58/62) | 100% (3/3) | 90.9% (60/66) |

## Changes Since Last Report (c50993d)

- Line coverage: 91.2% -> 90.9% (slight decrease due to code growth)
- Function coverage: 97.6% -> 97.3% (2 uncovered functions remain)
- Branch coverage: 65.6% -> 65.9% (slight improvement)
- Total executable lines grew from 2,340 to 2,224 (restructuring)

## Uncovered Areas

- **bzlib.c** (80.3%): Many FILE* API error handling paths and state machine edge cases not fully exercised
- **decompress.c** (60.6% branch): Two-level Huffman decode table has many conditional branches for different code length combinations
- **2 uncovered functions**: Likely in bzlib.c (error path helpers or rarely-used API variants)

## Seeds Used

- compress_seeds: 36 files (diverse uncompressed inputs)
- decompress_seeds: 297 files (valid bz2 streams)
- malformed_seeds: 148 files (invalid bz2 for error path coverage)
- multiblock_seeds: 7 files (99KB-500KB multi-block inputs)
- coverage_seeds: 10 files (targeted: large random 20KB, repetitive 20KB, ascending/descending patterns — for SA-IS and mainSort coverage)
