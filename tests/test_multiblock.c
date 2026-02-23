/* Multi-block CRC regression and streaming edge case tests for libqbz2
 *
 * Regression coverage for the dffe019 CRC bug: compression-side batch CRC
 * computed on raw input bytes, but at block boundaries, bytes in pending
 * RLE runs were CRC'd into the wrong block. These tests exercise multi-block
 * inputs with various data patterns, block sizes, chunked feeding, and
 * flush interleaving to ensure block-boundary CRC is always correct.
 *
 * Also covers streaming API edge cases: tiny output buffers, incremental
 * feed with flush, multi-stream decompression, and small-mode multi-block.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* PRNG for deterministic test data */
static unsigned int prng_state;
static void prng_seed(unsigned int s) { prng_state = s; }
static unsigned char prng_byte(void) {
    prng_state = prng_state * 1103515245 + 12345;
    return (unsigned char)(prng_state >> 16);
}

/* Helper: fill buffer with PRNG data */
static void fill_random(char *buf, int len, unsigned int seed) {
    prng_seed(seed);
    for (int i = 0; i < len; i++) buf[i] = (char)prng_byte();
}

/* Helper: streaming compress with configurable chunk size */
static int compress_chunked(const char *in, int inlen, char *out, int outlen,
                            int bs, int chunk_in, int chunk_out) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzCompressInit(&strm, bs, 0, 0) != BZ_OK) return -1;

    int in_off = 0, out_off = 0;

    /* Feed input in chunks */
    while (in_off < inlen) {
        int feed = (inlen - in_off < chunk_in) ? (inlen - in_off) : chunk_in;
        strm.next_in = (char *)in + in_off;
        strm.avail_in = feed;
        while (strm.avail_in > 0) {
            int avail = (outlen - out_off < chunk_out) ? (outlen - out_off) : chunk_out;
            if (avail <= 0) { BZ2_bzCompressEnd(&strm); return -1; }
            strm.next_out = out + out_off;
            strm.avail_out = avail;
            int ret = BZ2_bzCompress(&strm, BZ_RUN);
            if (ret != BZ_RUN_OK) { BZ2_bzCompressEnd(&strm); return -1; }
            out_off += avail - strm.avail_out;
        }
        in_off += feed;
    }

    /* Finish */
    strm.avail_in = 0;
    while (1) {
        int avail = (outlen - out_off < chunk_out) ? (outlen - out_off) : chunk_out;
        if (avail <= 0) { BZ2_bzCompressEnd(&strm); return -1; }
        strm.next_out = out + out_off;
        strm.avail_out = avail;
        int ret = BZ2_bzCompress(&strm, BZ_FINISH);
        out_off += avail - strm.avail_out;
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_FINISH_OK) { BZ2_bzCompressEnd(&strm); return -1; }
    }

    BZ2_bzCompressEnd(&strm);
    return out_off;
}

/* Helper: decompress with configurable chunk size */
static int decompress_chunked(const char *in, int inlen, char *out, int outlen,
                               int chunk_in, int chunk_out, int small) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzDecompressInit(&strm, 0, small) != BZ_OK) return -1;

    int in_off = 0, out_off = 0;
    while (1) {
        /* Refill input if empty */
        if (strm.avail_in == 0 && in_off < inlen) {
            int feed = (inlen - in_off < chunk_in) ? (inlen - in_off) : chunk_in;
            strm.next_in = (char *)in + in_off;
            strm.avail_in = feed;
            in_off += feed;
        }
        int avail = (outlen - out_off < chunk_out) ? (outlen - out_off) : chunk_out;
        if (avail <= 0) avail = 1; /* Always provide at least 1 byte output room */
        if (out_off + avail > outlen) { BZ2_bzDecompressEnd(&strm); return -1; }
        strm.next_out = out + out_off;
        strm.avail_out = avail;
        int ret = BZ2_bzDecompress(&strm);
        out_off += avail - strm.avail_out;
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_OK) { BZ2_bzDecompressEnd(&strm); return -1; }
        /* If no progress was made and no more input, we're stuck */
        if (strm.avail_in == 0 && in_off >= inlen && avail == (int)strm.avail_out) {
            BZ2_bzDecompressEnd(&strm);
            return -1;
        }
    }

    BZ2_bzDecompressEnd(&strm);
    return out_off;
}

/* Helper: simple compress and verify roundtrip */
static void roundtrip_verify(const char *data, int len, int bs, int small) {
    int comp_sz = len + len / 100 + 1000;
    char *comp = malloc(comp_sz);
    unsigned int clen = comp_sz;
    int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char *)data, len, bs, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    char *out = malloc(len + 1);
    unsigned int olen = len;
    ret = BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, small, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(olen, (unsigned int)len);
    ASSERT_MEM_EQ(out, data, len);

    free(comp);
    free(out);
}

/* ================================================================
 * CRC multi-block regression: dffe019 bug pattern
 * The bug was: at block boundaries, bytes in pending RLE runs were
 * CRC'd into the wrong block. These tests all force multi-block
 * compression with various data patterns.
 * ================================================================ */

/* 2.5 blocks with bs=1 (100KB blocks), pseudo-random data */
TEST(multiblock_random_2_5_blocks) {
    int len = 250000;
    char *data = malloc(len);
    fill_random(data, len, 42);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* 5 blocks with bs=1, ascending bytes */
TEST(multiblock_ascending_5_blocks) {
    int len = 500000;
    char *data = malloc(len);
    for (int i = 0; i < len; i++) data[i] = (char)(i & 0xff);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Exactly at block boundary — 100000 bytes with bs=1 */
TEST(multiblock_exact_boundary) {
    int len = 100000;
    char *data = malloc(len);
    fill_random(data, len, 99);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Just over block boundary — 100001 bytes */
TEST(multiblock_just_over_boundary) {
    int len = 100001;
    char *data = malloc(len);
    fill_random(data, len, 100);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Just under block boundary — 99999 bytes */
TEST(multiblock_just_under_boundary) {
    int len = 99999;
    char *data = malloc(len);
    fill_random(data, len, 101);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* RLE-heavy data at block boundary: long runs of same byte */
TEST(multiblock_rle_heavy) {
    int len = 300000;
    char *data = malloc(len);
    /* Alternating 255-byte runs of different values */
    for (int i = 0; i < len; i++) data[i] = (char)((i / 255) & 0xff);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Maximum RLE runs (255 same bytes) crossing block boundary */
TEST(multiblock_max_rle_at_boundary) {
    int len = 200000;
    char *data = malloc(len);
    /* Fill with 255-byte runs, targeting block boundary area */
    for (int i = 0; i < len; i++) data[i] = (char)((i / 255) % 2 ? 'A' : 'B');
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Single repeated byte — extreme RLE */
TEST(multiblock_single_byte_repeat) {
    int len = 400000;
    char *data = malloc(len);
    memset(data, 'X', len);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* All zeros — another extreme RLE case */
TEST(multiblock_all_zeros) {
    int len = 500000;
    char *data = calloc(len, 1);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* All 0xFF bytes */
TEST(multiblock_all_ff) {
    int len = 300000;
    char *data = malloc(len);
    memset(data, 0xFF, len);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Two-byte alternating pattern crossing boundaries */
TEST(multiblock_alternating_2byte) {
    int len = 250000;
    char *data = malloc(len);
    for (int i = 0; i < len; i++) data[i] = (char)(i & 1 ? 0xAA : 0x55);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Block size 2 — smaller blocks, more block boundaries */
TEST(multiblock_bs2_random) {
    int len = 500000; /* 2.5 blocks at bs=2 (200KB blocks) */
    char *data = malloc(len);
    fill_random(data, len, 200);
    roundtrip_verify(data, len, 2, 0);
    free(data);
}

/* Block size 9 — test with the largest block size, just over 1 block */
TEST(multiblock_bs9_large) {
    int len = 950000; /* ~1.05 blocks at bs=9 (900KB blocks) */
    char *data = malloc(len);
    fill_random(data, len, 900);
    roundtrip_verify(data, len, 9, 0);
    free(data);
}

/* All block sizes with same input */
TEST(multiblock_all_bs) {
    int len = 200000;
    char *data = malloc(len);
    fill_random(data, len, 777);
    for (int bs = 1; bs <= 9; bs++) {
        roundtrip_verify(data, len, bs, 0);
    }
    free(data);
}

/* Small mode decompression on multi-block data */
TEST(multiblock_small_decompress) {
    int len = 250000;
    char *data = malloc(len);
    fill_random(data, len, 555);
    roundtrip_verify(data, len, 1, 1); /* small=1 */
    free(data);
}

/* Multi-block with all block sizes in small mode */
TEST(multiblock_all_bs_small) {
    int len = 200000;
    char *data = malloc(len);
    fill_random(data, len, 888);
    for (int bs = 1; bs <= 3; bs++) { /* bs 1-3 keep it fast */
        roundtrip_verify(data, len, bs, 1);
    }
    free(data);
}

/* ================================================================
 * Chunked streaming multi-block: feed data in small increments
 * to exercise block-boundary transitions during streaming
 * ================================================================ */

TEST(multiblock_chunked_feed_small) {
    /* Feed in 64-byte chunks over a multi-block boundary */
    int len = 150000; /* 1.5 blocks at bs=1 */
    char *data = malloc(len);
    fill_random(data, len, 1111);

    int comp_sz = len + len / 100 + 1000;
    char *comp = malloc(comp_sz);
    /* Feed 64 bytes at a time, large output buffer */
    int clen = compress_chunked(data, len, comp, comp_sz, 1, 64, comp_sz);
    ASSERT(clen > 0);

    char *out = malloc(len);
    unsigned int olen = len;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(olen, (unsigned int)len);
    ASSERT_MEM_EQ(out, data, len);

    free(data); free(comp); free(out);
}

TEST(multiblock_chunked_feed_small_chunks) {
    int len = 200000;
    char *data = malloc(len);
    fill_random(data, len, 2222);

    int comp_sz = len + len / 100 + 1000;
    char *comp = malloc(comp_sz);
    /* Feed in 137-byte chunks (odd size to misalign with block boundaries) */
    int clen = compress_chunked(data, len, comp, comp_sz, 1, 137, comp_sz);
    ASSERT(clen > 0);

    char *out = malloc(len);
    unsigned int olen = len;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(olen, (unsigned int)len);
    ASSERT_MEM_EQ(out, data, len);

    free(data); free(comp); free(out);
}

TEST(multiblock_chunked_decompress_tiny_output) {
    /* Use smaller data for 64-byte output chunks to keep test fast */
    int len = 110000;
    char *data = malloc(len);
    fill_random(data, len, 3333);

    int comp_sz = len + len / 100 + 1000;
    char *comp = malloc(comp_sz);
    unsigned int clen = comp_sz;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, len, 1, 0, 0), BZ_OK);

    /* Decompress with 64-byte output chunks (still exercises multi-call path) */
    char *out = malloc(len);
    int dlen = decompress_chunked(comp, clen, out, len, clen, 64, 0);
    ASSERT_EQ(dlen, len);
    ASSERT_MEM_EQ(out, data, len);

    free(data); free(comp); free(out);
}

TEST(multiblock_chunked_decompress_small_mode) {
    int len = 150000;
    char *data = malloc(len);
    fill_random(data, len, 4444);

    int comp_sz = len + len / 100 + 1000;
    char *comp = malloc(comp_sz);
    unsigned int clen = comp_sz;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, len, 1, 0, 0), BZ_OK);

    /* Decompress with small=1 and 256-byte chunks */
    char *out = malloc(len);
    int dlen = decompress_chunked(comp, clen, out, len, 256, 256, 1);
    ASSERT_EQ(dlen, len);
    ASSERT_MEM_EQ(out, data, len);

    free(data); free(comp); free(out);
}

/* ================================================================
 * Flush interleaving with multi-block data
 * The dffe019 bug specifically affected block-boundary CRC.
 * Flush forces a block boundary, so flush + continue is a
 * targeted regression test.
 * ================================================================ */

TEST(multiblock_flush_at_boundary) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    int total_in = 40000;
    char *data = malloc(total_in);
    fill_random(data, total_in, 5555);

    int comp_sz = total_in * 2 + 1000;
    char *comp = malloc(comp_sz);
    strm.next_out = comp;
    strm.avail_out = comp_sz;

    /* Feed 10KB, flush, feed 10KB, flush, feed 20KB, finish */
    int offsets[] = {10000, 20000, 40000};
    for (int i = 0; i < 3; i++) {
        int start = (i == 0) ? 0 : offsets[i - 1];
        int end = offsets[i];
        strm.next_in = data + start;
        strm.avail_in = end - start;
        ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

        if (i < 2) {
            strm.avail_in = 0;
            int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
            while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
            ASSERT_EQ(ret, BZ_RUN_OK);
        }
    }

    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    int clen = comp_sz - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    char *out = malloc(total_in);
    unsigned int olen = total_in;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(olen, (unsigned int)total_in);
    ASSERT_MEM_EQ(out, data, total_in);

    free(data); free(comp); free(out);
}

TEST(multiblock_many_flushes) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    int total = 30000; /* 30KB with 3 flushes — keeps test fast */
    char *data = malloc(total);
    fill_random(data, total, 6666);

    int comp_sz = total * 2 + 1000;
    char *comp = malloc(comp_sz);
    strm.next_out = comp;
    strm.avail_out = comp_sz;

    /* Feed in 10KB chunks, flush after each */
    for (int off = 0; off < total; off += 10000) {
        int chunk = (total - off < 10000) ? (total - off) : 10000;
        strm.next_in = data + off;
        strm.avail_in = chunk;
        ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

        strm.avail_in = 0;
        int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    int clen = comp_sz - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    char *out = malloc(total);
    unsigned int olen = total;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(olen, (unsigned int)total);
    ASSERT_MEM_EQ(out, data, total);

    free(data); free(comp); free(out);
}

/* ================================================================
 * Work factor variations on multi-block data
 * ================================================================ */

TEST(multiblock_workfactor_extremes) {
    int len = 110000; /* Just over 1 block at bs=1, keeps each iteration fast */
    char *data = malloc(len);
    fill_random(data, len, 7777);

    int wfs[] = {1, 30, 100, 250};
    for (int i = 0; i < 4; i++) {
        int comp_sz = len + len / 100 + 1000;
        char *comp = malloc(comp_sz);
        char *out = malloc(len);

        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, wfs[i]), BZ_OK);
        strm.next_in = data; strm.avail_in = len;
        strm.next_out = comp; strm.avail_out = comp_sz;
        int ret = BZ2_bzCompress(&strm, BZ_FINISH);
        while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
        ASSERT_EQ(ret, BZ_STREAM_END);
        unsigned int actual_clen = comp_sz - strm.avail_out;
        BZ2_bzCompressEnd(&strm);

        unsigned int olen = len;
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, actual_clen, 0, 0), BZ_OK);
        ASSERT_EQ(olen, (unsigned int)len);
        ASSERT_MEM_EQ(out, data, len);

        free(comp); free(out);
    }
    free(data);
}

/* ================================================================
 * Data patterns that stress blocksort at block boundaries
 * ================================================================ */

/* Sawtooth pattern — repeating 0-255 sequence */
TEST(multiblock_sawtooth) {
    int len = 300000;
    char *data = malloc(len);
    for (int i = 0; i < len; i++) data[i] = (char)(i % 256);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Reverse sawtooth */
TEST(multiblock_reverse_sawtooth) {
    int len = 300000;
    char *data = malloc(len);
    for (int i = 0; i < len; i++) data[i] = (char)(255 - (i % 256));
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* English-like text with repeating sentences */
TEST(multiblock_text_like) {
    const char *phrases[] = {
        "The quick brown fox jumps over the lazy dog. ",
        "Pack my box with five dozen liquor jugs. ",
        "How vexingly quick daft zebras jump. ",
        "Sphinx of black quartz, judge my vow. ",
    };
    int len = 300000;
    char *data = malloc(len);
    int off = 0;
    while (off < len) {
        const char *p = phrases[(off / 47) % 4];
        int plen = strlen(p);
        int copy = (len - off < plen) ? (len - off) : plen;
        memcpy(data + off, p, copy);
        off += copy;
    }
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Binary with long identical runs at block boundary */
TEST(multiblock_long_runs_at_boundary) {
    int len = 200000;
    char *data = malloc(len);
    fill_random(data, len, 9999);
    /* Overwrite around the ~100KB boundary with a long run of 'Z' */
    memset(data + 99800, 'Z', 400);
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Sparse data — mostly zeros with occasional non-zero bytes */
TEST(multiblock_sparse) {
    int len = 400000;
    char *data = calloc(len, 1);
    prng_seed(12345);
    for (int i = 0; i < 1000; i++) {
        int pos = ((prng_byte() << 8) | prng_byte()) % len;
        data[pos] = (char)prng_byte();
    }
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* Binary data — all 256 byte values equally distributed */
TEST(multiblock_uniform_256) {
    int len = 256000;
    char *data = malloc(len);
    for (int i = 0; i < len; i++) data[i] = (char)(i % 256);
    /* Shuffle deterministically */
    prng_seed(54321);
    for (int i = len - 1; i > 0; i--) {
        int j = ((prng_byte() << 8) | prng_byte()) % (i + 1);
        char tmp = data[i]; data[i] = data[j]; data[j] = tmp;
    }
    roundtrip_verify(data, len, 1, 0);
    free(data);
}

/* ================================================================
 * Streaming decompress multi-block with tiny input feeding
 * ================================================================ */

TEST(multiblock_decompress_tiny_input_feed) {
    int len = 110000;
    char *data = malloc(len);
    fill_random(data, len, 11111);

    int comp_sz = len + len / 100 + 1000;
    char *comp = malloc(comp_sz);
    unsigned int clen = comp_sz;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, len, 1, 0, 0), BZ_OK);

    /* Decompress feeding 128 compressed bytes at a time */
    char *out = malloc(len);
    int dlen = decompress_chunked(comp, clen, out, len, 128, len, 0);
    ASSERT_EQ(dlen, len);
    ASSERT_MEM_EQ(out, data, len);

    free(data); free(comp); free(out);
}

/* ================================================================
 * Combined: chunked compress + chunked decompress
 * ================================================================ */

TEST(multiblock_both_chunked) {
    int len = 150000;
    char *data = malloc(len);
    fill_random(data, len, 22222);

    int comp_sz = len + len / 100 + 1000;
    char *comp = malloc(comp_sz);
    /* Compress with 1000-byte input chunks, 500-byte output chunks */
    int clen = compress_chunked(data, len, comp, comp_sz, 1, 1000, 500);
    ASSERT(clen > 0);

    /* Decompress with 256-byte input chunks, 256-byte output chunks */
    char *out = malloc(len);
    int dlen = decompress_chunked(comp, clen, out, len, 256, 256, 0);
    ASSERT_EQ(dlen, len);
    ASSERT_MEM_EQ(out, data, len);

    free(data); free(comp); free(out);
}

/* ================================================================
 * Multi-block with compress_chunked using tiny output buffer
 * Forces many copy_output_until_stop calls at block boundaries
 * ================================================================ */

TEST(multiblock_compress_tiny_output) {
    int len = 110000;
    char *data = malloc(len);
    fill_random(data, len, 33333);

    int comp_sz = len + len / 100 + 1000;
    char *comp = malloc(comp_sz);
    /* Compress with full input but 64-byte output chunks */
    int clen = compress_chunked(data, len, comp, comp_sz, 1, len, 64);
    ASSERT(clen > 0);

    char *out = malloc(len);
    unsigned int olen = len;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(olen, (unsigned int)len);
    ASSERT_MEM_EQ(out, data, len);

    free(data); free(comp); free(out);
}

/* ================================================================
 * Determinism: same input always produces same compressed output
 * ================================================================ */

TEST(multiblock_determinism) {
    int len = 200000;
    char *data = malloc(len);
    fill_random(data, len, 44444);

    int comp_sz = len + len / 100 + 1000;
    char *comp1 = malloc(comp_sz);
    char *comp2 = malloc(comp_sz);
    unsigned int clen1 = comp_sz, clen2 = comp_sz;

    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp1, &clen1, data, len, 1, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp2, &clen2, data, len, 1, 0, 0), BZ_OK);
    ASSERT_EQ(clen1, clen2);
    ASSERT_MEM_EQ(comp1, comp2, clen1);

    free(data); free(comp1); free(comp2);
}

/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    /* CRC multi-block regression (dffe019 pattern) */
    RUN(multiblock_random_2_5_blocks);
    RUN(multiblock_ascending_5_blocks);
    RUN(multiblock_exact_boundary);
    RUN(multiblock_just_over_boundary);
    RUN(multiblock_just_under_boundary);
    RUN(multiblock_rle_heavy);
    RUN(multiblock_max_rle_at_boundary);
    RUN(multiblock_single_byte_repeat);
    RUN(multiblock_all_zeros);
    RUN(multiblock_all_ff);
    RUN(multiblock_alternating_2byte);
    RUN(multiblock_bs2_random);
    RUN(multiblock_bs9_large);
    RUN(multiblock_all_bs);
    RUN(multiblock_small_decompress);
    RUN(multiblock_all_bs_small);

    /* Chunked streaming multi-block */
    RUN(multiblock_chunked_feed_small);
    RUN(multiblock_chunked_feed_small_chunks);
    RUN(multiblock_chunked_decompress_tiny_output);
    RUN(multiblock_chunked_decompress_small_mode);

    /* Flush interleaving */
    RUN(multiblock_flush_at_boundary);
    RUN(multiblock_many_flushes);

    /* Work factor variations */
    RUN(multiblock_workfactor_extremes);

    /* Data patterns at block boundaries */
    RUN(multiblock_sawtooth);
    RUN(multiblock_reverse_sawtooth);
    RUN(multiblock_text_like);
    RUN(multiblock_long_runs_at_boundary);
    RUN(multiblock_sparse);
    RUN(multiblock_uniform_256);

    /* Streaming decompress with tiny input */
    RUN(multiblock_decompress_tiny_input_feed);

    /* Combined chunked */
    RUN(multiblock_both_chunked);
    RUN(multiblock_compress_tiny_output);

    /* Determinism */
    RUN(multiblock_determinism);
TEST_MAIN_END()
