# Fuzz Campaign Report — c50993d

**Commit**: c50993d — perf: add branch prediction hints to hot decompression paths
**Also covers**: 9dbcccd (batch CRC compression), a8609e9 (prefetch BWT construction)
**Date**: 2026-02-23
**Focus**: Comprehensive verification of multiple recent optimizations

## Differential Fuzzing

| Harness | Duration | Executions | Coverage | Divergences | Crashes |
|---------|----------|------------|----------|-------------|---------|
| fuzz_differential (B2B) | 200s | 3,407 | 5,293 | 0 | 0 |
| fuzz_diff_streaming | 200s | 3,040 | 4,462 | 0 | 0 |
| **Total** | **400s** | **6,447** | — | **0** | **0** |

All runs with max_len=500,000, multi-block seeds, bzip2.dict, ASAN+UBSAN.

## Cumulative Session Statistics

| Metric | Value |
|--------|-------|
| Total differential comparisons (all campaigns) | ~30,000+ |
| Divergences found | 0 (on fixed code) |
| Crash-finding executions | ~20,000+ |
| Crashes found | 0 |
| Optimization commits verified | 8 (CRC fix, SA-IS, 64-bit bitstream, prefetch, hybrid CRC, bulk memcpy, 64-bit decomp, batch CRC/prefetch/hints) |

## Verdict

All three optimizations (batch CRC for compression run encoding, prefetch in inverse BWT construction, branch prediction hints in decompression) produce bit-for-bit identical output to reference libbz2. Zero divergences across 6,447 differential comparisons with inputs up to 500KB.
