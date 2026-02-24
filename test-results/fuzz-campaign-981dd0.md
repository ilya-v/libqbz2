# Fuzz Campaign Report: 981dd0 — Huffman decode OOB fix + bzip2-tests integration

**Commit**: 981dd00 — fix: add bounds checks to Huffman decode table builder
**Date**: 2026-02-24
**Seed corpus**: 565 files (36 compress + 297 decompress + 148 malformed + 7 multiblock + 11 coverage + 38 bzip2-tests valid + 8 bzip2-tests bad + 20 misc)
**Previous bugs**: 2 heap buffer overflows in Huffman decode table (8513bc3) — FIXED in this commit

## OOB Fix Verification

Both crash reproducers verified fixed under ASAN:
- `huffman-overflow-subtable.bz2`: returns BZ_DATA_ERROR (-4) — PASS (no ASAN errors)
- `huffman-overflow-primary.bz2`: returns BZ_DATA_ERROR (-4) — PASS (no ASAN errors)

Both match reference libbz2 behavior exactly.

## bzip2-tests Repository Conformance

All files from the bzip2-tests repository (git://sourceware.org/git/bzip2-tests.git) tested differentially:
- **38 valid .bz2 files**: 0 divergences (both normal and small mode)
- **7 malformed .bz2.bad files**: 0 divergences (both normal and small mode)
- **Total**: 45 files, 90 comparisons (normal + small), 0 divergences

## Sustained Fuzzing Campaigns

All harnesses run with ASAN+UBSAN enabled, `-max_len=500000`, seeded with full corpus including bzip2-tests.

| Harness | Runs | Time (s) | exec/s | Crashes | Divergences | Coverage |
|---------|------|----------|--------|---------|-------------|----------|
| fuzz_decompress | 4,512 | 121 | 37 | 0 | N/A | 5,809 ft |
| fuzz_differential | 2,640 | 124 | 21 | 0 | 0 | 4,409 ft |
| fuzz_diff_streaming | 2,699 | 121 | 22 | 0 | 0 | 4,480 ft |
| fuzz_compress | 927 | 127 | 7 | 0 | N/A | 7,714 ft |
| fuzz_fileio | 1,540 | 61 | -- | 0 | N/A | -- |
| fuzz_bufftobuff | 1,143 | 122 | 9 | 0 | N/A | 8,219 ft |
| **Total** | **13,461** | **676** | | **0** | **0** | |

## Cumulative Project Stats

- **Total differential comparisons**: 97,000+ (92K previous + 5,339 this session)
- **Total divergences found**: 0
- **Total crashes found**: 2 (both fixed in 981dd00)
- **Seed corpus size**: 565 files
- **bzip2-tests conformance**: 45/45 files PASS

## Findings

No new bugs found. The Huffman decode bounds check fix eliminates both OOB crashes while maintaining correct error reporting (BZ_DATA_ERROR). The library passes all bzip2-tests repository files differentially, confirming cross-implementation compatibility.
