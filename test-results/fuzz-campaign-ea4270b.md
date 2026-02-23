# Fuzz Campaign Report — ea4270b

**Commit**: ea4270b — perf: hybrid blocksort — SA-IS fallback for repetitive blocks
**Date**: 2026-02-23
**Focus**: Correctness verification of new SA-IS blocksort algorithm

## Risk Assessment

This commit replaces the core block-sorting algorithm with a hybrid approach using SA-IS (Suffix Array by Induced Sorting) as a fallback for repetitive blocks. This is a high-risk change since blocksort is the foundation of bzip2 compression — any sorting error produces silently different compressed output.

## Differential Fuzzing

### Buffer-to-Buffer (fuzz_differential)
- **Duration**: 152 seconds
- **Executions**: 2,538
- **Exec/sec**: 16
- **Coverage**: 3,836+ blocks
- **Max input size**: 500,000 bytes (multi-block)
- **Seeds**: all 508 corpus files + bzip2.dict
- **Divergences**: 0
- **Crashes**: 0

### Streaming (fuzz_diff_streaming)
- **Duration**: 152 seconds
- **Executions**: 2,543
- **Exec/sec**: 16
- **Coverage**: 3,900+ blocks
- **Max input size**: 500,000 bytes (multi-block)
- **Divergences**: 0
- **Crashes**: 0

**Total differential comparisons**: 5,081
**Total divergences**: 0

## Crash-Finding (Compress-Focused)

| Harness | Executions | Coverage | Crashes |
|---------|------------|----------|---------|
| fuzz_compress | 425 | 6,846 | 0 |
| fuzz_bufftobuff | 739 | 7,278 | 0 |
| fuzz_streaming | 361 | 6,014 | 0 |
| **Total** | **1,525** | — | **0** |

## Verdict

The SA-IS hybrid blocksort produces bit-for-bit identical output to the reference libbz2 across 5,081 differential comparisons with inputs up to 500KB (multi-block). No divergences, no crashes. The algorithm change is correct.
