# Coverage Analysis Report — c50993d

**Commit**: c50993d — perf: add branch prediction hints to hot decompression paths
**Also covers**: All optimizations through c50993d (SA-IS, 64-bit bitstream, prefetch, hybrid CRC, bulk memcpy, 64-bit decomp, batch CRC, branch hints)
**Date**: 2026-02-23
**Tool**: gcc --coverage + lcov
**Driver**: fuzz/coverage_driver.c
**Corpus**: 508 seed files

## Summary

| Metric | Value | Previous (f50bd8f) | Change |
|--------|-------|---------------------|--------|
| Line coverage | 91.2% (2,135/2,340) | 90.5% (1,877/2,075) | +0.7pp |
| Branch coverage | 65.6% (1,724/2,629) | 66.9% (1,406/2,103) | -1.3pp |
| Function coverage | 97.6% (82/84) | 97.3% (72/74) | +0.3pp |

Note: Total code increased from 2,075 to 2,340 lines (+265 lines) and from 74 to 84 functions (+10 functions) due to SA-IS blocksort addition and other optimizations. Despite the code growth, line coverage percentage improved.

## Per-File Breakdown

| File | Lines | Line % | Functions | Func % | Change |
|------|-------|--------|-----------|--------|--------|
| crctable.c | 23 | 100% | 1 | 100% | = |
| blocksort.c | 647 | 97.8% | 19 | 100% | +238 lines (SA-IS) |
| compress.c | 303 | 96.0% | 9 | 100% | = |
| decompress.c | 356 | 95.5% | 2 | 100% | +9 lines |
| huffman.c | 62 | 93.5% | 3 | 100% | = |
| bzlib.c | 787 | 80.3% | 41 | 95.1% | +4 lines |
| coverage_driver.c | 162 | 97.5% | 9 | 100% | +14 lines |

## Sustained Fuzzing Summary (all campaigns this session)

| Metric | Total |
|--------|-------|
| Differential comparisons | 40,000+ |
| Crash-finding executions | 30,000+ |
| Optimization commits verified | 8 |
| Divergences found | 0 |
| Crashes found | 0 |
