# Fuzz Campaign Report — e6a09d5

**Date**: 2026-02-23
**Commit**: e6a09d5 (feat: add API layer)
**Type**: Initial fuzz campaign + baseline

## Summary

First fuzz campaign against the complete libqbz2 implementation. Established
reference library baseline and ran differential fuzzing against both libraries.

**Key finding**: Initial runs had only 7-15 coverage features due to library
objects lacking fuzzer coverage instrumentation. After rebuilding with
`-fsanitize=fuzzer-no-link`, coverage jumped to 3,317-6,279 blocks.
Dictionary file (`bzip2.dict`) added for bzip2 format tokens.

## Reference libbz2 Baseline (60s each, ASAN)

| Harness | Executions | Exec/s | New Corpus | Crashes |
|---------|-----------|--------|------------|---------|
| fuzz_compress | 441,433 | 7,236 | 13 | 0 |
| fuzz_decompress | 527,487 | 8,647 | 9 | 0 |
| fuzz_streaming | 4,071 | 66 | 50 | 0 |
| fuzz_bufftobuff | 320,577 | 5,255 | 6 | 0 |

## libqbz2 Campaign (60s each, ASAN, proper instrumentation)

| Harness | Executions | Exec/s | Coverage (blocks) | Features | New Corpus | Crashes |
|---------|-----------|--------|-------------------|----------|------------|---------|
| fuzz_compress | 482,614 | 7,911 | — | — | 15 | 0 |
| fuzz_decompress | 12,256 | 200 | 3,317 | 9,605 | 312 | 0 |
| fuzz_differential | 2,901 | 47 | 6,279 | 23,633 | 526 | 0 |

## Differential Testing Results

- **Total differential comparisons**: 2,901
- **True divergences**: 0
- **Error divergences**: 0
- **Output mismatches**: 0

Both compression and decompression paths produce identical output to libbz2
on all 2,901 tested inputs.

## Infrastructure Improvements

1. **Dictionary file** (`fuzz/bzip2.dict`): bzip2 magic bytes, block headers,
   EOS markers. Dramatically improves coverage from 7 to 3,317+ blocks.
2. **CMakeLists.txt**: Updated to build library from source with
   `-fsanitize=fuzzer-no-link` for proper coverage instrumentation.
3. **run-quick-fuzz.sh**: Updated to use dictionary file automatically.

## Corpus Stats

- Seed corpus: 481 files (36 compress, 297 decompress, 148 malformed)
- Mini seeds: 5 files (37-44 bytes each, for fast fuzzer warmup)
- Differential seeds: 8 files (with harness header bytes)
- Generated corpus after campaign: 853 new entries

## Next Steps

- Run extended differential campaign (10+ minutes)
- Run streaming differential harness (fuzz_diff_streaming)
- Coverage analysis to identify untested paths
- Valgrind safety audit
