# Fuzz Campaign Report — f56cb57

**Commit**: f56cb57 — perf: 64-bit bitstream writer for compression
**Date**: 2026-02-23
**Focus**: Correctness verification of 64-bit bitstream writer optimization

## Differential Fuzzing

| Harness | Duration | Executions | Coverage | Divergences | Crashes |
|---------|----------|------------|----------|-------------|---------|
| fuzz_differential (B2B) | 120s | 1,812 | 4,468 | 0 | 0 |
| fuzz_diff_streaming | 120s | 2,283 | 4,379 | 0 | 0 |
| **Total** | **240s** | **4,095** | — | **0** | **0** |

All runs with max_len=500,000, multi-block seeds, bzip2.dict, ASAN+UBSAN.

## Sustained Fuzzing (ea4270b baseline, run during wait for this commit)

| Harness | Duration | Executions | Coverage | Crashes |
|---------|----------|------------|----------|---------|
| fuzz_decompress | 300s | 8,256 | 3,380 | 0 |
| fuzz_streaming | 300s | 817 | 6,029 | 0 |

## Verdict

The 64-bit bitstream writer produces bit-for-bit identical compressed output to the reference libbz2. Zero divergences across 4,095 differential comparisons.
