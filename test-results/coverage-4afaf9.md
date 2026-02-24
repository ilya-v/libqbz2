# Coverage Report — 4afaf97

**Commit**: 4afaf97 — test: add bzopen/read-API coverage driver targeting FILE* and NULL bzerror paths
**Date**: 2026-02-24
**Tool**: GCC 15.2.1 --coverage + lcov 2.4.1

## Summary

| Metric | Rate | Covered | Total |
|--------|------|---------|-------|
| Lines | 97.6% | 1702 | 1744 |
| Functions | 98.4% | 60 | 61 |
| Branches | 81.7% | 1732 | 2121 |

## Per-Module Breakdown

| Module | Line% | Lines | Branch% | Branches | Uncovered |
|--------|-------|-------|---------|----------|-----------|
| blocksort.c | 98.1% | 160 | 92.9% | 140 | 10 |
| bzlib.c | 96.2% | 787 | 77.9% | 873 | 193 |
| compress.c | 100% | 305 | 95.6% | 204 | 9 |
| crc32_pclmul.c | 100% | 63 | 100% | 6 | 0 |
| crctable.c | 100% | 23 | 100% | 8 | 0 |
| decompress.c | 97.8% | 407 | 78.9% | 830 | 175 |
| huffman.c | 100% | 62 | 97.0% | 66 | 2 |

## Test Suite

32 test executables + 2 coverage drivers (coverage_driver, coverage_bzopen_driver):
- 32/32 tests PASS
- coverage_driver: exercises compress, decompress, streaming, FILE*, B2B, parameter errors, byte-at-a-time, RLE run-length
- coverage_bzopen_driver: exercises bzopen/bzdopen, FILE* write/read API, NULL bzerror, pipe error injection, convenience I/O

## Uncovered Branch Analysis (389 total uncovered)

### Structurally Unreachable (~111 branches)

**decompress.c total_in overflow (~108)**:
- Each GET_BITS macro expansion generates branches for `total_in_lo32` overflow checking.
- These require >4GB of input to trigger. With ~36 GET_BITS call sites, 3 overflow branches each = ~108.
- Branch IDs 5, 6, 10 in lcov BRDA — account for 108 of 175 uncovered in decompress.c.

**bzlib.c total_out overflow (~3)**:
- Output loop overflow checks at lines 308, 322.

### Randomised Block Deep RLE (~45 branches)

**FAST path (lines 550-600)**: 19 uncovered branches in the randomised block variant of `unRLE_obuf_to_output_FAST`. The RLE state machine has levels 1-4+; current tests reach level 2 but not 3+.

**SMALL path (lines 740-840)**: 26 uncovered branches in `unRLE_obuf_to_output_SMALL` randomised variant. Same RLE level issue.

Covering these requires synthesizing bzip2 files with randomised blocks containing correct CRCs and data patterns that produce long RLE runs.

### FILE* API BZ_SETERR paths (~113 branches, lines 900-1500)

The `BZ_SETERR(eee)` macro expands to 4 branches per call (null checks on bzerror and bzf). With 45 BZ_SETERR calls across all FILE* functions, many branch combinations remain uncovered. Categories:
- Functions called only with valid bzerror: miss the `bzerror==NULL` false-branch
- Functions where bzf is never NULL: miss the `bzf==NULL` false-branch
- Functions never called at all in certain error states

### decompress.c Reachable Gaps (~67 branches)

GET_BITS suspend/resume paths that haven't been triggered by byte-at-a-time feeding at specific decompression stages:
- GET_MTF_VAL at lines 476, 491, 580: 10 uncovered each (30 total)
- Stream header parsing: 18 uncovered at lines 224-233
- Block header parsing: ~19 uncovered at lines 687-705

## Improvement History

| Metric | 76996f | 617fb3 | 4afaf9 (current) |
|--------|--------|--------|-------------------|
| Lines | 93.1% | 97.2% | 97.6% |
| Functions | 98.7% | 98.4% | 98.4% |
| Branches | ~78.5% | 79.9% | 81.7% |

## Path to 85%

Target: 1803 branches (85% of 2121).
Currently: 1732 covered.
Gap: 71 more branches needed.
Achievable pool: ~278 reachable uncovered branches.

Best targets (in priority order):
1. **FILE* BZ_SETERR null paths** (~113 available, ~30-40 achievable with more comprehensive driver)
2. **decompress.c GET_BITS suspend paths** (~67 available, ~20-30 achievable with targeted byte-at-a-time exercises)
3. **Randomised block deep RLE** (~45 available, complex to trigger)
