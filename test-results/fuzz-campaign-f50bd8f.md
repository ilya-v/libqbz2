# Fuzz Campaign Report — f50bd8f

**Commit**: f50bd8f — fix: revert compression-side batch CRC that broke multi-block streams
**Date**: 2026-02-23
**Focus**: Multi-block differential verification, crash-finding, coverage analysis

## Differential Fuzzing (Priority 1)

### Buffer-to-Buffer Differential (fuzz_differential)
- **Duration**: 151 seconds
- **Executions**: 2,646
- **Exec/sec**: 17
- **Coverage**: 3,836 blocks
- **Corpus size**: 477 entries (57 MB)
- **Max input size**: 500,000 bytes (multi-block)
- **Seeds**: compress_seeds + decompress_seeds + malformed_seeds + multiblock_seeds + bzip2.dict
- **Divergences**: 0
- **Crashes**: 0

### Streaming Differential (fuzz_diff_streaming)
- **Duration**: 152 seconds
- **Executions**: 2,279
- **Exec/sec**: 14
- **Coverage**: 3,923 blocks
- **Corpus size**: 469 entries (55 MB)
- **Max input size**: 500,000 bytes (multi-block)
- **Seeds**: compress_seeds + decompress_seeds + malformed_seeds + multiblock_seeds + bzip2.dict
- **Divergences**: 0
- **Crashes**: 0

**Total differential comparisons**: 4,925 (buffer-to-buffer + streaming)
**Total divergences**: 0

## Crash-Finding Fuzzing

| Harness | Duration | Executions | Exec/sec | Coverage | Crashes |
|---------|----------|------------|----------|----------|---------|
| fuzz_compress | 100s | 1,268 | 12 | 6,417 | 0 |
| fuzz_decompress | 100s | 2,478 | 24 | 3,368 | 0 |
| fuzz_streaming | 100s | 485 | 4 | 6,022 | 0 |
| fuzz_bufftobuff | 100s | 1,055 | 10 | 6,856 | 0 |
| fuzz_fileio | 100s | 1,336 | 13 | 6,695 | 0 |
| **Total** | **500s** | **6,622** | **~13** | — | **0** |

All harnesses run with max_len=500,000, bzip2.dict, ASAN+UBSAN, seeded with all 508 corpus files (including 7 multi-block seeds 99KB-500KB).

## Coverage Analysis

Built with gcc --coverage, exercised with coverage_driver against all 508 corpus files.

### Per-File Coverage

| File | Lines | Line % | Functions | Func % |
|------|-------|--------|-----------|--------|
| blocksort.c | 409 | 98.0% | 9 | 100% |
| compress.c | 303 | 96.0% | 9 | 100% |
| decompress.c | 347 | 95.7% | 2 | 100% |
| huffman.c | 62 | 93.5% | 3 | 100% |
| crctable.c | 23 | 100% | 1 | 100% |
| bzlib.c | 783 | 80.2% | 41 | 95.1% |
| coverage_driver.c | 148 | 97.3% | 9 | 100% |

### Summary

| Metric | Previous (ab9b0b9) | Current (f50bd8f) | Change |
|--------|--------------------|--------------------|--------|
| Line coverage | 79.3% | 90.5% | +11.2pp |
| Branch coverage | 57.8% | 66.9% | +9.1pp |
| Function coverage | — | 97.3% (72/74) | — |

### Remaining Coverage Gaps

- **bzlib.c at 80.2%**: The uncovered 20% is primarily error handling paths (malloc failures, invalid states) and the `BZ2_bz__AssertH__fail` assertion handler. These are difficult to reach through normal exercising but will be covered by OOM injection testing (planned).
- **2 uncovered functions**: Likely internal helpers that are only called in error paths.

## Multi-Block Testing Verification

The CRC multi-block bug (discovered in dffe019, fixed in f50bd8f) was specifically targeted:
- Added 7 multi-block seeds (99KB-500KB) to seed corpus
- Increased max_len to 500,000 bytes for all differential and crash-finding runs
- Verified that the differential harness catches the original bug (byte 10 divergence on 200KB input)
- Confirmed 0 divergences on the fixed code across 4,925 differential comparisons

## Corpus Summary

| Category | Files |
|----------|-------|
| Compression seeds | 36 |
| Decompression seeds | 297 |
| Malformed seeds | 148 |
| Multi-block seeds | 7 |
| **Total** | **508** |

## Per-Commit Fuzz Script Updates

- Added `fuzz_diff_streaming` to `quick-fuzz-list.txt` (now 4 harnesses per commit)
- Updated `run-quick-fuzz.sh` to seed differential harnesses with multiblock_seeds
- Updated `generate-corpus.sh` to generate multi-block seeds automatically
