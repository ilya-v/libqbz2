# Fuzz Campaign Report — 981dd00

**Commit**: 981dd00 — fix: add bounds checks to Huffman decode table builder
**Date**: 2026-02-24
**Purpose**: Verify fix for 2 heap buffer overflow crashes (bug-huffman-decode-oob-8513bc3)

## Crash Reproducer Verification

All 3 crash reproducers from the original bug report now pass without crashing:
- `crash-0218d8970f95f75b67927261802f9a33b1d03789` (overflow sub-table OOB) — PASS
- `crash-b592ec8bbc50a9d823d83fc654c550a617653acb` (primary table OOB) — PASS
- `crash-9c7917ada4bf511957fef614193f936e9e727638` (variant) — PASS

## Differential Fuzzing

| Harness | Duration | Executions | Divergences | Crashes |
|---------|----------|------------|-------------|---------|
| fuzz_differential (B2B) | 201s | 3,064 | 0 | 0 |
| fuzz_diff_streaming | 201s | 3,270 | 0 | 0 |
| **Total** | **402s** | **6,334** | **0** | **0** |

## Crash-Finding (Decompression)

| Harness | Duration | Executions | Crashes |
|---------|----------|------------|---------|
| fuzz_decompress | 201s | 9,573 | 0 |

## Seed Corpus

All seed categories used (546 files):
- compress_seeds (36), decompress_seeds (297), malformed_seeds (148)
- multiblock_seeds (7), bzip2_tests_seeds (38), coverage_seeds (10)

## Verdict

Fix 981dd00 eliminates both heap buffer overflow crashes. Zero divergences across 6,334 differential comparisons. Zero crashes across 9,573 decompression fuzz runs with malformed inputs. The bounds checks correctly reject malformed Huffman tables that previously caused out-of-bounds writes.
