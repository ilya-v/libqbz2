# libqbz2 Requirements

## 1. Goal

Write a new bzip2 compression library from scratch — a clean-room rewrite, not a fork or incremental improvement of the existing libbz2 code. The new library must be an API-compatible drop-in replacement for libbz2, focused on optimization (throughput, latency), to make it as fast or faster than the best-in-class competitors. Use the original libbz2 source only as a behavioral reference for correctness, not as a starting point for the code.

## 2. API Compatibility

- Must be fully API-compatible with libbz2 — same header (`bzlib.h`), same types (`bz_stream`), same functions, same behavior, same error codes
- Programs linking against libbz2 must be able to switch to libqbz2 by replacing the library with no source changes
- Correctness first, then performance — never sacrifice conformance for speed
- Both compression and decompression must produce bit-for-bit identical output to libbz2 for the same inputs and parameters
- All public API functions must be implemented:
  - **Low-level streaming**: `BZ2_bzCompressInit`, `BZ2_bzCompress`, `BZ2_bzCompressEnd`, `BZ2_bzDecompressInit`, `BZ2_bzDecompress`, `BZ2_bzDecompressEnd`
  - **High-level FILE\* I/O**: `BZ2_bzReadOpen`, `BZ2_bzRead`, `BZ2_bzReadGetUnused`, `BZ2_bzReadClose`, `BZ2_bzWriteOpen`, `BZ2_bzWrite`, `BZ2_bzWriteClose64`, `BZ2_bzWriteClose`
  - **Utility (buffer-to-buffer)**: `BZ2_bzBuffToBuffCompress`, `BZ2_bzBuffToBuffDecompress`
  - **Version**: `BZ2_bzlibVersion`
- All error codes must match: `BZ_OK`, `BZ_RUN_OK`, `BZ_FLUSH_OK`, `BZ_FINISH_OK`, `BZ_STREAM_END`, `BZ_SEQUENCE_ERROR`, `BZ_PARAM_ERROR`, `BZ_MEM_ERROR`, `BZ_DATA_ERROR`, `BZ_DATA_ERROR_MAGIC`, `BZ_IO_ERROR`, `BZ_UNEXPECTED_EOF`, `BZ_OUTBUFF_FULL`, `BZ_CONFIG_ERROR`
- The `bz_stream` struct layout must be identical — same fields, same types, same offsets

## 3. Reference Implementation

- Clone and build libbz2 (from the bzip2 source at `https://sourceware.org/bzip2/`) as a reference implementation
- Use libbz2's source, tests, and behavior as the ground truth
- Reproduce libbz2's behavior exactly — match its compressed output byte-for-byte for all valid inputs and parameter combinations
- Decompression must accept everything libbz2 accepts and produce identical output
- Use libbz2's test suite as a baseline: libqbz2 must pass all of libbz2's existing tests without modification

## 4. Testing

### 4.1 Conformance Tests
- libbz2's own test suite must pass unmodified against libqbz2
- Round-trip testing: compress with libqbz2, decompress with libbz2 (and vice versa) — output must match the original input in all cases
- All block sizes (1–9) and all `workFactor` values must produce identical output to libbz2
- **External test corpus**: clone and use the bzip2-tests repository (`git://sourceware.org/git/bzip2-tests.git`) as an additional conformance and regression test suite. This repository contains cross-project test files specifically designed for bzip2 compatibility testing. All test files from this repo must be used as inputs for differential testing and as seed corpus for fuzz harnesses. Since we are building a highly compatible drop-in replacement, passing every test case in this repository is a hard requirement — any failure indicates a compatibility bug.

### 4.2 Unit Tests
- Comprehensive unit test suite covering all public API functions
- Edge cases, error paths, and boundary conditions
- Minimum 1000 unit tests
- Code coverage must be close to 100%

### 4.3 Fuzz Testing

#### 4.3.1 Fuzz Sources
All fuzzing (crash-finding and differential) must be seeded with and continuously run against:

- **Diverse uncompressed inputs**: text files, binary data, repetitive data, random data, zero-filled buffers, single-byte inputs, multi-megabyte inputs — to exercise compression paths
- **Valid bzip2 streams**: pre-compressed `.bz2` files from real-world corpora (e.g., the bzip2 test files, Silesia corpus compressed at all block sizes) — to exercise decompression paths
- **Malformed bzip2 streams**: truncated, bit-flipped, and structurally invalid compressed data — to exercise error handling in decompression

These sources must be integrated as seed inputs for all fuzz harnesses. The fuzz engine's own corpus (generated through coverage-guided mutation) supplements but does not replace these external sources.

#### 4.3.2 Fuzz Techniques
- Fuzz the compression path: random and structured uncompressed inputs across all block sizes and work factors
- Fuzz the decompression path: mutated compressed streams to test error handling and robustness
- Fuzz the streaming API: random chunk sizes, partial reads/writes, interleaved flush operations
- Fuzz the high-level FILE* API and buffer-to-buffer API
- Run fuzzing continuously and fix all issues found
- **Bonus: if fuzzing discovers bugs in libbz2 itself, document them and ensure libqbz2 avoids them**

#### 4.3.3 Differential Fuzzing
- In addition to crash-finding fuzzing, run differential fuzzing: feed fuzzed inputs to both libqbz2 and the reference libbz2, and compare their outputs byte-for-byte at all API levels (streaming, FILE* I/O, buffer-to-buffer)
- For compression: identical input + identical parameters must produce identical compressed output
- For decompression: identical compressed input must produce identical decompressed output, or identical error codes for invalid input
- Any divergence is a correctness bug in libqbz2 (unless it is a documented libbz2 bug)
- Differential fuzzing must use all fuzz sources as seed inputs
- Differential fuzzing must be included in the per-commit validation pipeline (via the quick fuzz script) and also run as sustained campaigns

### 4.4 Differential Testing
- Feed identical inputs to both libqbz2 and the reference libbz2, and compare their outputs byte-for-byte at every API level (streaming, FILE* I/O, buffer-to-buffer)
- Both compressed output and decompressed output must be bit-for-bit identical
- Error behavior must also match: when both libraries reject an input, the error codes must be identical
- Inputs must include: all fuzz sources, the standard benchmark workloads, the generated fuzz corpus, and any regression inputs from previously found bugs
- Test all parameter combinations: block sizes 1–9, work factors 0–250, `verbosity` levels, `small` decompress mode
- Differential testing must be part of the per-commit validation pipeline (see the per-commit validation section) — it is not optional background work

### 4.5 OOM Injection
- Systematically fail every allocation point (malloc, realloc, calloc) and verify the library handles it gracefully — no crashes, no leaks, no undefined behavior
- Use the `bz_stream` custom allocator hooks (`bzalloc`, `bzfree`, `opaque`) to inject failures without modifying library source

### 4.6 Memory Safety and Correctness
- All tests must pass under valgrind (memcheck) with zero errors
- All tests must pass under AddressSanitizer (ASAN) with zero errors
- All tests must pass under UndefinedBehaviorSanitizer (UBSAN) with zero errors
- All tests must pass under LeakSanitizer (LSAN) with zero leaks
- All tests must pass under MemorySanitizer (MSAN) with zero uninitialized reads (note: requires full-program instrumentation)
- Use a guard-page allocator (e.g., Electric Fence / DUMA) to catch any buffer overruns immediately — every 1-byte overrun must segfault, not silently corrupt
- Use an allocation-tracking allocator that records every alloc/free and verifies at teardown that every allocation was freed exactly once — catches leaks and double-frees precisely

### 4.7 Input Robustness
- Input truncation testing: feed compressed data truncated at every byte offset — the decompressor must return an error cleanly, never crash
- Bit-flip testing: systematically flip bits in valid compressed streams and verify the decompressor detects corruption (via CRC checks or structural validation) and returns appropriate error codes
- Resource-constrained runs: test under `ulimit` restrictions (limited stack size, memory, file descriptors) to verify graceful degradation
- Extreme parameters: block size 1 (100k blocks) through block size 9 (900k blocks), work factor 0 and 250, zero-length inputs, inputs that compress to larger output

### 4.8 Compiler Warnings and Static Analysis
- Must compile with zero warnings under both gcc and clang with `-Wall -Wextra -Wpedantic`
- Run clang `scan-build` or `cppcheck` and fix all reported issues
- Zero warnings from static analysis on the library source

### 4.9 Documentation
- All testing, benchmarking, and fuzzing results must be documented, focusing on outcomes and measurable metrics
- Document: pass/fail counts, coverage percentages, throughput numbers, crash counts, memory errors found and fixed, fuzz corpus size, and any regressions

### 4.10 Test Execution
- Tests must run in parallel, allocating up to 4 cores
- A complete testing loop (compile + run all tests) must finish within 2 minutes
- If the test suite grows beyond the 2-minute budget, split into two sets:
  - **Light test set**: used during development iteration, must stay under 2 minutes
  - **Full test set**: includes all tests, run before commits and during CI
- Development iteration always uses the light test set to keep the feedback loop fast

### 4.11 Per-Commit Validation

Every code change commit must be followed by a validation pass. The validation pass must take **at least 2 minutes and at most 2 minutes** per commit. If the pipeline finishes early, add more tests or repeat tests to fill the budget. If it exceeds the budget, trim lower-priority items.

#### 4.11.1 Validation Pipeline

The validation pipeline runs the following stages in order. All stages are mandatory for every commit.

| Stage | Description | Failure action |
|-------|-------------|----------------|
| **Build** | Compile the library and all test targets in both Release and ASAN/UBSAN modes | Abort validation, report build failure |
| **Unit tests** | Run the full unit test suite (or a curated fast subset if the full suite exceeds the time budget) | Report pass/fail counts, continue |
| **Differential** | Run the differential test suite: feed the same inputs to both libqbz2 and the reference libbz2, compare compressed and decompressed outputs byte-for-byte across all API levels and parameter combinations. Any divergence is a correctness bug. | Report any divergences immediately — divergences are critical bugs |
| **ASAN+UBSAN** | Run the full unit test suite under AddressSanitizer and UndefinedBehaviorSanitizer | Report any errors immediately — ASAN failures are critical bugs |
| **Quick fuzz** | Run the per-commit fuzz script (a single executable script with no arguments, located in `fuzz/`). The script runs a curated subset of fuzz harnesses (including differential fuzzing) with ASAN enabled. A test list file in `fuzz/` controls which harnesses are included. The script and test list are maintained separately from the validation pipeline and must be kept up to date as harnesses evolve. The script may use up to 3 cores and must fit within the time allocated by the validation pipeline. | Report any crashes or divergences immediately — fuzz crashes and differential divergences are critical bugs |
| **Benchmarks** | Run the standard benchmark workloads against the reference library (libbz2). Run at least once; run multiple times for statistical confidence if time permits | Report throughput numbers (MB/s) and speedup ratios |

#### 4.11.2 Time Budget

- **Minimum:** 2 minutes. If all stages complete in less than 2 minutes, extend the quick fuzz stage or add benchmark repetitions to fill the remaining time. Every second of the 2-minute budget should be used productively.
- **Maximum:** 2 minutes. If the pipeline cannot complete within 2 minutes, trim the unit test subset or reduce fuzz time (but never below 10 seconds per harness) to fit. ASAN and benchmarks are never trimmed.

#### 4.11.3 Validation Report

Each validation produces a report committed to `test-results/`, tagged with the 6-character commit ID (e.g., `validation-a1b2c3.md`). The report must include:

- Commit hash and description
- Unit test results: pass/fail counts, assertion counts, total time
- Differential test results: inputs tested, divergences found (with details if any)
- ASAN+UBSAN results: pass/fail counts, any errors found (with details)
- Quick fuzz results: harness names, run counts, execution rate, crashes found, divergences found, time per harness
- Benchmark results: throughput (MB/s) and speedup ratio vs libbz2 for each workload
- Total validation time

#### 4.11.4 Escalation

- Any ASAN error, fuzz crash, or differential divergence is a **critical bug**. The tester must immediately notify the worker and coordinator with full details (stack trace, reproducer, root cause if known).
- No new optimization work may begin until all critical bugs from the current commit are fixed.
- The full test suite (Valgrind, OOM injection, extended fuzzing, coverage analysis) runs separately as background work and does not need to fit in the 2-minute budget.

## 5. Directory Structure

### 5.1 Source Code (git-tracked)

| Directory | Contents | Owner |
|-----------|----------|-------|
| `src/` | Library implementation (C source files) | Worker |
| `include/` | Public API headers (`bzlib.h`) | Worker |
| `tests/` | Test source code (unit tests, integration tests, conformance tests) | Tester |
| `bench/` | Benchmark source code and workload data | Tester |
| `fuzz/` | Fuzz harness source code | Strategic Tester |

### 5.2 Build Output (gitignored)

| Directory | Contents |
|-----------|----------|
| `build/` | CMake build output (main tree). All variant builds (debug, release, asan, coverage, pgo, etc.) must go under `build/` or `test-output/`, not as separate top-level directories. |
| `.worktree/` | Git worktrees used by the testers for isolated builds |

### 5.3 Testing Artifacts

| Directory | Contents | Tracked |
|-----------|----------|---------|
| `test-output/` | Transient testing artifacts — raw logs, intermediate results, scratch data. Gitignored. May be deleted at any time without loss. | No |
| `test-results/` | Permanent testing records — per-commit validation reports, benchmark reports, coverage reports, fuzz campaign summaries, safety audit reports. All significant testing artifacts must be written here or copied here. Tagged with the commit ID they cover. | Yes |

**Rules for `test-results/`:**
- Every per-commit validation report goes here (not `test-output/`)
- Benchmark comparison reports go here
- Coverage analysis reports go here
- Fuzz campaign summaries go here (corpus stats, crashes found, time run)
- Safety audit reports (ASAN, UBSAN, Valgrind, guard-page, alloc-tracker) go here
- Files must be tagged with the 6-character commit ID they cover (e.g., `validation-a1b2c3.md`, `benchmark-a1b2c3.md`)
- This directory is the permanent record of the project's quality history

### 5.4 Other Directories

| Directory | Contents | Tracked |
|-----------|----------|---------|
| `reference/` | Reference libbz2 source and pre-built library (build artifacts under `reference/build/` are gitignored) | Yes (source only) |
| `requirements/` | Project requirements (this file) | Yes |
| `process/` | Agent rules and process scripts | Yes |
| `cmake/` | CMake modules and helpers | Yes |
| `scripts/` | Utility scripts | Yes |
| `logs/` | Runtime logs (project feed, injection logs) | No |

### 5.5 `.gitignore`

The following must be gitignored — these are build artifacts, transient outputs, and runtime files that must never be committed:

```
# Build output — ALL builds (debug, release, asan, coverage, pgo, etc.) go under build/
build/
*.o
*.a
*.so
*.so.*
*.dylib

# Test output (transient scratch data)
test-output/

# Git worktrees (used by testers for isolated builds)
.worktree/

# Reference build artifacts (source is tracked, builds are not)
reference/build/

# Editor and IDE files
*.swp
*.swo
*~
.vscode/
.idea/

# Runtime logs
logs/
```

**No stray build directories.** All build variants (debug, release, asan, coverage, fuzz, pgo, etc.) must be placed under `build/` as subdirectories (e.g., `build/release/`, `build/asan/`, `build/coverage/`). Top-level directories like `build-asan/` or `build-debug/` are not allowed.

## 6. Performance

- Benchmark against libbz2 and other bzip2-compatible implementations
- Target: at least 10x faster than libbz2 on most throughput metrics (both compression and decompression)
- Target: equal or better throughput and latency than the fastest competitors
- Benchmark on a variety of input types (text, binary, repetitive, random, small buffers, large buffers, all block sizes)
- A major or total architecture rework is a viable path to achieving the throughput targets — do not limit optimization to incremental micro-optimizations on the existing libbz2 architecture if a redesign would yield better results
- **The library must remain single-threaded.** Multi-threading is not allowed. All performance gains must come from algorithmic improvements, SIMD, cache optimization, and other single-threaded techniques.

### 6.1 Profiler-Guided Optimization

All optimization work must be guided by profiling data, not by intuition or guesswork. Before optimizing any code path:

1. **Profile first**: use `perf record` / `perf stat` / `perf annotate`, `callgrind`, or equivalent tools to identify where CPU cycles are actually spent. Report the hot functions, their percentage of total runtime, and the specific bottleneck (compute-bound, memory-bound, branch-bound).
2. **Target the bottleneck**: optimize the function or loop that the profiler identifies as the top consumer. Do not optimize code that the profiler shows is already fast or rarely executed.
3. **Measure after**: re-profile after each optimization to verify the bottleneck shifted and the speedup is real. Report before/after numbers.
4. **SIMD is required where applicable**: the BWT inverse, MTF decode, Huffman decode, CRC computation, and RLE encoding/decoding are all candidates for SIMD (SSE2/AVX2) vectorization. Scalar-only implementations of these hot loops are not acceptable as a final state — evaluate SIMD for every hot inner loop identified by the profiler.
5. **No "optimizations exhausted" without data**: do not declare optimization complete without a profiler report showing that the remaining hot paths are at the theoretical throughput limit (memory bandwidth, instruction throughput, or data dependency chains). If the profiler shows headroom, keep optimizing.

### 6.2 Table-Based Huffman Decoding

The bzip2 reference implementation uses tree-walking Huffman decoding, which is slow due to branch mispredictions and poor instruction-level parallelism. The library must replace this with a **table-based branchless decoder**.

**Required approach:** pre-compute a lookup table indexed by the next N bits of the bitstream. Each table entry stores the decoded symbol and the number of bits consumed (`symbol | nbBits`). A single table lookup replaces the entire tree walk. Since bzip2 uses canonical Huffman codes with lengths up to 20 bits, use a two-level table: a primary table covering short codes (e.g., up to 11 bits, 2048 entries) and an overflow table for longer codes. Use unaligned 64-bit reads for branchless bit buffer refill.

**Reference implementations:**

- **libdeflate** (`github.com/ebiggers/libdeflate`): table-based Huffman decoder, 2x faster than zlib. Clean C, good reference for two-level table construction.
- **Fabian Giesen's Oodle/Kraken blog** (`fgiesen.wordpress.com/2021/08/30/entropy-coding-in-oodle-data-huffman-coding/`): detailed analysis of table design trade-offs, achieving 250–373 MB/s.

**Constraints:** the bitstream format must not change — only the internal decode implementation. Bit-for-bit identical output to the reference is mandatory.

### 6.3 Profile-Guided Optimization (PGO)

The build system must support profile-guided optimization (PGO) as a standard build mode:

1. **Instrumented build**: compile with `-fprofile-generate` (GCC) or `-fprofile-instr-generate` (Clang) to produce an instrumented binary that records branch frequencies, loop trip counts, and call targets during execution.
2. **Training workload**: run the instrumented binary against a representative training workload — the standard benchmark suite (text, binary, repetitive, random data at multiple block sizes). The training workload must exercise both compression and decompression paths across all block sizes.
3. **Optimized build**: recompile with `-fprofile-use` (GCC) or `-fprofile-instr-use` (Clang) using the collected profile data. This enables the compiler to optimize branch layout, inline decisions, and loop unrolling based on real execution patterns.
4. **Benchmark both**: always report benchmark results for both the regular (`-O2`/`-O3`) build and the PGO build. PGO typically yields 5–15% improvement on branch-heavy code (Huffman decoding, state machines, MTF).
5. **PGO build target**: the CMake build system must provide a `pgo` build target (e.g., `cmake --build build/pgo`) that automates the full three-step process: instrumented build → training run → optimized rebuild. The PGO profile data and build artifacts go under `build/pgo/`.
6. **PGO in benchmarks**: the official benchmark numbers reported in validation reports should include both regular and PGO results, so the full optimization potential is visible.

### 6.4 Hardware-Accelerated CRC-32

The bzip2 format uses CRC-32 on every byte of input (both compression and decompression). The current slicing-by-8 software implementation is a significant bottleneck, especially on high-throughput data. The library must support **hardware-accelerated CRC-32** using the x86 PCLMULQDQ (carry-less multiply) and CRC32 instructions.

**Required approach:** use the Barrett reduction technique with PCLMULQDQ to process 64 bytes at a time, folding the CRC in parallel, then finalize with the scalar CRC32 instruction. This achieves ~10x throughput over slicing-by-8 on modern x86 CPUs.

**Reference implementations:**

- **Intel's CRC-32 with PCLMULQDQ** (white paper: "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction"): the foundational algorithm. Used by the Linux kernel (`arch/x86/crypto/crc32-pclmul_glue.c`).
- **zlib-ng** (`github.com/zlib-ng/zlib-ng`): production-quality C implementation of PCLMULQDQ-based CRC-32 with runtime CPU feature detection.
- **ISA-L** (`github.com/intel/isa-l`): Intel's high-performance CRC implementation with similar approach.

**Build-time dispatch:** the hardware CRC path must be selected at build time via CMake feature detection (check for SSE4.2 and PCLMULQDQ support). If the target CPU does not support these instructions, the build must fall back to the existing slicing-by-8 software implementation automatically. Both paths must produce identical CRC values.

**Constraints:** bit-for-bit identical CRC output is mandatory — the hardware and software paths must agree exactly. The CRC polynomial is fixed by the bzip2 format (CRC-32/ISO-HDLC, polynomial 0x04C11DB7).

### 6.5 Table-Based Huffman Encoding (Compression) — SATISFIED

**Status:** Already satisfied by the existing implementation. The compression encoding path uses `s->code[t][symbol]` / `s->len[t][symbol]` lookup tables with constant-time per-symbol access, an unrolled `BZ_ITAH` macro processing 50 symbols per group via table lookups, and `bsW()` writing to a 64-bit bit accumulator. The original libbz2 was already table-based on the encode side — the slow tree-walking problem only existed on the decode side (addressed in 6.2).

### 6.6 Stretch Goal: State-of-the-Art BWT Construction (libsais)

The current SA-IS implementation may not match the performance of the latest suffix array construction libraries. As a stretch goal, evaluate and potentially integrate **libsais** (`github.com/IlyadLT/libsais`) — a modern, cache-friendly suffix array construction library that is ~65% faster than libdivsufsort on typical inputs.

**Constraints:** BWT output must remain bit-for-bit identical to the reference. This is a stretch goal — prioritize correctness and other required optimizations first.

### 6.7 Stretch Goal: Cache-Oblivious Inverse BWT

The inverse BWT in decompression performs a pointer chase through a permutation array, which has poor cache locality on large blocks (up to 900KB). As a stretch goal, investigate **cache-oblivious** or **cache-friendly inverse BWT** algorithms that improve spatial locality.

**References:**

- Kärkkäinen, Kempa & Puglisi — research on cache-friendly BWT inversion achieving 2–4x speedup over the naive pointer-chase approach on large blocks.
- The key idea: reorder the traversal to improve sequential memory access patterns, or split the inverse BWT into cache-line-sized chunks processed in order.

**Constraints:** identical decompressed output is mandatory. This is a stretch goal — only pursue after all required optimizations are complete and validated.

## 7. Process

- Report status to the coordinator after every meaningful step — before and after making changes, after running tests, whenever hitting a blocker. Never work silently.
- Listen to the coordinator's guidance on priorities and project direction
- Install whatever tools, packages, and dependencies are needed — no restrictions on tooling
- Commit at meaningful milestones using conventional commit messages:
  - `feat:` — new functionality or capability
  - `fix:` — bug fixes
  - `test:` — test additions, changes, or infrastructure
  - `ops:` — build system, CI, tooling, process, or infrastructure changes
  - Keep the subject line short and descriptive; use the body for details when needed
