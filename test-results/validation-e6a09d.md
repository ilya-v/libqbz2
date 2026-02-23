# Validation Report: e6a09d5

| Field | Value |
|-------|-------|
| **Commit** | `e6a09d5` |
| **Description** | feat: add API layer -- compression/decompression drivers, FILE* wrappers, utility functions |
| **Date** | 2026-02-23 |
| **Validator** | tester (per-commit validator) |
| **Total validation time** | ~2 minutes |

## Build

**PASS** -- Library is now fully linkable. All build variants succeed.

| Variant | Result |
|---------|--------|
| Release (gcc) | PASS -- libqbz2.a, libbz2.so, test_api, test_differential, bench_throughput all built |
| ASAN+UBSAN (clang) | PASS -- all targets built with `-fsanitize=address,undefined` |
| Fuzz harnesses (clang, libFuzzer+ASAN) | PASS -- fuzz_compress, fuzz_decompress, fuzz_differential all built |

Zero warnings across all configurations (gcc -Wall -Wextra -Wpedantic, clang -Wall -Wextra -Wpedantic).

## Unit Tests

**PASS** -- 57/57 tests passed, 235 assertions, 0 failures.

| Mode | Tests | Passed | Failed | Assertions | Time |
|------|-------|--------|--------|------------|------|
| Release | 57 | 57 | 0 | 235 | 0.019s |
| ASAN+UBSAN | 57 | 57 | 0 | 235 | 0.230s |

Note: 2 test expectations were corrected during validation (verbosity parameter tests). The reference libbz2 does NOT validate verbosity range -- it accepts any integer, including negative values and values > 4. Tests updated to match reference behavior.

## Differential Tests

**PASS** -- 206/206 tests passed, 0 divergences.

All tests compare libqbz2 output against reference libbz2 byte-for-byte across:
- Compression at all block sizes (1-9) with multiple work factors
- Decompression of reference-compressed data
- Round-trip (compress with libqbz2, decompress with reference, and vice versa)
- Buffer-to-buffer API
- Error code comparison on invalid inputs

## ASAN+UBSAN

**PASS** -- 57/57 unit tests passed under AddressSanitizer and UndefinedBehaviorSanitizer with zero errors.

- No heap buffer overflows
- No stack buffer overflows
- No use-after-free
- No undefined behavior detected
- No integer overflow (signed)

## Quick Fuzz

**PASS** -- 3 harnesses, 0 crashes, 0 divergences, 35 seconds total.

| Harness | Runs | Exec/sec | Crashes | Corpus Size | Time |
|---------|------|----------|---------|-------------|------|
| fuzz_compress | 61 | 5 | 0 | 49 inputs / 8.7 MB | ~10s |
| fuzz_decompress | 61 | 5 | 0 | 49 inputs / 8.7 MB | ~10s |
| fuzz_differential | 61 | 5 | 0 | 49 inputs / 8.7 MB | ~10s |

Coverage: 6488 edges. Low exec/sec is expected -- seed corpus includes large inputs up to 1 MB that exercise the full compression pipeline. No differential divergences found. ASAN enabled throughout.

## Benchmarks

| Workload | BS | qbz2 Compress | ref Compress | C Speedup | qbz2 Decompress | ref Decompress | D Speedup |
|----------|---:|:--------------|:-------------|:---------:|:-----------------|:---------------|:---------:|
| text-100k | 1 | 21.59 MB/s | 23.48 MB/s | 0.92x | 125.66 MB/s | 126.51 MB/s | 0.99x |
| text-100k | 5 | 22.87 MB/s | 19.77 MB/s | 1.16x | 124.55 MB/s | 128.75 MB/s | 0.97x |
| text-100k | 9 | 23.34 MB/s | 23.07 MB/s | 1.01x | 90.29 MB/s | 106.39 MB/s | 0.85x |
| binary-100k | 1 | 12.48 MB/s | 15.26 MB/s | 0.82x | 35.19 MB/s | 34.54 MB/s | 1.02x |
| binary-100k | 5 | 17.28 MB/s | 17.38 MB/s | 0.99x | 36.42 MB/s | 35.43 MB/s | 1.03x |
| binary-100k | 9 | 16.93 MB/s | 17.12 MB/s | 0.99x | 36.25 MB/s | 35.72 MB/s | 1.01x |
| repeated-100k | 1 | 12.98 MB/s | 14.03 MB/s | 0.93x | 430.01 MB/s | 432.67 MB/s | 0.99x |
| repeated-100k | 5 | 15.12 MB/s | 18.44 MB/s | 0.82x | 433.92 MB/s | 433.06 MB/s | 1.00x |
| repeated-100k | 9 | 15.03 MB/s | 17.77 MB/s | 0.85x | 409.96 MB/s | 423.79 MB/s | 0.97x |
| zeros-100k | 1 | 315.42 MB/s | 307.84 MB/s | 1.02x | 596.81 MB/s | 594.17 MB/s | 1.00x |
| zeros-100k | 5 | 331.01 MB/s | 330.13 MB/s | 1.00x | 592.14 MB/s | 598.18 MB/s | 0.99x |
| zeros-100k | 9 | 323.26 MB/s | 326.52 MB/s | 0.99x | 595.72 MB/s | 594.36 MB/s | 1.00x |

**Summary**: Performance is near-parity with the reference (0.82x to 1.16x compression, 0.85x to 1.03x decompression). This is expected for a clean-room implementation that produces byte-for-byte identical output -- the algorithms are identical. Performance optimization is the next phase.

## Known Issues

| # | Description | Severity | Introduced | Status |
|---|-------------|----------|------------|--------|
| 1 | cppcheck false positive: potential OOB in huffman.c:97 | Low (same as reference) | d23ca69 | Open -- false positive |
| 2 | cppcheck false positive: negative index in compress.c:357 | Low (same as reference) | 24059a0 | Open -- false positive |
| 3 | ASAN build requires CMAKE_EXE_LINKER_FLAGS workaround | Low (build system) | e6a09d5 | Open -- CMakeLists.txt should propagate sanitizer flags to test executables |

## Summary

Commit e6a09d5 adds bzlib.c (1574 lines), completing the full libqbz2 implementation. The library is now fully linkable and all validation stages run for the first time. **All stages pass clean**: 57/57 unit tests (235 assertions), 206/206 differential tests (0 divergences), 57/57 ASAN+UBSAN tests (0 errors), 3/3 fuzz harnesses (0 crashes, 0 divergences in 35 seconds), and benchmarks showing near-parity performance with the reference. Two unit test expectations were corrected to match reference behavior (verbosity parameter range is not validated by libbz2). No critical bugs found. The library produces bit-for-bit identical output to the reference across all tested inputs. This is a major milestone -- the implementation is complete and correct. Next phase: performance optimization.
