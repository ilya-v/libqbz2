# Fuzz Campaign Report — 8513bc3

**Commit**: 8513bc3 — perf: upgrade Huffman decode to 11-bit two-level table with branchless refill
**Date**: 2026-02-23

## Differential Fuzzing

| Harness | Duration | Executions | Coverage | Divergences | Crashes |
|---------|----------|------------|----------|-------------|---------|
| fuzz_differential (B2B) | 200s | 3,415 | 4,423 | 0 | 0 |
| fuzz_diff_streaming | 200s | 3,008 | 4,503 | 0 | 0 |
| **Total** | **400s** | **6,423** | — | **0** | **0** |

## High-Throughput Campaign (small inputs)

| Harness | Duration | Executions | Coverage | Divergences |
|---------|----------|------------|----------|-------------|
| fuzz_differential | 300s | 12,224 | 6,170 | 0 |

## Session Grand Total

| Metric | Value |
|--------|-------|
| Total differential comparisons | ~92,000 |
| Optimization commits verified | 12 |
| Divergences found | 0 |
| Crashes found | 0 |

## Verdict

The 11-bit two-level Huffman decode table with branchless refill produces bit-for-bit identical output to reference libbz2. Zero divergences across 6,423 multi-block comparisons plus 12,224 small-input high-throughput comparisons.
