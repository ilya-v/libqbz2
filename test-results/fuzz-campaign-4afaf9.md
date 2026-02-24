# Fuzz Campaign Report: 4afaf97

**Commit**: 4afaf97 (test: add bzopen/read-API coverage driver)
**Source commit**: 0326d27 (fix: revert sais_bwt to direct malloc/free)
**Date**: 2026-02-24
**Duration**: ~180 seconds per harness (3 minutes)
**Seed corpus**: 7,250 files (all categories)

## Results

| Harness | Runs | Crashes | Divergences | Exec/s | Max Input |
|---------|------|---------|-------------|--------|-----------|
| fuzz_differential | 964 | 0 | 0 | 4 | 500KB |
| fuzz_diff_streaming | 964 | 0 | 0 | 5 | 500KB |
| fuzz_compress | 921 | 0 | -- | 5 | 500KB |
| fuzz_decompress | 6,153 | 0 | -- | 19 | 500KB |
| **Total** | **9,002** | **0** | **0** | | |

## Coverage Points

| Harness | Coverage Points | Features |
|---------|----------------|----------|
| fuzz_differential | 8,769 | 42,082 |
| fuzz_diff_streaming | 6,841 | 39,571 |
| fuzz_compress | 10,080 | 52,011 |
| fuzz_decompress | 6,042 | 17,965 |

## Verification Scope

This campaign verifies the sais_bwt allocator revert (0326d27) which fixed a critical OOM regression introduced in 2a855ff. The differential harnesses compare output against the reference libbz2 for both compression and decompression across all block sizes and work factors.

## Cumulative Statistics

- Total differential comparisons (project lifetime): ~130,000+
- Total divergences found: 0
- Total crashes found: 2 (both fixed in 981dd00)
- Current code: **CLEAN** — no known correctness or safety issues
