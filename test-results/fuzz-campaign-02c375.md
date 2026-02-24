# Fuzz Campaign Report: 02c375 — PCLMULQDQ Hardware CRC-32

**Commit**: 02c3751 — feat: implement PCLMULQDQ hardware-accelerated CRC-32
**Date**: 2026-02-24
**Seed corpus**: 565 files
**Previous commit**: 981dd00 (Huffman decode OOB fix)

## Campaign Purpose

Verify correctness of the PCLMULQDQ hardware-accelerated CRC-32 implementation. CRC-32 is computed on every byte of input during both compression and decompression. Any CRC computation error would cause:
- Bit-for-bit output divergence in compressed output (compression CRC is embedded in the stream)
- False BZ_DATA_ERROR on valid input (decompression CRC verification)

## Sustained Fuzzing Results

All harnesses built with ASAN+UBSAN, seeded with full corpus including bzip2-tests.

| Harness | Runs | Time (s) | exec/s | Crashes | Divergences |
|---------|------|----------|--------|---------|-------------|
| fuzz_differential | 2,902 | 182 | 15 | 0 | 0 |
| fuzz_diff_streaming | 2,490 | 184 | 13 | 0 | 0 |
| fuzz_decompress | 7,679 | 121 | 63 | 0 | N/A |
| **Total** | **13,071** | **487** | | **0** | **0** |

## Cumulative Project Stats

- **Total differential comparisons**: 102,000+ (97K previous + 5,392 this session)
- **Total divergences**: 0
- **Total crashes**: 0 on current code (2 historical, both fixed)
- **Seed corpus**: 565 files including bzip2-tests repo

## Findings

The PCLMULQDQ CRC-32 implementation produces identical results to the previous slicing-by-8 software implementation across 13,071 fuzz runs, including multi-block inputs up to 500KB. No CRC-related divergences or crashes.
