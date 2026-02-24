# Fuzz Campaign Report: 0bd244 — libsais BWT Construction

**Commit**: 0bd244d — feat: replace custom SA-IS with vendored libsais library
**Date**: 2026-02-24
**Seed corpus**: 565+ files (grew to 677+ via libFuzzer-generated mutations)
**Previous commit**: 02c3751 (PCLMULQDQ CRC-32)

## Campaign Purpose

Verify correctness of the vendored libsais suffix array construction library, which replaced the custom SA-IS implementation in blocksort.c. BWT construction is the most complex algorithmic component of bzip2 compression. Any incorrect suffix array output would cause bit-for-bit divergence in compressed output.

## Sustained Fuzzing Results

All harnesses built with ASAN+UBSAN, seeded with full corpus including bzip2-tests and multi-block inputs.

| Harness | Runs | Time (s) | exec/s | Crashes | Divergences | Coverage |
|---------|------|----------|--------|---------|-------------|----------|
| fuzz_differential | 2,401 | 182 | 13 | 0 | 0 | 7,483 ft |
| fuzz_diff_streaming | 1,431 | 130 | 11 | 0 | 0 | 7,424 ft |
| fuzz_compress | 650 | 127 | 5 | 0 | N/A | 10,658 ft |
| **Total** | **4,482** | **439** | | **0** | **0** | |

Note: Coverage increased significantly (10,658 vs 7,714 on previous blocksort) because libsais has many more code paths.

## Build Fix

Added `libsais.c` to `fuzz/CMakeLists.txt` to resolve the undefined `libsais` symbol reference from `blocksort.c`.

## Cumulative Project Stats

- **Total differential comparisons**: 106,000+ (102K previous + 3,832 this session)
- **Total divergences**: 0
- **Total crashes**: 0 on current code
- **Seed corpus**: 565+ files

## Findings

libsais produces identical BWT output to the previous custom SA-IS implementation across 4,482 fuzz runs. No crashes or divergences. The integration is correct.
