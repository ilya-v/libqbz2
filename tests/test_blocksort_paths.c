/* test_blocksort_paths.c — targeted tests for all three blocksort code paths
 *
 * The BZ2_blockSort function has three paths:
 * 1. fallbackSort: nblock < 10000 (exponential radix sort)
 * 2. mainSort: nblock >= 10000, budget not exhausted (quicksort-based)
 * 3. sais_bwt: nblock >= 10000, budget < 0 after mainSort (SA-IS suffix array)
 *
 * SA-IS triggers when quicksort budget is exhausted on repetitive data.
 * Budget = nblock * ((wfact-1) / 3). With wfact=1, budget=0, so any mainSort
 * work exhausts it immediately, forcing SA-IS on all blocks >= 10000.
 *
 * Also tests intermediate block sizes (bs=2..8) at multi-block boundaries,
 * and concatenated bz2 streams via low-level streaming API.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: fill buffer with PRNG data */
static void fill_random(char *buf, unsigned int len, unsigned int seed) {
    for (unsigned int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
}

/* Helper: compress, decompress, verify bit-for-bit match */
static int roundtrip_verify(const char *data, unsigned int len,
                            int blockSize, int workFactor, int small) {
    unsigned int clen = len + len / 100 + 600;
    char *comp = malloc(clen);
    if (!comp) return -1;

    int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char *)data, len,
                                       blockSize, 0, workFactor);
    if (ret != BZ_OK) { free(comp); return -1; }

    unsigned int dlen = len;
    char *decomp = malloc(len > 0 ? len : 1);
    if (!decomp) { free(comp); return -1; }

    ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, small, 0);
    if (ret != BZ_OK) { free(comp); free(decomp); return -1; }
    if (dlen != len) { free(comp); free(decomp); return -1; }
    if (len > 0 && memcmp(decomp, data, len) != 0) {
        free(comp); free(decomp); return -1;
    }

    free(comp);
    free(decomp);
    return 0;
}

/* Helper: streaming compress + decompress with configurable chunk sizes */
static int streaming_roundtrip(const char *data, unsigned int len,
                               int blockSize, int workFactor,
                               int compChunk, int decompChunk) {
    /* Compress */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, blockSize, 0, workFactor);
    if (ret != BZ_OK) return -1;

    unsigned int clen = len + len / 100 + 600;
    char *comp = malloc(clen);
    if (!comp) { BZ2_bzCompressEnd(&strm); return -1; }

    unsigned int in_off = 0, out_off = 0;
    int action = BZ_RUN;
    while (1) {
        if (strm.avail_in == 0 && in_off < len) {
            strm.next_in = (char *)data + in_off;
            int chunk = (int)(len - in_off);
            if (chunk > compChunk) chunk = compChunk;
            strm.avail_in = chunk;
            in_off += chunk;
        }
        if (strm.avail_in == 0 && in_off >= len) action = BZ_FINISH;

        strm.next_out = comp + out_off;
        strm.avail_out = clen - out_off;

        ret = BZ2_bzCompress(&strm, action);
        out_off = (unsigned int)((char *)strm.next_out - comp);
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_RUN_OK && ret != BZ_FINISH_OK) {
            BZ2_bzCompressEnd(&strm);
            free(comp);
            return -1;
        }
    }
    BZ2_bzCompressEnd(&strm);
    clen = out_off;

    /* Decompress */
    memset(&strm, 0, sizeof(strm));
    ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK) { free(comp); return -1; }

    char *decomp = malloc(len > 0 ? len : 1);
    if (!decomp) { BZ2_bzDecompressEnd(&strm); free(comp); return -1; }

    in_off = 0; out_off = 0;
    while (1) {
        if (strm.avail_in == 0 && in_off < clen) {
            strm.next_in = comp + in_off;
            int chunk = (int)(clen - in_off);
            if (chunk > decompChunk) chunk = decompChunk;
            strm.avail_in = chunk;
            in_off += chunk;
        }
        strm.next_out = decomp + out_off;
        strm.avail_out = len - out_off;

        ret = BZ2_bzDecompress(&strm);
        out_off = (unsigned int)((char *)strm.next_out - decomp);
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_OK) {
            BZ2_bzDecompressEnd(&strm);
            free(comp); free(decomp);
            return -1;
        }
    }
    BZ2_bzDecompressEnd(&strm);

    if (out_off != len) { free(comp); free(decomp); return -1; }
    if (len > 0 && memcmp(decomp, data, len) != 0) {
        free(comp); free(decomp); return -1;
    }

    free(comp);
    free(decomp);
    return 0;
}


/* ================================================================
 * Section 1: SA-IS path tests (repetitive data, wfact=1, nblock >= 10000)
 *
 * With wfact=1: budget = nblock * ((1-1)/3) = 0
 * Any mainSort work will make budget < 0, triggering SA-IS.
 * ================================================================ */

/* Single-byte repeated data (most repetitive possible) */
TEST(sais_single_byte_repeated_bs1) {
    unsigned int sz = 110000;  /* > 1 block at bs=1 */
    char *d = calloc(sz, 1);
    ASSERT(d != NULL);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* Two-byte alternating (period-2 repetition) */
TEST(sais_alternating_2byte_bs1) {
    unsigned int sz = 110000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = (i & 1) ? 'B' : 'A';
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* 4-byte repeating pattern (period-4) */
TEST(sais_period4_pattern_bs1) {
    unsigned int sz = 110000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = "ACGT"[i % 4];
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* Long runs of same byte with occasional breaks */
TEST(sais_long_runs_bs1) {
    unsigned int sz = 110000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    memset(d, 'X', sz);
    /* Every 1000 bytes, put a different byte */
    for (unsigned int i = 0; i < sz; i += 1000) d[i] = 'Y';
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* SA-IS on exactly one block (nblock = 100000 at bs=1) */
TEST(sais_exact_one_block) {
    unsigned int sz = 100000;
    char *d = calloc(sz, 1);
    ASSERT(d != NULL);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* SA-IS with sawtooth pattern (low period, many repeated substrings) */
TEST(sais_sawtooth_bs1) {
    unsigned int sz = 110000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = (char)(i % 8);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* SA-IS at bs=9 with wfact=1 (single large block, 900KB data) */
TEST(sais_large_block_bs9) {
    unsigned int sz = 850000;  /* fits in one bs=9 block */
    char *d = malloc(sz);
    ASSERT(d != NULL);
    /* Repetitive but with some variation */
    for (unsigned int i = 0; i < sz; i++) d[i] = (char)(i % 16);
    ASSERT_EQ(roundtrip_verify(d, sz, 9, 1, 0), 0);
    free(d);
}

/* SA-IS multi-block at bs=1 with all-zeros (strongest repetition) */
TEST(sais_multiblock_zeros_bs1) {
    unsigned int sz = 300000;  /* 3 blocks at bs=1 */
    char *d = calloc(sz, 1);
    ASSERT(d != NULL);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* SA-IS with small-mode decompress */
TEST(sais_small_decompress) {
    unsigned int sz = 110000;
    char *d = calloc(sz, 1);
    ASSERT(d != NULL);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 1), 0);
    free(d);
}

/* SA-IS with streaming compress (64-byte chunks, forces many internal flushes) */
TEST(sais_streaming_small_chunks) {
    unsigned int sz = 110000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = (char)(i % 4);
    ASSERT_EQ(streaming_roundtrip(d, sz, 1, 1, 256, 256), 0);
    free(d);
}


/* ================================================================
 * Section 2: fallbackSort path tests (nblock < 10000)
 *
 * fallbackSort is the exponential radix sort used for small blocks.
 * At bs=1, block size is 100000, so nblock < 10000 means data < 10000.
 * ================================================================ */

/* Exactly at threshold: 9999 bytes (just under fallback cutoff) */
TEST(fallback_9999_random) {
    char d[9999];
    fill_random(d, 9999, 20001);
    ASSERT_EQ(roundtrip_verify(d, 9999, 1, 0, 0), 0);
}

/* Just over threshold: 10000 bytes (mainSort path) */
TEST(fallback_10000_random) {
    char d[10000];
    fill_random(d, 10000, 20002);
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
}

/* Repetitive data in fallback range */
TEST(fallback_5000_zeros) {
    char d[5000];
    memset(d, 0, 5000);
    ASSERT_EQ(roundtrip_verify(d, 5000, 1, 0, 0), 0);
}

TEST(fallback_5000_repeated_pattern) {
    char d[5000];
    for (int i = 0; i < 5000; i++) d[i] = (char)(i % 3);
    ASSERT_EQ(roundtrip_verify(d, 5000, 1, 0, 0), 0);
}

/* All 256 byte values in fallback range */
TEST(fallback_all_symbols) {
    char d[9000];
    for (int i = 0; i < 9000; i++) d[i] = (char)(i & 0xff);
    ASSERT_EQ(roundtrip_verify(d, 9000, 1, 0, 0), 0);
}

/* Single-symbol in fallback range */
TEST(fallback_single_symbol_8k) {
    char d[8000];
    memset(d, 42, 8000);
    ASSERT_EQ(roundtrip_verify(d, 8000, 1, 0, 0), 0);
}

/* Very small (well within fallback, boundary test for minimal sorting) */
TEST(fallback_100_bytes) {
    char d[100];
    fill_random(d, 100, 20006);
    ASSERT_EQ(roundtrip_verify(d, 100, 1, 0, 0), 0);
}

TEST(fallback_1_byte) {
    char d = 0x42;
    ASSERT_EQ(roundtrip_verify(&d, 1, 1, 0, 0), 0);
}

/* fallback with various work factors (should not affect fallback path) */
TEST(fallback_wf1) {
    char d[5000];
    fill_random(d, 5000, 20010);
    ASSERT_EQ(roundtrip_verify(d, 5000, 1, 1, 0), 0);
}

TEST(fallback_wf250) {
    char d[5000];
    fill_random(d, 5000, 20011);
    ASSERT_EQ(roundtrip_verify(d, 5000, 1, 250, 0), 0);
}


/* ================================================================
 * Section 3: mainSort path tests (nblock >= 10000, budget not exhausted)
 *
 * Random data with default workFactor should always stay in mainSort.
 * ================================================================ */

/* Random data just over fallback threshold */
TEST(mainsort_10001_random) {
    char *d = malloc(10001);
    ASSERT(d != NULL);
    fill_random(d, 10001, 30001);
    ASSERT_EQ(roundtrip_verify(d, 10001, 1, 0, 0), 0);
    free(d);
}

/* Random data, full block at bs=1 */
TEST(mainsort_100000_random) {
    char *d = malloc(100000);
    ASSERT(d != NULL);
    fill_random(d, 100000, 30002);
    ASSERT_EQ(roundtrip_verify(d, 100000, 1, 0, 0), 0);
    free(d);
}

/* Random data, multi-block at bs=1 */
TEST(mainsort_multiblock_random) {
    unsigned int sz = 250000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    fill_random(d, sz, 30003);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 0, 0), 0);
    free(d);
}

/* High work factor increases budget, ensuring mainSort does not fall through */
TEST(mainsort_high_wfact_repetitive) {
    /* Somewhat repetitive data, but wfact=250 gives huge budget */
    unsigned int sz = 50000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = (char)(i % 32);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 250, 0), 0);
    free(d);
}


/* ================================================================
 * Section 4: Intermediate block sizes (bs=2..8) at multi-block boundaries
 *
 * Block size N means blockSize100k * 100000 bytes per block.
 * Test at exactly N*100000, N*100000+1, and N*100000-1.
 * ================================================================ */

/* bs=2: boundary at 200000 */
TEST(bs2_boundary_minus1) {
    unsigned int sz = 199999;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40001);
    ASSERT_EQ(roundtrip_verify(d, sz, 2, 0, 0), 0);
    free(d);
}
TEST(bs2_boundary_exact) {
    unsigned int sz = 200000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40002);
    ASSERT_EQ(roundtrip_verify(d, sz, 2, 0, 0), 0);
    free(d);
}
TEST(bs2_boundary_plus1) {
    unsigned int sz = 200001;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40003);
    ASSERT_EQ(roundtrip_verify(d, sz, 2, 0, 0), 0);
    free(d);
}

/* bs=3: boundary at 300000 */
TEST(bs3_boundary_minus1) {
    unsigned int sz = 299999;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40011);
    ASSERT_EQ(roundtrip_verify(d, sz, 3, 0, 0), 0);
    free(d);
}
TEST(bs3_boundary_exact) {
    unsigned int sz = 300000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40012);
    ASSERT_EQ(roundtrip_verify(d, sz, 3, 0, 0), 0);
    free(d);
}
TEST(bs3_boundary_plus1) {
    unsigned int sz = 300001;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40013);
    ASSERT_EQ(roundtrip_verify(d, sz, 3, 0, 0), 0);
    free(d);
}

/* bs=4: boundary at 400000 */
TEST(bs4_boundary_minus1) {
    unsigned int sz = 399999;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40021);
    ASSERT_EQ(roundtrip_verify(d, sz, 4, 0, 0), 0);
    free(d);
}
TEST(bs4_boundary_exact) {
    unsigned int sz = 400000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40022);
    ASSERT_EQ(roundtrip_verify(d, sz, 4, 0, 0), 0);
    free(d);
}
TEST(bs4_boundary_plus1) {
    unsigned int sz = 400001;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40023);
    ASSERT_EQ(roundtrip_verify(d, sz, 4, 0, 0), 0);
    free(d);
}

/* bs=5: boundary at 500000 */
TEST(bs5_boundary_minus1) {
    unsigned int sz = 499999;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40031);
    ASSERT_EQ(roundtrip_verify(d, sz, 5, 0, 0), 0);
    free(d);
}
TEST(bs5_boundary_exact) {
    unsigned int sz = 500000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40032);
    ASSERT_EQ(roundtrip_verify(d, sz, 5, 0, 0), 0);
    free(d);
}
TEST(bs5_boundary_plus1) {
    unsigned int sz = 500001;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40033);
    ASSERT_EQ(roundtrip_verify(d, sz, 5, 0, 0), 0);
    free(d);
}

/* bs=6: boundary at 600000 */
TEST(bs6_boundary_minus1) {
    unsigned int sz = 599999;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40041);
    ASSERT_EQ(roundtrip_verify(d, sz, 6, 0, 0), 0);
    free(d);
}
TEST(bs6_boundary_exact) {
    unsigned int sz = 600000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40042);
    ASSERT_EQ(roundtrip_verify(d, sz, 6, 0, 0), 0);
    free(d);
}
TEST(bs6_boundary_plus1) {
    unsigned int sz = 600001;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40043);
    ASSERT_EQ(roundtrip_verify(d, sz, 6, 0, 0), 0);
    free(d);
}

/* bs=7: boundary at 700000 */
TEST(bs7_boundary_minus1) {
    unsigned int sz = 699999;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40051);
    ASSERT_EQ(roundtrip_verify(d, sz, 7, 0, 0), 0);
    free(d);
}
TEST(bs7_boundary_exact) {
    unsigned int sz = 700000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40052);
    ASSERT_EQ(roundtrip_verify(d, sz, 7, 0, 0), 0);
    free(d);
}
TEST(bs7_boundary_plus1) {
    unsigned int sz = 700001;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40053);
    ASSERT_EQ(roundtrip_verify(d, sz, 7, 0, 0), 0);
    free(d);
}

/* bs=8: boundary at 800000 */
TEST(bs8_boundary_minus1) {
    unsigned int sz = 799999;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40061);
    ASSERT_EQ(roundtrip_verify(d, sz, 8, 0, 0), 0);
    free(d);
}
TEST(bs8_boundary_exact) {
    unsigned int sz = 800000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40062);
    ASSERT_EQ(roundtrip_verify(d, sz, 8, 0, 0), 0);
    free(d);
}
TEST(bs8_boundary_plus1) {
    unsigned int sz = 800001;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 40063);
    ASSERT_EQ(roundtrip_verify(d, sz, 8, 0, 0), 0);
    free(d);
}


/* ================================================================
 * Section 5: SA-IS at intermediate block sizes
 *
 * Same trick: wfact=1 forces SA-IS on any block >= 10000.
 * Test with repetitive data at various block sizes.
 * ================================================================ */

TEST(sais_bs2_repetitive) {
    unsigned int sz = 210000;  /* > 1 block at bs=2 */
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = (char)(i % 4);
    ASSERT_EQ(roundtrip_verify(d, sz, 2, 1, 0), 0);
    free(d);
}

TEST(sais_bs3_repetitive) {
    unsigned int sz = 310000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = (char)(i % 6);
    ASSERT_EQ(roundtrip_verify(d, sz, 3, 1, 0), 0);
    free(d);
}

TEST(sais_bs5_repetitive) {
    unsigned int sz = 510000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = (char)(i % 8);
    ASSERT_EQ(roundtrip_verify(d, sz, 5, 1, 0), 0);
    free(d);
}

TEST(sais_bs7_repetitive) {
    unsigned int sz = 710000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < sz; i++) d[i] = (char)(i % 10);
    ASSERT_EQ(roundtrip_verify(d, sz, 7, 1, 0), 0);
    free(d);
}


/* ================================================================
 * Section 6: Concatenated bz2 streams via low-level streaming API
 *
 * The bzip2 format allows concatenation of independent streams.
 * Decompress must handle stream boundaries correctly.
 * ================================================================ */

/* Compress two independent chunks, concatenate, decompress all */
TEST(concat_two_streams_b2b) {
    char data1[5000], data2[8000];
    fill_random(data1, 5000, 50001);
    fill_random(data2, 8000, 50002);

    /* Compress each independently */
    unsigned int clen1 = 6000, clen2 = 9000;
    char *comp1 = malloc(clen1), *comp2 = malloc(clen2);
    ASSERT(comp1 && comp2);

    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp1, &clen1, data1, 5000, 1, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp2, &clen2, data2, 8000, 1, 0, 0), BZ_OK);

    /* Concatenate */
    char *concat = malloc(clen1 + clen2);
    ASSERT(concat);
    memcpy(concat, comp1, clen1);
    memcpy(concat + clen1, comp2, clen2);

    /* Decompress using streaming API - should handle two streams */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[13000];
    strm.next_in = concat;
    strm.avail_in = clen1 + clen2;
    strm.next_out = output;
    strm.avail_out = 13000;

    /* First stream */
    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int got1 = 13000 - strm.avail_out;
    ASSERT_EQ(got1, 5000u);
    ASSERT_MEM_EQ(output, data1, 5000);

    /* Reinit for second stream */
    BZ2_bzDecompressEnd(&strm);
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    /* Feed remaining data (second stream) */
    strm.next_in = concat + (clen1 + clen2 - strm.avail_in);
    /* Actually: after first decompress, avail_in tells how much is left */
    /* Let me re-do: track what was consumed */
    BZ2_bzDecompressEnd(&strm);

    /* Simpler approach: decompress first stream, note consumed bytes */
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    strm.next_in = concat;
    strm.avail_in = clen1 + clen2;
    strm.next_out = output;
    strm.avail_out = 13000;
    ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int consumed1 = (clen1 + clen2) - strm.avail_in;
    unsigned int remaining = strm.avail_in;
    char *remaining_ptr = strm.next_in;
    BZ2_bzDecompressEnd(&strm);

    /* Second stream from remaining */
    if (remaining > 0) {
        memset(&strm, 0, sizeof(strm));
        ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
        strm.next_in = remaining_ptr;
        strm.avail_in = remaining;
        char output2[8000];
        strm.next_out = output2;
        strm.avail_out = 8000;
        ret = BZ2_bzDecompress(&strm);
        ASSERT_EQ(ret, BZ_STREAM_END);
        unsigned int got2 = 8000 - strm.avail_out;
        ASSERT_EQ(got2, 8000u);
        ASSERT_MEM_EQ(output2, data2, 8000);
        BZ2_bzDecompressEnd(&strm);
    }

    free(comp1); free(comp2); free(concat);
}

/* Concatenated streams with different block sizes */
TEST(concat_different_blocksizes) {
    char data1[3000], data2[3000];
    fill_random(data1, 3000, 50011);
    fill_random(data2, 3000, 50012);

    unsigned int clen1 = 4000, clen2 = 4000;
    char comp1[4000], comp2[4000];

    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp1, &clen1, data1, 3000, 1, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp2, &clen2, data2, 3000, 9, 0, 0), BZ_OK);

    char *concat = malloc(clen1 + clen2);
    ASSERT(concat);
    memcpy(concat, comp1, clen1);
    memcpy(concat + clen1, comp2, clen2);

    /* Decompress first stream */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    strm.next_in = concat;
    strm.avail_in = clen1 + clen2;
    char out1[3000];
    strm.next_out = out1;
    strm.avail_out = 3000;
    ASSERT_EQ(BZ2_bzDecompress(&strm), BZ_STREAM_END);
    ASSERT_EQ(3000u - strm.avail_out, 3000u);
    ASSERT_MEM_EQ(out1, data1, 3000);
    char *rem = strm.next_in;
    unsigned int rem_len = strm.avail_in;
    BZ2_bzDecompressEnd(&strm);

    /* Decompress second stream */
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    strm.next_in = rem;
    strm.avail_in = rem_len;
    char out2[3000];
    strm.next_out = out2;
    strm.avail_out = 3000;
    ASSERT_EQ(BZ2_bzDecompress(&strm), BZ_STREAM_END);
    ASSERT_EQ(3000u - strm.avail_out, 3000u);
    ASSERT_MEM_EQ(out2, data2, 3000);
    BZ2_bzDecompressEnd(&strm);

    free(concat);
}

/* Three concatenated streams */
TEST(concat_three_streams) {
    char data1[2000], data2[3000], data3[4000];
    fill_random(data1, 2000, 50021);
    fill_random(data2, 3000, 50022);
    fill_random(data3, 4000, 50023);

    unsigned int clen1 = 3000, clen2 = 4000, clen3 = 5000;
    char comp1[3000], comp2[4000], comp3[5000];

    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp1, &clen1, data1, 2000, 1, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp2, &clen2, data2, 3000, 5, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp3, &clen3, data3, 4000, 9, 0, 0), BZ_OK);

    char *concat = malloc(clen1 + clen2 + clen3);
    ASSERT(concat);
    memcpy(concat, comp1, clen1);
    memcpy(concat + clen1, comp2, clen2);
    memcpy(concat + clen1 + clen2, comp3, clen3);

    unsigned int total_comp = clen1 + clen2 + clen3;
    char *ptr = concat;
    unsigned int rem = total_comp;

    /* Decompress all three */
    const char *datas[] = { data1, data2, data3 };
    unsigned int sizes[] = { 2000, 3000, 4000 };

    for (int s = 0; s < 3; s++) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
        strm.next_in = ptr;
        strm.avail_in = rem;
        char out[4000];
        strm.next_out = out;
        strm.avail_out = 4000;
        ASSERT_EQ(BZ2_bzDecompress(&strm), BZ_STREAM_END);
        ASSERT_EQ(4000u - strm.avail_out, sizes[s]);
        ASSERT_MEM_EQ(out, datas[s], sizes[s]);
        ptr = strm.next_in;
        rem = strm.avail_in;
        BZ2_bzDecompressEnd(&strm);
    }

    ASSERT_EQ(rem, 0u);  /* All consumed */
    free(concat);
}


/* ================================================================
 * Section 7: Cross-path transitions (mixed blocksort paths in one stream)
 *
 * Multi-block compression where different blocks trigger different paths.
 * E.g., one block is repetitive (SA-IS), next is random (mainSort).
 * ================================================================ */

/* First block repetitive (SA-IS with wfact=1), second block random */
TEST(mixed_sais_then_random) {
    unsigned int sz = 200000;  /* 2 blocks at bs=1 */
    char *d = malloc(sz);
    ASSERT(d != NULL);
    /* First 100K: repetitive -> SA-IS */
    for (unsigned int i = 0; i < 100000; i++) d[i] = (char)(i % 4);
    /* Second 100K: random -> mainSort */
    fill_random(d + 100000, 100000, 60001);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* First block random, second block repetitive */
TEST(mixed_random_then_sais) {
    unsigned int sz = 200000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    fill_random(d, 100000, 60002);
    for (unsigned int i = 100000; i < 200000; i++) d[i] = (char)(i % 4);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}

/* Alternating: rep, random, rep (3 blocks) */
TEST(mixed_alternating_3blocks) {
    unsigned int sz = 300000;
    char *d = malloc(sz);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < 100000; i++) d[i] = (char)(i % 2);
    fill_random(d + 100000, 100000, 60003);
    for (unsigned int i = 200000; i < 300000; i++) d[i] = (char)(i % 3);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 1, 0), 0);
    free(d);
}


/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    /* SA-IS path */
    RUN(sais_single_byte_repeated_bs1);
    RUN(sais_alternating_2byte_bs1);
    RUN(sais_period4_pattern_bs1);
    RUN(sais_long_runs_bs1);
    RUN(sais_exact_one_block);
    RUN(sais_sawtooth_bs1);
    RUN(sais_large_block_bs9);
    RUN(sais_multiblock_zeros_bs1);
    RUN(sais_small_decompress);
    RUN(sais_streaming_small_chunks);

    /* fallbackSort path */
    RUN(fallback_9999_random);
    RUN(fallback_10000_random);
    RUN(fallback_5000_zeros);
    RUN(fallback_5000_repeated_pattern);
    RUN(fallback_all_symbols);
    RUN(fallback_single_symbol_8k);
    RUN(fallback_100_bytes);
    RUN(fallback_1_byte);
    RUN(fallback_wf1);
    RUN(fallback_wf250);

    /* mainSort path */
    RUN(mainsort_10001_random);
    RUN(mainsort_100000_random);
    RUN(mainsort_multiblock_random);
    RUN(mainsort_high_wfact_repetitive);

    /* Intermediate block size boundaries */
    RUN(bs2_boundary_minus1); RUN(bs2_boundary_exact); RUN(bs2_boundary_plus1);
    RUN(bs3_boundary_minus1); RUN(bs3_boundary_exact); RUN(bs3_boundary_plus1);
    RUN(bs4_boundary_minus1); RUN(bs4_boundary_exact); RUN(bs4_boundary_plus1);
    RUN(bs5_boundary_minus1); RUN(bs5_boundary_exact); RUN(bs5_boundary_plus1);
    RUN(bs6_boundary_minus1); RUN(bs6_boundary_exact); RUN(bs6_boundary_plus1);
    RUN(bs7_boundary_minus1); RUN(bs7_boundary_exact); RUN(bs7_boundary_plus1);
    RUN(bs8_boundary_minus1); RUN(bs8_boundary_exact); RUN(bs8_boundary_plus1);

    /* SA-IS at intermediate block sizes */
    RUN(sais_bs2_repetitive);
    RUN(sais_bs3_repetitive);
    RUN(sais_bs5_repetitive);
    RUN(sais_bs7_repetitive);

    /* Concatenated streams */
    RUN(concat_two_streams_b2b);
    RUN(concat_different_blocksizes);
    RUN(concat_three_streams);

    /* Mixed blocksort paths */
    RUN(mixed_sais_then_random);
    RUN(mixed_random_then_sais);
    RUN(mixed_alternating_3blocks);
TEST_MAIN_END()
