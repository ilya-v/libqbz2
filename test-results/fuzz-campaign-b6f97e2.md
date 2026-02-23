# Fuzz Campaign Report — b6f97e2

**Commit**: b6f97e2 — perf: 64-bit decompression bitstream with 4-byte absorption and bulk RLE fill
**Date**: 2026-02-23
**Focus**: Correctness verification of 64-bit decompression bitstream optimization

## Differential Fuzzing

| Harness | Duration | Executions | Coverage | Divergences | Crashes |
|---------|----------|------------|----------|-------------|---------|
| fuzz_differential (B2B) | 150s | 2,552 | 4,361 | 0 | 0 |
| fuzz_diff_streaming | 150s | 2,859 | 4,545 | 0 | 0 |
| **Total** | **300s** | **5,411** | — | **0** | **0** |

## Sustained Campaign (small inputs, high throughput)

| Harness | Duration | Executions | Coverage | max_len | Divergences |
|---------|----------|------------|----------|---------|-------------|
| fuzz_differential | 300s | 9,485 | 4,539 | 65536 | 0 |

## Verdict

The 64-bit decompression bitstream with 4-byte absorption and bulk RLE fill produces bit-for-bit identical output to reference libbz2. Zero divergences across 5,411 multi-block differential comparisons plus 9,485 small-input comparisons.
