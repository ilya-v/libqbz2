# Fuzz Campaign Report — 46ebafb (and 1f048d6)

**Commits**:
- 1f048d6 — perf: hybrid CRC in decompression — inline for single bytes, batch for runs
- 46ebafb — perf: bulk memcpy for compressed output copy
**Date**: 2026-02-23

## Differential Fuzzing

| Harness | Duration | Executions | Coverage | Divergences | Crashes |
|---------|----------|------------|----------|-------------|---------|
| fuzz_differential (B2B) | 120s | 2,002 | 4,513 | 0 | 0 |
| fuzz_diff_streaming | 120s | 2,297 | 4,396 | 0 | 0 |
| **Total** | **240s** | **4,299** | — | **0** | **0** |

All runs with max_len=500,000, multi-block seeds, bzip2.dict, ASAN+UBSAN.

## Verdict

Both optimizations (hybrid CRC decompression, bulk memcpy output copy) produce bit-for-bit identical output to reference libbz2. Zero divergences across 4,299 differential comparisons.
