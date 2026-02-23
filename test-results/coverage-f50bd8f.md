# Coverage Analysis Report — f50bd8f

**Commit**: f50bd8f — fix: revert compression-side batch CRC that broke multi-block streams
**Date**: 2026-02-23
**Tool**: gcc --coverage + lcov
**Driver**: fuzz/coverage_driver.c (exercises compress, decompress, streaming, FILE* API, parameter errors)
**Corpus**: 508 seed files (36 compress + 297 decompress + 148 malformed + 7 multi-block)

## Summary

| Metric | Value | Previous (ab9b0b9) | Change |
|--------|-------|---------------------|--------|
| Line coverage | 90.5% (1,877/2,075) | 79.3% | +11.2pp |
| Branch coverage | 66.9% (1,406/2,103) | 57.8% | +9.1pp |
| Function coverage | 97.3% (72/74) | — | — |

## Per-File Breakdown

| File | Lines | Line % | Functions | Func % |
|------|-------|--------|-----------|--------|
| crctable.c | 23 | 100% | 1 | 100% |
| blocksort.c | 409 | 98.0% | 9 | 100% |
| compress.c | 303 | 96.0% | 9 | 100% |
| decompress.c | 347 | 95.7% | 2 | 100% |
| huffman.c | 62 | 93.5% | 3 | 100% |
| bzlib.c | 783 | 80.2% | 41 | 95.1% |

## What Changed

The major improvement is **bzlib.c: 52.1% -> 80.2%** (+28.1pp), driven by:
- Added `exercise_fileio_write_read()` to coverage_driver.c — exercises BZ2_bzWriteOpen, BZ2_bzWrite, BZ2_bzWriteClose64, BZ2_bzReadOpen, BZ2_bzRead, BZ2_bzReadGetUnused, BZ2_bzReadClose
- Added `exercise_bzopen()` to coverage_driver.c — exercises BZ2_bzopen, BZ2_bzwrite, BZ2_bzread, BZ2_bzclose, BZ2_bzerror, BZ2_bzflush
- Added error path testing — BZ2_bzopen(NULL, ...), BZ2_bzopen(..., NULL), invalid mode strings

## Remaining Gaps

### bzlib.c (80.2% — 155 lines uncovered)
- Error handling paths triggered only by malloc failures (OOM injection will cover these)
- `BZ2_bz__AssertH__fail` assertion handler (intentionally unreachable in normal operation)
- Some edge cases in FILE* read/write state machine transitions
- 2 uncovered functions (likely internal error-path helpers)

### Core Modules
- blocksort.c (98.0%): 2% uncovered is fallback sorting paths rarely triggered
- compress.c (96.0%): Uncovered paths are rare parameter combinations
- decompress.c (95.7%): Uncovered paths are error recovery branches
- huffman.c (93.5%): Uncovered branches in edge-case tree construction

## Next Steps
- OOM injection testing to cover malloc-failure paths in bzlib.c
- Targeted tests for uncovered branches in core modules
- Guard-page testing for buffer overrun detection
