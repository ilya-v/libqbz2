# Validation Report: 2a855ff — FAIL (OOM crash)

**Commit:** 2a855ff fix: use stream allocator hooks in sais_bwt instead of direct malloc/free
**Date:** 2026-02-24
**Validator:** tester

## Build

| Variant | Result |
|---------|--------|
| Release | PASS |
| ASAN+UBSAN | PASS |
| Fuzz harnesses | PASS |

## Unit Tests (Release)

- **32/33 suites pass**, 1 FAIL (test_oom SEGFAULT)
- Tests: 1,185 pass (excluding OOM)
- Assertions: ~436,000 (excluding OOM)
- Time: 15.9s

### CRITICAL FAILURE: test_oom SEGFAULT

ASAN stack trace:
```
#0 generateMTFValues compress.c:143
#1 BZ2_compressBlock compress.c:624
#2 handle_compress bzlib.c:392
#3 BZ2_bzCompress bzlib.c:464
#4 try_compress_with_oom test_oom.c:249
```

Root cause: `sais_bwt()` now uses `BZALLOC`/`BZFREE` (stream allocator hooks) instead of `malloc`/`free`. When the OOM injector causes `BZALLOC` to return NULL inside `sais_bwt`, the function returns with `origPtr = -1`. `BZ2_blockSort()` then hits `AssertH(s->origPtr != -1, 1003)` which calls `BZ2_bz__AssertH__fail()` -- but in the OOM test context this leads to a SEGV.

Before this commit, `sais_bwt` used `malloc` directly, invisible to the OOM injector. The allocator change exposed a missing error propagation path: `sais_bwt` -> `BZ2_blockSort` -> `handle_compress` has no way to propagate `BZ_MEM_ERROR`.

## Differential Tests

- **335/335 pass** (206 standard + 129 multi-block), 0 divergences

## ASAN+UBSAN

- **32/33 pass**, test_oom SEGFAULT (same issue)
- 0 ASAN errors in passing tests
- 0 UBSAN errors in passing tests

## Quick Fuzz

| Harness | Runs | exec/s | Crashes | Time |
|---------|------|--------|---------|------|
| fuzz_compress | 70 | 2 | 0 | 24s |
| fuzz_decompress | 443 | 14 | 0 | 31s |
| fuzz_differential | 478 | 6 | 0 | 77s |
| fuzz_diff_streaming | 478 | 9 | 0 | 53s |

Differential harness: 0 divergences (new or known).

## Benchmarks

| Workload | BS | Compress | Decompress |
|----------|----|---------:|----------:|
| text-100k | 1 | 1.25x | 1.18x |
| text-100k | 5 | 1.49x | 1.18x |
| text-100k | 9 | 1.46x | 1.18x |
| binary-100k | 1 | 1.04x | 1.84x |
| binary-100k | 5 | 1.03x | 1.81x |
| binary-100k | 9 | 0.96x | 1.81x |
| repeated-100k | 1 | 4.51x | 0.89x |
| repeated-100k | 5 | 3.57x | 1.01x |
| repeated-100k | 9 | 3.31x | 0.98x |
| zeros-100k | 1 | 1.85x | 6.39x |
| zeros-100k | 5 | 1.86x | 6.49x |
| zeros-100k | 9 | 1.87x | 6.44x |

No regressions relative to prior commit.

## Known Issues

1. **CRITICAL (this commit):** OOM crash in `sais_bwt` when stream allocator returns NULL. SEGV in `generateMTFValues`. Reverted in 0326d27.

## Summary

**FAIL.** The allocator consistency change introduced a critical OOM crash. The library crashes (SEGV) when the custom allocator fails inside `sais_bwt`, because `BZ2_blockSort` uses `AssertH` (abort) instead of propagating an error code. All non-OOM functionality is correct -- differential tests, fuzz, and benchmarks are clean. The fix is to revert `sais_bwt` back to direct `malloc`/`free` (commit 0326d27).
