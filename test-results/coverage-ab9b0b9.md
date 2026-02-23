# Coverage Analysis Report — ab9b0b9

**Date**: 2026-02-23
**Commit**: ab9b0b9 (perf: optimize blocksort mainGtU)
**Tool**: gcc --coverage + lcov
**Inputs**: 481 seed corpus files + all parameter combinations

## Summary

| File | Lines | Line % | Branches | Branch % |
|------|-------|--------|----------|----------|
| blocksort.c | 401/409 | **98.0%** | 314/320 | **98.1%** |
| huffman.c | 58/62 | **93.6%** | 64/66 | **97.0%** |
| compress.c | 291/303 | **96.0%** | 200/202 | **99.0%** |
| decompress.c | 332/347 | **95.7%** | 536/548 | **97.8%** |
| bzlib.c | 407/781 | **52.1%** | 339/867 | **39.1%** |
| crctable.c | — | data only | — | — |
| randtable.c | — | data only | — | — |
| **Total** | **1583/1996** | **79.3%** | **1191/2059** | **57.8%** |

## Core Library Coverage (excluding API layer)

The core compression/decompression modules have excellent coverage:
- blocksort.c: 98.0% lines, 98.1% branches
- compress.c: 96.0% lines, 99.0% branches
- decompress.c: 95.7% lines, 97.8% branches
- huffman.c: 93.6% lines, 97.0% branches

## Uncovered Paths in bzlib.c

### Zero-coverage functions (18 functions)
All FILE* I/O API functions have zero coverage from fuzz harnesses:
- BZ2_bzReadOpen, BZ2_bzRead, BZ2_bzReadClose, BZ2_bzReadGetUnused
- BZ2_bzWriteOpen, BZ2_bzWrite, BZ2_bzWriteClose, BZ2_bzWriteClose64
- BZ2_bzopen, BZ2_bzdopen, BZ2_bzread, BZ2_bzwrite
- BZ2_bzflush, BZ2_bzclose, BZ2_bzerror
- BZ2_bz__AssertH__fail (assertion handler — expected uncovered)
- myfeof (helper)
- bzopen_or_bzdopen (helper)

### Low-coverage code paths
- BZ_FLUSH action in BZ2_bzCompress (0% — only BZ_RUN and BZ_FINISH exercised)
- Memory allocation failure paths (partial coverage)
- BZ_M_FLUSHING state machine transitions

## Recommendations

1. **fuzz_fileio harness** already exists but was not included in this coverage
   measurement. Need to run it through the coverage driver to capture FILE* API
   coverage.
2. **BZ_FLUSH path**: Create targeted fuzz inputs that exercise the FLUSH action
   in the streaming API. The streaming harness should interleave BZ_FLUSH operations.
3. **OOM injection**: Test allocation failure paths using custom bzalloc/bzfree hooks.
4. **Small decompress mode**: Ensure decompression with small=1 is being exercised
   (it uses different code paths).
