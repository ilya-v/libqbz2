# Coverage Report: 76996fe

**Commit**: 76996fe — perf: replace mainSort with libsais for all blocks >= 10000
**Date**: 2026-02-24
**Tool**: gcc --coverage + lcov (excluding vendored libsais.c)
**Input**: 42 curated seed files + randomised block synthesis + BZ_FLUSH/OUTBUFF_FULL/bzdopen exercises

## Summary

| Metric | Value | Previous (981dd0) | Delta |
|--------|-------|--------------------|-------|
| Line coverage | 93.1% (1866/2005) | 91.1% (2185/2398) | +2.0pp |
| Function coverage | 98.7% (74/75) | 97.6% (82/84) | +1.1pp |
| Branch coverage | 59.2% (1359/2297) | 63.0% (1679/2663) | -3.8pp |

Note: Line and function denominators decreased because blocksort.c went from 647 lines (custom SA-IS) to 160 lines (libsais wrapper). Branch coverage denominator also shrank but the ratio decreased due to new branches from libsais dispatch logic. Overall line/function coverage improved.

## Per-Module Breakdown

| Module | Lines | Functions | Branches |
|--------|-------|-----------|----------|
| blocksort.c | 95.6% (153/160) | 100% (5/5) | 90.0% (126/140) |
| bzlib.c | 88.1% (693/787) | 97.6% (40/41) | 53.6% (468/873) |
| compress.c | 96.1% (293/305) | 100% (9/9) | 90.2% (184/204) |
| crctable.c | 100% (23/23) | 100% (1/1) | 100% (8/8) |
| decompress.c | 96.3% (392/407) | 100% (2/2) | 48.3% (401/830) |
| huffman.c | 93.5% (58/62) | 100% (3/3) | 90.9% (60/66) |
| coverage_driver.c | 97.3% (249/256) | 100% (14/14) | 63.6% (112/176) |

## Coverage Improvements in This Run

New tests added to coverage driver targeting previously uncovered paths:

1. **BZ_FLUSH path** (bzlib.c:436-458) — Now exercised. Feeds half the data with BZ_RUN, then BZ_FLUSH, then feeds the rest and BZ_FINISH. Covers the BZ_M_FLUSHING state machine transitions, copy_input_until_stop flushing mode branch, and the flush completion check.

2. **BZ_OUTBUFF_FULL** (bzlib.c:1312,1319-1321,1368-1374) — Now exercised. Compress with 1-byte output buffer triggers output_overflow path. Decompress bz2 with 1-byte output triggers output_overflow_or_eof path.

3. **BZ2_bzdopen** (bzlib.c:1491-1496) — Now exercised. Write and read via fd-based open using open()/BZ2_bzdopen()/BZ2_bzclose(). Covers the fdopen branch in bzopen_or_bzdopen.

4. **Small-buffer streaming decompress** — Exercises the avail_out==0 return-and-resume paths in unRLE_obuf_to_output_FAST and unRLE_obuf_to_output_SMALL, which were previously only hit with exact buffer sizes.

5. **Randomised block synthesis** — Compresses a test string, flips the randomisation bit in the block header, then decompresses with both small=0 and small=1. This exercises the blockRandomised code paths but with corrupted CRC, so the decompressor enters the randomised decode loop but ultimately rejects the block.

## Remaining Coverage Gaps

### decompress.c (48.3% branch, 96.3% line)

The low branch coverage is dominated by the two-level Huffman decode table's many conditional branches. Each GET_BITS macro expands to multiple branches for input exhaustion checks, and the table builder has many bounds-checking branches that are only taken on malformed input. The randomised block code paths (~100 branches) are partially covered via synthesis but many sub-branches within the randomised RLE decode loops remain uncovered because the CRC check terminates execution before all paths are explored.

### bzlib.c (53.6% branch, 88.1% line)

Remaining uncovered paths:
- **BZ2_bz__AssertH__fail** (5 lines) — Internal assertion handler; only triggered by logic bugs.
- **Verbose/VPrintf paths** (~10 lines) — Diagnostic output when verbosity > 0.
- **Some FILE* error branches** — IO error mid-write/read simulation would require mock FILE*.
- **Empty-path bzopen** — The stdin/stdout path in bzopen_or_bzdopen (path=="" || path==NULL with open_mode=0).

### blocksort.c (90.0% branch, 95.6% line)

The libsais replacement simplified this module significantly. Remaining uncovered lines are fallback paths in the mainSort wrapper for small blocks.

## Recommendations

1. **Randomised block with valid CRC** — To fully cover the randomised decode paths, need to either: (a) produce a bz2 stream with a correct randomised-block CRC, or (b) find a legacy randomised bz2 file. The reference bzip2 tool no longer produces randomised blocks (the feature was removed in bzip2 1.0.x).

2. **Verbose mode** — Add a verbosity>0 test to cover VPrintf paths. Low priority.

3. **IO error simulation** — Would require pipe-based testing to inject read/write errors. Moderate effort for marginal coverage gain.
