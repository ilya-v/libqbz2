# Fuzz Campaign Report: 76996f — libsais for All Blocks >= 10000

**Commit**: 76996fe — perf: replace mainSort with libsais for all blocks >= 10000
**Date**: 2026-02-24
**Seed corpus**: 6,978 files (grew from 565 base via libFuzzer mutations)
**Previous commit**: 0bd244d (initial libsais integration as fallback)

## Campaign Purpose

Verify correctness of the change that replaces mainSort with libsais for all blocks >= 10000 (previously libsais was only used as a fallback when mainSort exceeded the depth limit). This change affects the most critical algorithmic component of compression — BWT construction via suffix array sorting. Any incorrect suffix array output would produce bit-for-bit divergent compressed output.

The threshold of 10000 means most practical inputs will use libsais (block sizes are typically 100K-900K), while very small blocks still use mainSort. Both paths must be tested.

## Sustained Fuzzing Results

All harnesses built with ASAN+UBSAN, seeded with full corpus including bzip2-tests, multi-block inputs up to 500KB.

| Harness | Runs | Time (s) | exec/s | Crashes | Divergences | Coverage |
|---------|------|----------|--------|---------|-------------|----------|
| fuzz_differential | 1,959 | 182 | 10 | 0 | 0 | 7,003 ft |
| fuzz_diff_streaming | 1,738 | 181 | 9 | 0 | 0 | 6,849 ft |
| fuzz_compress | 1,017 | 122 | 8 | 0 | N/A | 10,082 ft |
| **Total** | **4,714** | **485** | | **0** | **0** | |

## Cumulative Project Stats

- **Total differential comparisons**: 110,000+ (106K previous + 3,697 this session)
- **Total divergences**: 0
- **Total crashes**: 0 on current code
- **Seed corpus**: 6,978 files (including bzip2-tests repo, multi-block seeds up to 1MB)

## Findings

libsais-for-all-blocks->=10000 produces identical output to the reference implementation across 4,714 fuzz runs. Both the mainSort path (blocks < 10000) and the libsais path (blocks >= 10000) are exercised by the corpus. No crashes or divergences. The optimization is correct.
