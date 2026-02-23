# BUG REPORT: Heap buffer overflow in 11-bit Huffman decode table — 8513bc3

**Severity**: CRITICAL (security — arbitrary heap write from crafted input)
**Introduced by**: 8513bc3 — perf: upgrade Huffman decode to 11-bit two-level table with branchless refill
**Status**: OPEN
**Date found**: 2026-02-23
**Found by**: Strategic tester sustained decompression fuzzing (libFuzzer + ASAN)

## Summary

Two heap buffer overflow vulnerabilities in the Huffman decode table building code in `decompress.c`. Malformed bz2 inputs with crafted Huffman code lengths cause out-of-bounds writes to:
1. The primary decode table (`decode_fast[t]`, 2048 entries)
2. The overflow sub-table (`decode_overflow[t]`, 512 entries)

Reference libbz2 correctly returns `BZ_DATA_ERROR` for both inputs.

## Crash 1 — Overflow sub-table OOB write (decompress.c:446)

**ASAN error**: `SEGV on unknown address`
**Write location**: `ovf[sub_offset + base_idx2 + k]` exceeds 512-entry `decode_overflow[t]` array

```
==2722624==ERROR: AddressSanitizer: SEGV on unknown address 0x7f30561d09f0
    #0 0x55f8d7517f72 in BZ2_decompress decompress.c:446:57
    #1 0x55f8d747b61b in BZ2_bzDecompress bzlib.c:855:20
    #2 0x55f8d749931c in BZ2_bzBuffToBuffDecompress bzlib.c:1360:10
```

**Reproducer**: `test-results/crash-reproducers/huffman-overflow-subtable.bz2` (63KB)

## Crash 2 — Primary table OOB write (decompress.c:410)

**ASAN error**: `heap-buffer-overflow, WRITE of size 4`
**Write location**: `tbl[base_idx + k]` exceeds 2048-entry `decode_fast[t]` array

```
==2722832==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x7e3c399ff2b0
WRITE of size 4 at 0x7e3c399ff2b0 thread T0
    #0 0x5608d044f4c4 in BZ2_decompress decompress.c:410:40
    #1 0x5608d03b161b in BZ2_bzDecompress bzlib.c:855:20
    #2 0x5608d03cf31c in BZ2_bzBuffToBuffDecompress bzlib.c:1360:10

0x7e3c399ff2b0 is located 0 bytes after 125616-byte region [0x7e3c399e0800,0x7e3c399ff2b0)
allocated by thread T0 here:
    #0 in malloc
    #1 in BZ2_bzDecompressInit bzlib.c:519:8
```

**Reproducer**: `test-results/crash-reproducers/huffman-overflow-primary.bz2` (500KB)

## Root Cause Analysis

The decode table building loop at `decompress.c:395-449` iterates over Huffman symbols in canonical order, computing `code_val` (the codeword) incrementally. It builds a two-level lookup table:

1. **Primary table** (2048 entries, indexed by top 11 bits): Direct decode for codes <= 11 bits
2. **Overflow sub-tables** (512 entries total): For codes > 11 bits

The bug: the loop trusts that `code_val` stays within bounds implied by the Huffman code lengths. For valid Huffman codes, `code_val < (1 << sym_len)` always holds. But malformed bz2 streams can contain invalid Huffman code length arrays that violate this invariant.

**Primary table overflow (line 410)**:
```c
Int32 pad = BZ_DECODE_TABLE_BITS - sym_len;  // e.g., 11 - 5 = 6
Int32 base_idx = code_val << pad;             // if code_val is too large, base_idx >= 2048
Int32 fill_count = 1 << pad;                  // 64
for (k = 0; k < fill_count; k++)
   tbl[base_idx + k] = entry;                 // OOB write!
```

**Overflow sub-table overflow (line 446)**:
```c
Int32 base_idx2 = suffix << pad2;
Int32 fill2 = 1 << pad2;
for (k = 0; k < fill2; k++)
   ovf[sub_offset + base_idx2 + k] = ovf_entry;  // OOB write!
```

## Reference Behavior

```
$ ./ref_test crash-reproducers/huffman-overflow-subtable.bz2
Reference result: -4 (BZ_DATA_ERROR)

$ ./ref_test crash-reproducers/huffman-overflow-primary.bz2
Reference result: -4 (BZ_DATA_ERROR)
```

Reference libbz2 does not use a decode table — it uses a sequential bit-by-bit Huffman decode that naturally rejects invalid codewords during the MTF decode phase.

## Recommended Fix

Add bounds checks before both table writes:

```c
// Before line 410 (primary table):
if (base_idx + fill_count > tbl_size) RETURN(BZ_DATA_ERROR);

// Before line 446 (overflow sub-table):
if (sub_offset + base_idx2 + fill2 > 512) RETURN(BZ_DATA_ERROR);
```

Or more defensively, validate `code_val < (1 << cur_len)` after each increment to catch malformed Huffman tables early.
