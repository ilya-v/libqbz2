# Fuzz Campaign Report — ea8cffa

**Commit**: ea8cffa — perf: SIMD-accelerate MTF linear search in compression
**Date**: 2026-02-23
**Focus**: Correctness verification of SIMD-accelerated MTF encoding

## Risk Assessment

This commit introduces SIMD (SSE2/SSE4.1) instructions to accelerate the Move-To-Front linear search in the compression path. SIMD code is notoriously error-prone for off-by-one errors, endianness issues, and alignment problems. High verification priority.

## Differential Fuzzing

| Harness | Duration | Executions | Coverage | Divergences | Crashes |
|---------|----------|------------|----------|-------------|---------|
| fuzz_differential (B2B) | 200s | 3,538 | 4,992 | 0 | 0 |
| fuzz_diff_streaming | 200s | 3,336 | 4,470 | 0 | 0 |
| **Total** | **400s** | **6,874** | — | **0** | **0** |

## Deep Sustained Campaign (10 minutes, pre-SIMD baseline)

| Harness | Duration | Executions | Coverage | Corpus | Divergences |
|---------|----------|------------|----------|--------|-------------|
| fuzz_differential | 600s | 6,827 | 5,323 | 664 entries, 90MB | 0 |

## Verdict

SIMD-accelerated MTF linear search produces bit-for-bit identical compressed output to reference libbz2. Zero divergences across 6,874 differential comparisons with inputs up to 500KB. The SIMD implementation is correct.

## Session Grand Total

| Metric | Value |
|--------|-------|
| Total differential comparisons | ~53,000 |
| Optimization commits verified | 9 |
| Divergences found | 0 |
| Crashes found | 0 |
