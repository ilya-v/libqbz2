# Fuzz Campaign Report — 329da9d (and d061e3d)

**Commits**:
- d061e3d — perf: flatten decompression MTF decode to simple array with memmove
- 329da9d — perf: add table-based Huffman decode for decompression fast path
**Date**: 2026-02-23

## Differential Fuzzing (329da9d)

| Harness | Duration | Executions | Coverage | Divergences | Crashes |
|---------|----------|------------|----------|-------------|---------|
| fuzz_differential (B2B) | 200s | 2,882 | 4,423 | 0 | 0 |
| fuzz_diff_streaming | 200s | 3,137 | 4,470 | 0 | 0 |
| **Total** | **400s** | **6,019** | — | **0** | **0** |

## Differential Fuzzing (d061e3d)

| Harness | Duration | Executions | Coverage | Divergences | Crashes |
|---------|----------|------------|----------|-------------|---------|
| fuzz_differential (B2B) | 150s | 2,671 | 6,257 | 0 | 0 |
| fuzz_diff_streaming | 300s | 4,326 | 4,470 | 0 | 0 |
| **Total** | **450s** | **6,997** | — | **0** | **0** |

## Session Grand Total

| Metric | Value |
|--------|-------|
| Total differential comparisons | ~73,000 |
| Optimization commits verified | 11 |
| Divergences found | 0 |
| Crashes found | 0 |

## Verdict

Both the flat MTF decode and table-based Huffman decode produce bit-for-bit identical output to reference libbz2. Zero divergences across 13,016 combined differential comparisons.
