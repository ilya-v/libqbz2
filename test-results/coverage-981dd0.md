# Coverage Report: 981dd0

**Commit**: 981dd00 — fix: add bounds checks to Huffman decode table builder
**Date**: 2026-02-24
**Tool**: gcc --coverage + lcov
**Input**: 39 curated seed files covering compression, decompression, malformed, bzip2-tests, crash reproducers

## Summary

| Metric | Value | Previous (7a7341d) |
|--------|-------|--------------------|
| Line coverage | 91.1% (2185/2398) | 90.9% |
| Function coverage | 97.6% (82/84) | 97.3% |
| Branch coverage | 63.0% (1679/2663) | 65.9% |

Note: Branch coverage is slightly lower than the previous report because the curated subset is smaller than a full corpus run. Line and function coverage improved marginally due to the new Huffman decode bounds check code being exercised.

## Per-Module Breakdown

| Module | Lines | Branches (executed) | Branches (taken) |
|--------|-------|---------------------|-----------------|
| blocksort.c | 97.5% (631/647) | 98.3% | 87.8% |
| bzlib.c | 80.2% (631/787) | 70.0% | 48.2% |
| compress.c | 96.1% (293/305) | 99.0% | 90.2% |
| crctable.c | 100% (23/23) | 100% | 100% |
| decompress.c | 94.8% (386/407) | 78.8% | 52.5% |
| huffman.c | 93.6% (58/62) | 97.0% | 90.9% |
| randtable.c | N/A (data only) | N/A | N/A |

## Uncovered Code Analysis

### bzlib.c (156 uncovered lines, 80.2% line coverage)

The largest coverage gaps are:

1. **BZ2_bz__AssertH__fail** (5 lines) — Internal assertion handler. Only triggered by logic bugs. Cannot be exercised without injecting impossible state. Low priority.

2. **Compression OOM path** (5 lines) — Memory allocation failure during compress init. Covered by OOM injection testing separately.

3. **BZ_FLUSH compression path** (12 lines) — The `BZ_M_FLUSHING` state in `BZ2_bzCompress`. Exercised when calling `BZ2_bzCompress` with `BZ_FLUSH` action. **Coverage gap: coverage driver only uses BZ_RUN and BZ_FINISH.**

4. **Randomized block decompression** (~50 lines) — The `s->blockRandomised` code path in both `unRLE_obuf_to_output_FAST` and `unRLE_obuf_to_output_SMALL`. This is a legacy bzip2 feature. **Coverage gap: no randomized bzip2 streams in the test corpus.** The randomization flag is set in the block header bit — need to craft or find a stream with this flag set.

5. **FILE* API error paths** (~40 lines) — IO error handling in BZ2_bzReadOpen, BZ2_bzRead, BZ2_bzWriteOpen, BZ2_bzWrite, BZ2_bzReadClose. **Coverage gap: no simulated IO failures.**

6. **BZ2_bzdopen** (5 lines) — fd-based file open. **Coverage gap: coverage driver doesn't test bzdopen.**

7. **BZ_OUTBUFF_FULL paths** (8 lines) — When output buffer is too small in BZ2_bzBuffToBuffCompress/Decompress. **Coverage gap: coverage driver always allocates large output buffers.**

8. **VPrintf / verbose paths** (~10 lines) — Diagnostic output when verbosity > 0. Low priority.

### decompress.c (21 uncovered lines, 94.8% line coverage)

Most uncovered code is in error handling branches (Huffman decode validation, CRC mismatch paths that are already covered by malformed input testing via fuzzing).

## Recommendations for Coverage Improvement

Priority order:

1. **BZ_FLUSH path** — Add a test that exercises `BZ2_bzCompress(strm, BZ_FLUSH)` mid-stream. Easy to add to coverage driver.
2. **BZ_OUTBUFF_FULL** — Add tests with deliberately undersized output buffers.
3. **Randomized blocks** — Craft a bzip2 stream with the randomization flag set. This requires modifying a valid stream's block header bit or finding a tool that produces randomized blocks.
4. **BZ2_bzdopen** — Add a test using `BZ2_bzdopen`.
5. **IO error simulation** — Lower priority; would need mock FILE* or pipe-based testing.
