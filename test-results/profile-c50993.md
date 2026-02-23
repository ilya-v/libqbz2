# Profile Report: c50993d — gprof analysis

**Commit:** c50993d  
**Date:** 2026-02-23  
**Profiler:** gprof (gcc -pg -O2)  
**Data sizes:** text 1MB (/usr/share/dict/words), binary 1MB (/dev/urandom), zeros 1MB

## Summary Tables

### Compression — Top Functions by Time

| Function | Text (%) | Binary (%) | Zeros (%) | Notes |
|----------|----------|------------|-----------|-------|
| mainSort | **63.2** | **29.6** | 8.0 | BWT sorting — dominant for text |
| BZ2_compressBlock | 26.3 | **60.8** | 0.0 | Huffman encoding + MTF — dominant for binary |
| sendMTFValues | 7.9 | 5.6 | 0.0 | Huffman code output |
| add_pair_to_block | 2.6 | 4.0 | **36.0** | RLE encoding + CRC — dominant for zeros |
| fallbackSort | 0.0 | 0.0 | 20.0 | SA-IS fallback path — significant for zeros |
| BZ2_crc32_update | 0.0 | 0.0 | 16.0 | CRC computation — visible for zeros |
| BZ2_blockSort | 0.0 | 0.0 | 16.0 | Sort dispatch — zeros profile |
| induceL_byte | 0.0 | 0.0 | 4.0 | SA-IS suffix array construction |

### Decompression — Top Functions by Time

| Function | Text (%) | Binary (%) | Zeros (%) | Notes |
|----------|----------|------------|-----------|-------|
| BZ2_decompress | **71.7** | **70.2** | 12.5 | Core Huffman decode + inverse BWT — dominant |
| BZ2_bzDecompress | 21.7 | 26.8 | 0.0 | Outer loop (RLE output + CRC) |
| BZ2_crc32_update | 0.0 | 0.0 | **87.5** | CRC — dominant for zeros decompression |

### Key Observations

**Compression bottlenecks:**
1. **Text**: 63% in mainSort (BWT sorting). This is the single biggest target for compression speed improvement on real-world data.
2. **Binary (random)**: 61% in BZ2_compressBlock (Huffman encoding). Random data has high entropy — the sort is fast but encoding is expensive because the compressed output is nearly as large as the input.
3. **Zeros (repetitive)**: 36% in add_pair_to_block (RLE + CRC), 20% in fallbackSort (SA-IS), 16% in CRC. The data compresses to 45 bytes, so most time is in input processing, not encoding.

**Decompression bottlenecks:**
1. **Text and Binary**: 70-72% in BZ2_decompress (Huffman decode + inverse BWT pointer chase). This is where the 64-bit bitstream optimization and BWT prefetch are already helping. Remaining time is in the outer loop (BZ2_bzDecompress) which does RLE output and CRC.
2. **Zeros**: 87.5% in BZ2_crc32_update — the decompression itself is trivial for zeros, but CRC on 1MB of output dominates. The slicing-by-8 CRC is already optimized.

## Detailed Call Graphs

### Compression — Text 1MB, bs=9, 10 iterations

```
BZ2_bzBuffToBuffCompress (100%)
  └─ BZ2_bzCompress (100%)
       └─ add_pair_to_block (100%, recursive 271700 calls)
            └─ BZ2_compressBlock (97.4%)
                 ├─ BZ2_blockSort (63.2%)
                 │    └─ mainSort (63.2%)
                 │         └─ fallbackSort (0.0%, 13450 calls)
                 ├─ sendMTFValues (7.9%)
                 │    ├─ BZ2_hbMakeCodeLengths (0.0%, 480 calls)
                 │    └─ BZ2_hbAssignCodes (0.0%, 120 calls)
                 └─ bsPutUInt32 (0.0%, 30 calls)
```

### Compression — Binary 1MB, bs=9, 20 iterations

```
BZ2_bzBuffToBuffCompress (100%)
  └─ BZ2_bzCompress (100%)
       └─ add_pair_to_block (100%)
            └─ BZ2_compressBlock (100%)
                 ├─ BZ2_blockSort (29.6%)
                 │    └─ mainSort (29.6%)
                 ├─ sendMTFValues (5.6%)
                 └─ [Huffman encoding inline: 60.8%]
```

### Compression — Zeros 1MB, bs=9, 100 iterations

```
BZ2_bzBuffToBuffCompress (100%)
  └─ add_pair_to_block (36%)
  └─ BZ2_blockSort (16%)
       └─ mainSort (8%) + SA-IS fallback (20%)
            └─ induceL_byte (4%)
  └─ BZ2_crc32_update (16%, 411300 calls)
```

### Decompression — Text 1MB, bs=9, 20 iterations

```
BZ2_bzBuffToBuffDecompress (93.5% of total, rest is initial compress)
  └─ BZ2_bzDecompress (93.5%)
       ├─ BZ2_decompress (71.7%, 60 calls = 3 blocks x 20 iters)
       │    └─ BZ2_hbCreateDecodeTables (0.0%, 240 calls)
       └─ BZ2_crc32_update (0.0%, 543380 calls)
```

### Decompression — Binary 1MB, bs=9, 50 iterations

```
BZ2_bzBuffToBuffDecompress (97%)
  └─ BZ2_bzDecompress (97%)
       ├─ BZ2_decompress (70.2%, 150 calls = 3 blocks x 50 iters)
       └─ BZ2_crc32_update (0.0%, 206900 calls)
```

### Decompression — Zeros 1MB, bs=9, 200 iterations

```
BZ2_bzBuffToBuffDecompress (100%)
  └─ BZ2_bzDecompress (100%)
       ├─ BZ2_crc32_update (87.5%, 826713 calls)
       └─ BZ2_decompress (12.5%, 400 calls)
```

## Optimization Opportunities

### Compression
1. **mainSort (63% of text compression)** — The BWT sort is by far the biggest bottleneck for text data. Further optimization of the sorting algorithm (better cache locality, SIMD comparisons, radix sort for initial bucket sort) would have the highest impact.
2. **BZ2_compressBlock / sendMTFValues (34% of text, 66% of binary)** — Huffman encoding is second. The 64-bit bitstream writer already helps, but the MTF transform and Huffman table construction could potentially be further optimized.
3. **add_pair_to_block CRC (36% of zeros)** — Already optimized with batch CRC. Further gains possible with SIMD CRC (CRC32C instruction if available, or PCLMULQDQ-based CRC).

### Decompression
1. **BZ2_decompress (70-72% of text/binary)** — The Huffman decode and inverse BWT pointer chase dominate. The 64-bit bitstream reader and BWT prefetch already help. Potential further optimizations: lookup-table-based Huffman decode, SIMD-accelerated inverse BWT.
2. **BZ2_bzDecompress / RLE output (22-27%)** — The outer RLE output loop. The bulk memcpy optimization helps for runs. Further: vectorized RLE expansion.
3. **BZ2_crc32_update (87.5% of zeros decompression)** — CRC is the bottleneck for highly compressible data. PCLMULQDQ-based CRC32 would give ~10x speedup on this path.

## Raw gprof Function Counts

| Function | Text Compress | Binary Compress | Zeros Compress | Text Decompress | Binary Decompress | Zeros Decompress |
|----------|--------------|----------------|----------------|-----------------|-------------------|------------------|
| mainSort | 20 calls | 40 calls | 100 calls | 2 calls | 2 calls | 1 call |
| BZ2_compressBlock | 20 calls | 40 calls | 100 calls | 2 calls | 2 calls | 1 call |
| sendMTFValues | 20 calls | 40 calls | 100 calls | 2 calls | 2 calls | — |
| BZ2_decompress | — | — | — | 60 calls | 150 calls | 400 calls |
| BZ2_crc32_update | — | — | 411,300 calls | 543,380 calls | 206,900 calls | 826,713 calls |
| fallbackSort | 13,450 calls | — | 53,100 calls | 1,345 calls | — | 531 calls |
| BZ2_hbCreateDecodeTables | — | — | — | 240 calls | 600 calls | 400 calls |
| BZ2_hbMakeCodeLengths | 480 calls | 960 calls | 800 calls | 48 calls | 48 calls | 8 calls |
