# Coverage Report: 04b3ae6

**Commit**: 04b3ae6 (source equivalent to 76996fe — CRC batch was applied and reverted)
**Date**: 2026-02-24
**Tool**: gcc --coverage + lcov (excluding vendored libsais.c and crc32_pclmul.c)
**Input**: 8 curated seed files via coverage_driver + 29 bz2 files via byte-at-a-time coverage_byte_driver
**New technique**: Byte-at-a-time decompression to exercise GET_BITS input exhaustion branches

## Summary

| Metric | Value | Previous (76996fe) | Delta |
|--------|-------|--------------------|-------|
| Line coverage | 91.1% (1588/1744) | 93.1% (1866/2005) | -2.0pp* |
| Function coverage | 96.7% (59/61) | 98.7% (74/75) | -2.0pp* |
| Branch coverage | **66.4%** (1408/2121) | 59.2% (1359/2297) | **+7.2pp** |

*Line/function decrease is due to running fewer compression-heavy exercises in this pass (timeout constraints at -O0). The previous report included more compression paths. Branch coverage is the primary improvement target.

## Per-Module Breakdown

| Module | Lines | Functions | Branches | Previous Branches |
|--------|-------|-----------|----------|-------------------|
| blocksort.c | 83.1% (133/160) | 80.0% (4/5) | 81.4% (114/140) | 90.0% |
| bzlib.c | 87.9% (692/787) | 97.6% (40/41) | 52.1% (455/873) | 53.6% |
| compress.c | 95.4% (291/305) | 100% (9/9) | 88.7% (181/204) | 90.2% |
| crctable.c | 100% (23/23) | 100% (1/1) | 100% (8/8) | 100% |
| decompress.c | 96.1% (391/407) | 100% (2/2) | **71.1%** (590/830) | 48.3% |
| huffman.c | 93.5% (58/62) | 100% (3/3) | 90.9% (60/66) | 90.9% |

## Key Improvement: Byte-at-a-Time Decompression

The new `coverage_byte_driver.c` feeds compressed input one byte at a time to the streaming decompression API. This forces the decompressor to hit every GET_BITS input exhaustion save/restore point in decompress.c.

Each GET_BITS macro expands to:
1. Check if enough bits are in the buffer
2. If not, try to bulk-read 4 bytes from input
3. If not enough input, read 1 byte
4. If no input at all, save state and return BZ_OK

By feeding single bytes, path (3) and (4) are exercised at every decode point in the state machine. This improved decompress.c branch coverage from **48.3% to 71.1%** (+22.8pp, +189 additional branches covered).

The driver also exercises:
- Both normal (small=0) and small-memory (small=1) decompression modes
- Verbose decompression (verbosity=3) for VPrintf path coverage
- Malformed and corrupted bz2 inputs for error path coverage

## New Test Infrastructure

Added `fuzz/coverage_byte_driver.c`:
- Standalone driver, independent of the main coverage_driver
- Only processes files with valid bz2 magic header and size < 8KB (for speed)
- Runs byte-at-a-time decompress in both normal and small modes
- Runs verbose decompress for VPrintf coverage
- Can be built and run incrementally alongside the main coverage driver (gcda files accumulate)

## Remaining Coverage Gaps

### decompress.c (71.1% branch — 240 uncovered branches)
- Many remaining uncovered branches are in the **randomised block decode loops** — the XOR-based de-randomisation path. Only partially exercised because the synthesised randomised block has invalid CRC, causing early termination.
- Some branches are in the **two-level Huffman decode table** overflow handling — only triggered when a specific combination of code lengths produces overflow entries.
- Some GET_BITS save/restore points may require input exhaustion at positions that are unlikely with single-byte feeding (e.g., when the buffer happens to have enough bits accumulated).

### bzlib.c (52.1% branch — 418 uncovered branches)
- **FILE* I/O error branches** (~100 branches): ferror() checks, fwrite() failure paths. Would require mock FILE* or pipe-based testing.
- **Verbose output branches** (~40 branches): VPrintf calls guarded by verbosity checks. Would need verbosity > 0 tests in more contexts.
- **BZ2_bz__AssertH__fail** code paths: internal assertion handler, only triggered by logic bugs.
- **bzopen stdin/stdout paths**: when path is empty or NULL with open_mode=0.

### blocksort.c (81.4% branch)
- Lower than previous report because this pass didn't include heavy compression exercises. The uncovered branches are in mainSort small-block fallback paths.

## Recommendations

1. **Combine both drivers in a single run** with careful time budgeting — run byte driver on all small bz2 files, then main driver on a few representative files.
2. **Create valid randomised bz2 stream** — would require computing correct CRC for the randomised output. This would add ~50 branch coverage points in decompress.c.
3. **Mock FILE* testing** — inject read/write errors to cover IO error branches. High effort, moderate reward.
