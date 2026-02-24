# Differential Fuzz Campaign Report — 76996fe

**Commit**: 76996fe — perf: replace mainSort with libsais for all blocks >= 10000
**Date**: 2026-02-24
**Purpose**: Verify bit-identical output after major blocksort change (mainSort entirely removed)

## Differential Fuzzing

| Harness | Duration | Executions | Corpus | Coverage | Divergences | Crashes |
|---------|----------|------------|--------|----------|-------------|---------|
| fuzz_differential (B2B) | 646s | 7,058 | 1,030 / 68MB | 6,952 | 0 | 0 |
| fuzz_diff_streaming | 673s | 7,053 | 1,095 / 71MB | 6,849 | 0 | 0 |
| **Total** | **1,319s** | **14,111** | — | — | **0** | **0** |

## Crash-Finding (Decompression)

| Harness | Duration | Executions | Corpus | Coverage | Crashes |
|---------|----------|------------|--------|----------|---------|
| fuzz_decompress | 302s | 7,230 | 302 / 8MB | 6,058 | 0 |

## Seed Corpus

All seed categories used (7,000+ files per harness):
- compress_seeds (893), decompress_seeds (6,114), malformed_seeds (145)
- multiblock_seeds (7), bzip2_tests_seeds (38)

max_len=500000 to test multi-block boundaries.

## Verdict

Zero divergences across 14,111 differential comparisons. Zero crashes across 7,230 decompression fuzz runs. The mainSort-to-libsais replacement (76996fe) preserves bit-identical output with the reference libbz2 across all tested inputs including multi-block files, malformed streams, edge cases from bzip2-tests, and fuzzer-generated mutations.
