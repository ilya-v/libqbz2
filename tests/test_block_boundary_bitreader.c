/*
 * test_block_boundary_bitreader.c — Tests for the decompression bit-reader
 * behavior at internal block boundaries within multi-block streams.
 *
 * The 64-bit decompression bitstream absorbs 4 bytes at a time. When
 * processing multi-block streams, the bit-reader crosses internal block
 * boundaries (end-of-block RLE -> block header parse -> Huffman tables ->
 * MTF decode -> BWT). These tests verify correct state save/restore
 * across all block header parse points by feeding compressed data in
 * various chunk sizes.
 *
 * This is distinct from test_concat_readahead.c (which tests concatenated
 * independent streams) — here we test internal block boundaries within
 * a single bzip2 stream.
 */

#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

/* Reference library */
static int (*ref_bzBuffToBuffCompress)(char*, unsigned int*, char*, unsigned int, int, int, int);
static int (*ref_bzBuffToBuffDecompress)(char*, unsigned int*, char*, unsigned int, int, int);
static int (*ref_bzDecompressInit)(bz_stream*, int, int);
static int (*ref_bzDecompress)(bz_stream*);
static int (*ref_bzDecompressEnd)(bz_stream*);

static void *ref_lib = NULL;

static void load_ref(void) {
    if (ref_lib) return;
    ref_lib = dlopen("./reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_lib) ref_lib = dlopen("reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_lib) {
        fprintf(stderr, "WARNING: cannot load reference library: %s\n", dlerror());
        return;
    }
    ref_bzBuffToBuffCompress = dlsym(ref_lib, "BZ2_bzBuffToBuffCompress");
    ref_bzBuffToBuffDecompress = dlsym(ref_lib, "BZ2_bzBuffToBuffDecompress");
    ref_bzDecompressInit = dlsym(ref_lib, "BZ2_bzDecompressInit");
    ref_bzDecompress = dlsym(ref_lib, "BZ2_bzDecompress");
    ref_bzDecompressEnd = dlsym(ref_lib, "BZ2_bzDecompressEnd");
}

/* Compress data to malloc'd buffer */
static char *compress_buf(const char *src, unsigned int srcLen,
                          unsigned int *outLen, int blockSize) {
    unsigned int dlen = srcLen + srcLen/100 + 600;
    char *dst = malloc(dlen);
    if (!dst) return NULL;
    int rc = BZ2_bzBuffToBuffCompress(dst, &dlen, (char*)src, srcLen,
                                      blockSize, 0, 0);
    if (rc != BZ_OK) { free(dst); return NULL; }
    *outLen = dlen;
    return dst;
}

/* Create multi-block input: fill with data larger than one block at BS=1 */
static char *make_multiblock_data(unsigned int *dataLen, int nblocks, int bs) {
    /* Each block holds at most bs*100000 bytes; fill nblocks+0.5 blocks */
    unsigned int sz = (unsigned int)(bs * 100000) * nblocks + bs * 50000;
    char *data = malloc(sz);
    if (!data) return NULL;
    for (unsigned int i = 0; i < sz; i++)
        data[i] = (char)((i * 73 + 17 + nblocks) % 256);
    *dataLen = sz;
    return data;
}

/*
 * Decompress compressed data by feeding exactly `feedSize` bytes at a time.
 * Returns decompressed length, or -1 on error.
 */
static int decompress_with_feed(const char *comp, unsigned int compLen,
                                 char *out, unsigned int outLen,
                                 int feedSize, int small) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) return -1;

    strm.next_out = out;
    strm.avail_out = outLen;

    unsigned int pos = 0;
    while (pos < compLen) {
        unsigned int chunk = (unsigned int)feedSize;
        if (pos + chunk > compLen) chunk = compLen - pos;
        strm.next_in = (char*)comp + pos;
        strm.avail_in = chunk;

        while (strm.avail_in > 0) {
            ret = BZ2_bzDecompress(&strm);
            if (ret == BZ_STREAM_END) {
                unsigned int got = outLen - strm.avail_out;
                BZ2_bzDecompressEnd(&strm);
                return (int)got;
            }
            if (ret != BZ_OK) {
                BZ2_bzDecompressEnd(&strm);
                return -1;
            }
        }
        pos += chunk;
    }

    BZ2_bzDecompressEnd(&strm);
    return -1; /* shouldn't reach here */
}

/*
 * Same but with 1-byte output buffer, forcing many returns.
 */
static int decompress_1byte_output(const char *comp, unsigned int compLen,
                                    char *out, unsigned int outLen,
                                    int small) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) return -1;

    strm.next_in = (char*)comp;
    strm.avail_in = compLen;

    unsigned int written = 0;
    while (1) {
        strm.next_out = out + written;
        strm.avail_out = 1;

        ret = BZ2_bzDecompress(&strm);
        if (strm.avail_out == 0) written++;

        if (ret == BZ_STREAM_END) {
            BZ2_bzDecompressEnd(&strm);
            return (int)written;
        }
        if (ret != BZ_OK) {
            BZ2_bzDecompressEnd(&strm);
            return -1;
        }
        if (written >= outLen) {
            BZ2_bzDecompressEnd(&strm);
            return -1;
        }
    }
}


/* ======== TESTS ======== */

/*
 * Test 1: Multi-block (2 blocks) byte-at-a-time input feed.
 * Forces the bit-reader to save/restore state at every single
 * GET_BITS call point including the inter-block header parse.
 */
TEST(multiblock_byte_feed_bs1) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, dataLen, 1, 0);
    ASSERT_EQ(got, (int)dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    free(data); free(comp); free(out);
}

/*
 * Test 2: Same but small mode.
 */
TEST(multiblock_byte_feed_bs1_small) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, dataLen, 1, 1);
    ASSERT_EQ(got, (int)dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    free(data); free(comp); free(out);
}

/*
 * Test 3: Multi-block with 2-byte feed. This feeds exactly 2 bytes at
 * a time, so the GET_BITS macro never triggers the 4-byte bulk path
 * (which requires avail_in >= 4). All absorption is 1-byte-at-a-time.
 */
TEST(multiblock_2byte_feed) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, dataLen, 2, 0);
    ASSERT_EQ(got, (int)dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    free(data); free(comp); free(out);
}

/*
 * Test 4: Multi-block with 3-byte feed. Still below the 4-byte threshold.
 */
TEST(multiblock_3byte_feed) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, dataLen, 3, 0);
    ASSERT_EQ(got, (int)dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    free(data); free(comp); free(out);
}

/*
 * Test 5: Multi-block with exactly 4-byte feed. This is the critical
 * threshold: avail_in == 4 triggers the bulk absorption path. When the
 * 4 bytes span a block boundary in the bitstream, the absorption must
 * handle it correctly.
 */
TEST(multiblock_4byte_feed) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, dataLen, 4, 0);
    ASSERT_EQ(got, (int)dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    free(data); free(comp); free(out);
}

/*
 * Test 6: Multi-block with 5-byte feed (just above 4-byte threshold).
 */
TEST(multiblock_5byte_feed) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, dataLen, 5, 0);
    ASSERT_EQ(got, (int)dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    free(data); free(comp); free(out);
}

/*
 * Test 7: Multi-block with all feed sizes 1-8 must produce identical output.
 * Verifies that the bit-reader produces identical results regardless of
 * how input is chunked.
 */
TEST(multiblock_all_feed_sizes_identical) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    /* Baseline: all-at-once decompress */
    unsigned int baseLen = dataLen;
    char *base = malloc(dataLen);
    ASSERT(base);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(base, &baseLen, comp, compLen, 0, 0), BZ_OK);
    ASSERT_EQ(baseLen, dataLen);

    /* Compare against feed sizes 1-8 */
    for (int fs = 1; fs <= 8; fs++) {
        char *out = malloc(dataLen);
        ASSERT(out);
        int got = decompress_with_feed(comp, compLen, out, dataLen, fs, 0);
        ASSERT_EQ(got, (int)dataLen);
        ASSERT_MEM_EQ(out, base, dataLen);
        free(out);
    }

    free(data); free(comp); free(base);
}

/*
 * Test 8: 3 blocks with various data patterns, byte-at-a-time.
 */
TEST(multiblock_3blocks_patterns) {
    /* Create data with distinct patterns to test different Huffman tables */
    unsigned int sz = 350000; /* ~3.5 blocks at BS1 */
    char *data = malloc(sz);
    ASSERT(data);

    /* Block 1: low entropy (few unique bytes) */
    for (unsigned int i = 0; i < 100000; i++)
        data[i] = (char)('A' + (i % 3));
    /* Block 2: high entropy (many unique bytes) */
    for (unsigned int i = 100000; i < 200000; i++)
        data[i] = (char)((i * 73 + 17) % 256);
    /* Block 3: runs (triggers RLE) */
    for (unsigned int i = 200000; i < 300000; i++)
        data[i] = (char)((i / 1000) % 256);
    /* Partial block 4: alternating */
    for (unsigned int i = 300000; i < sz; i++)
        data[i] = (char)(i % 2 ? 0xFF : 0x00);

    unsigned int compLen;
    char *comp = compress_buf(data, sz, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(sz);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, sz, 1, 0);
    ASSERT_EQ(got, (int)sz);
    ASSERT_MEM_EQ(out, data, sz);

    free(data); free(comp); free(out);
}

/*
 * Test 9: Multi-block with 1-byte output buffer. Forces the decompressor
 * to return BZ_OK after every single output byte, including across block
 * boundaries. The state machine must correctly transition from block N's
 * output to block N+1's header parse.
 */
TEST(multiblock_1byte_output) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    int got = decompress_1byte_output(comp, compLen, out, dataLen, 0);
    ASSERT_EQ(got, (int)dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    free(data); free(comp); free(out);
}

/*
 * Test 10: Multi-block with 1-byte output buffer in small mode.
 */
TEST(multiblock_1byte_output_small) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    int got = decompress_1byte_output(comp, compLen, out, dataLen, 1);
    ASSERT_EQ(got, (int)dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    free(data); free(comp); free(out);
}

/*
 * Test 11: Simultaneous 1-byte input AND 1-byte output on multi-block.
 * This is the most hostile pattern: the decompressor must save/restore
 * state at every possible point in both the bitstream reader AND the
 * output generation.
 */
TEST(multiblock_1byte_input_and_output) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    unsigned int in_pos = 0;
    unsigned int out_pos = 0;
    int ret = BZ_OK;

    while (ret != BZ_STREAM_END) {
        /* Feed 1 input byte if needed and available */
        if (strm.avail_in == 0 && in_pos < compLen) {
            strm.next_in = (char*)comp + in_pos;
            strm.avail_in = 1;
            in_pos++;
        }
        /* Provide 1 output byte of space */
        strm.next_out = out + out_pos;
        strm.avail_out = 1;

        ret = BZ2_bzDecompress(&strm);
        if (strm.avail_out == 0) out_pos++;

        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    }

    ASSERT_EQ(out_pos, dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    BZ2_bzDecompressEnd(&strm);
    free(data); free(comp); free(out);
}

/*
 * Test 12: Differential comparison — multi-block byte-at-a-time vs reference.
 * Both libraries must produce identical output for each feed pattern.
 */
TEST(multiblock_byte_feed_vs_reference) {
    load_ref();
    if (!ref_lib) {
        fprintf(stderr, "  SKIP: reference library not available\n");
        return;
    }

    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    /* Test feed sizes 1, 2, 3, 4, 5, 8 */
    int feeds[] = {1, 2, 3, 4, 5, 8};
    int nfeeds = sizeof(feeds) / sizeof(feeds[0]);

    for (int fi = 0; fi < nfeeds; fi++) {
        int fs = feeds[fi];

        /* qbz2 */
        bz_stream qs;
        memset(&qs, 0, sizeof(qs));
        ASSERT_EQ(BZ2_bzDecompressInit(&qs, 0, 0), BZ_OK);
        char *qout = malloc(dataLen);
        ASSERT(qout);
        qs.next_out = qout;
        qs.avail_out = dataLen;
        unsigned int qpos = 0;
        int qret = BZ_OK;
        while (qpos < compLen && qret == BZ_OK) {
            unsigned int chunk = (unsigned int)fs;
            if (qpos + chunk > compLen) chunk = compLen - qpos;
            qs.next_in = (char*)comp + qpos;
            qs.avail_in = chunk;
            while (qs.avail_in > 0 && qret == BZ_OK) {
                qret = BZ2_bzDecompress(&qs);
            }
            qpos += chunk;
        }
        ASSERT_EQ(qret, BZ_STREAM_END);
        unsigned int qgot = dataLen - qs.avail_out;
        BZ2_bzDecompressEnd(&qs);

        /* reference */
        bz_stream rs;
        memset(&rs, 0, sizeof(rs));
        ASSERT_EQ(ref_bzDecompressInit(&rs, 0, 0), BZ_OK);
        char *rout = malloc(dataLen);
        ASSERT(rout);
        rs.next_out = rout;
        rs.avail_out = dataLen;
        unsigned int rpos = 0;
        int rret = BZ_OK;
        while (rpos < compLen && rret == BZ_OK) {
            unsigned int chunk = (unsigned int)fs;
            if (rpos + chunk > compLen) chunk = compLen - rpos;
            rs.next_in = (char*)comp + rpos;
            rs.avail_in = chunk;
            while (rs.avail_in > 0 && rret == BZ_OK) {
                rret = ref_bzDecompress(&rs);
            }
            rpos += chunk;
        }
        ASSERT_EQ(rret, BZ_STREAM_END);
        unsigned int rgot = dataLen - rs.avail_out;
        ref_bzDecompressEnd(&rs);

        /* Compare */
        ASSERT_EQ(qgot, rgot);
        ASSERT_MEM_EQ(qout, rout, qgot);

        free(qout); free(rout);
    }

    free(data); free(comp);
}

/*
 * Test 13: All block sizes (1-9) with multi-block input, byte-at-a-time.
 * Each block size produces different numbers of blocks for the same input.
 */
TEST(multiblock_all_blocksizes_byte_feed) {
    for (int bs = 1; bs <= 9; bs++) {
        unsigned int dataLen;
        char *data = make_multiblock_data(&dataLen, 2, bs);
        ASSERT(data);

        unsigned int compLen;
        char *comp = compress_buf(data, dataLen, &compLen, bs);
        ASSERT(comp);

        char *out = malloc(dataLen);
        ASSERT(out);

        int got = decompress_with_feed(comp, compLen, out, dataLen, 1, 0);
        ASSERT_EQ(got, (int)dataLen);
        ASSERT_MEM_EQ(out, data, dataLen);

        free(data); free(comp); free(out);
    }
}

/*
 * Test 14: Multi-block all-zeros. Zeros compress very efficiently via RLE
 * and produce a tiny compressed stream for many blocks. The block headers
 * are packed close together in the bitstream, maximizing the chance of
 * the 4-byte absorption spanning a block header.
 */
TEST(multiblock_zeros_byte_feed) {
    unsigned int sz = 250000; /* 2.5 blocks at BS1 */
    char *data = calloc(sz, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, sz, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(sz);
    ASSERT(out);

    /* The compressed size for all-zeros is very small, so block headers
       are extremely close together in the bitstream */
    int got = decompress_with_feed(comp, compLen, out, sz, 1, 0);
    ASSERT_EQ(got, (int)sz);

    char *expected = calloc(sz, 1);
    ASSERT_MEM_EQ(out, expected, sz);

    free(data); free(comp); free(out); free(expected);
}

/*
 * Test 15: Multi-block with highly repetitive single-byte data.
 * Different from zeros — uses 0xAA pattern.
 */
TEST(multiblock_repeat_byte_feed) {
    unsigned int sz = 250000;
    char *data = malloc(sz);
    ASSERT(data);
    memset(data, 0xAA, sz);

    unsigned int compLen;
    char *comp = compress_buf(data, sz, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(sz);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, sz, 1, 0);
    ASSERT_EQ(got, (int)sz);
    ASSERT_MEM_EQ(out, data, sz);

    free(data); free(comp); free(out);
}

/*
 * Test 16: Multi-block decompression interrupted and resumed at every
 * possible point. Feed 1 byte of input, then call BZ2_bzDecompress
 * repeatedly with 0 additional input (avail_in=0) to drain output,
 * then feed the next byte. This forces maximum state save/restores.
 */
TEST(multiblock_feed_drain_cycle) {
    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 2, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(dataLen);
    ASSERT(out);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    unsigned int out_pos = 0;
    int ret = BZ_OK;

    for (unsigned int i = 0; i < compLen && ret != BZ_STREAM_END; i++) {
        /* Feed exactly 1 byte */
        strm.next_in = (char*)comp + i;
        strm.avail_in = 1;

        /* Drain all available output */
        while (ret == BZ_OK) {
            strm.next_out = out + out_pos;
            strm.avail_out = (dataLen - out_pos < 4096) ?
                             (dataLen - out_pos) : 4096;
            unsigned int before = strm.avail_out;
            ret = BZ2_bzDecompress(&strm);
            out_pos += (before - strm.avail_out);

            if (strm.avail_in == 0 && ret == BZ_OK) break;
        }
    }

    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(out_pos, dataLen);
    ASSERT_MEM_EQ(out, data, dataLen);

    BZ2_bzDecompressEnd(&strm);
    free(data); free(comp); free(out);
}

/*
 * Test 17: Multi-block data at exact block boundary — input is exactly
 * N * 100000 bytes at BS1 (exactly N full blocks, no partial block).
 */
TEST(multiblock_exact_block_boundary) {
    for (int nblocks = 1; nblocks <= 3; nblocks++) {
        unsigned int sz = (unsigned int)(nblocks * 100000);
        char *data = malloc(sz);
        ASSERT(data);
        for (unsigned int i = 0; i < sz; i++)
            data[i] = (char)((i * 41 + nblocks) % 256);

        unsigned int compLen;
        char *comp = compress_buf(data, sz, &compLen, 1);
        ASSERT(comp);

        /* Byte-at-a-time decompress */
        char *out = malloc(sz);
        ASSERT(out);
        int got = decompress_with_feed(comp, compLen, out, sz, 1, 0);
        ASSERT_EQ(got, (int)sz);
        ASSERT_MEM_EQ(out, data, sz);

        /* Also verify with all-at-once */
        unsigned int dlen = sz;
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, compLen, 0, 0), BZ_OK);
        ASSERT_EQ(dlen, sz);
        ASSERT_MEM_EQ(out, data, sz);

        free(data); free(comp); free(out);
    }
}

/*
 * Test 18: Multi-block differential — total_in and total_out must
 * match reference library after multi-block decompression.
 */
TEST(multiblock_totals_vs_reference) {
    load_ref();
    if (!ref_lib) {
        fprintf(stderr, "  SKIP: reference library not available\n");
        return;
    }

    unsigned int dataLen;
    char *data = make_multiblock_data(&dataLen, 3, 1);
    ASSERT(data);

    unsigned int compLen;
    char *comp = compress_buf(data, dataLen, &compLen, 1);
    ASSERT(comp);

    /* qbz2 */
    bz_stream qs;
    memset(&qs, 0, sizeof(qs));
    ASSERT_EQ(BZ2_bzDecompressInit(&qs, 0, 0), BZ_OK);
    char *qout = malloc(dataLen);
    qs.next_in = comp; qs.avail_in = compLen;
    qs.next_out = qout; qs.avail_out = dataLen;
    ASSERT_EQ(BZ2_bzDecompress(&qs), BZ_STREAM_END);
    unsigned int q_tin = qs.total_in_lo32;
    unsigned int q_tout = qs.total_out_lo32;
    BZ2_bzDecompressEnd(&qs);

    /* reference */
    bz_stream rs;
    memset(&rs, 0, sizeof(rs));
    ASSERT_EQ(ref_bzDecompressInit(&rs, 0, 0), BZ_OK);
    char *rout = malloc(dataLen);
    rs.next_in = comp; rs.avail_in = compLen;
    rs.next_out = rout; rs.avail_out = dataLen;
    ASSERT_EQ(ref_bzDecompress(&rs), BZ_STREAM_END);
    unsigned int r_tin = rs.total_in_lo32;
    unsigned int r_tout = rs.total_out_lo32;
    ref_bzDecompressEnd(&rs);

    ASSERT_EQ(q_tin, r_tin);
    ASSERT_EQ(q_tout, r_tout);
    ASSERT_MEM_EQ(qout, rout, dataLen);

    free(data); free(comp); free(qout); free(rout);
}

/*
 * Test 19: Multi-block with data that transitions from low-entropy
 * to high-entropy at the block boundary. Different Huffman tables
 * are generated for each block.
 */
TEST(multiblock_entropy_transition) {
    unsigned int sz = 250000; /* 2.5 blocks at BS1 */
    char *data = malloc(sz);
    ASSERT(data);

    /* First block (100K): low entropy — only 4 unique bytes */
    for (unsigned int i = 0; i < 100000; i++)
        data[i] = "ABCD"[i % 4];
    /* Second block (100K): high entropy — all 256 byte values */
    for (unsigned int i = 100000; i < 200000; i++)
        data[i] = (char)((i * 131 + 7) % 256);
    /* Partial third block: alternating runs */
    for (unsigned int i = 200000; i < sz; i++)
        data[i] = (i / 100) % 2 ? 0x00 : 0xFF;

    unsigned int compLen;
    char *comp = compress_buf(data, sz, &compLen, 1);
    ASSERT(comp);

    /* Byte-at-a-time */
    char *out = malloc(sz);
    ASSERT(out);
    int got = decompress_with_feed(comp, compLen, out, sz, 1, 0);
    ASSERT_EQ(got, (int)sz);
    ASSERT_MEM_EQ(out, data, sz);

    /* 4-byte feed (bulk absorption threshold) */
    got = decompress_with_feed(comp, compLen, out, sz, 4, 0);
    ASSERT_EQ(got, (int)sz);
    ASSERT_MEM_EQ(out, data, sz);

    free(data); free(comp); free(out);
}

/*
 * Test 20: Multi-block with many blocks (5+ blocks at BS1) and
 * byte-at-a-time feed. Tests sustained state machine correctness
 * across many block transitions.
 */
TEST(multiblock_many_blocks_byte_feed) {
    unsigned int sz = 550000; /* ~5.5 blocks at BS1 */
    char *data = malloc(sz);
    ASSERT(data);
    for (unsigned int i = 0; i < sz; i++)
        data[i] = (char)((i * 37 + i / 1000) % 256);

    unsigned int compLen;
    char *comp = compress_buf(data, sz, &compLen, 1);
    ASSERT(comp);

    char *out = malloc(sz);
    ASSERT(out);

    int got = decompress_with_feed(comp, compLen, out, sz, 1, 0);
    ASSERT_EQ(got, (int)sz);
    ASSERT_MEM_EQ(out, data, sz);

    free(data); free(comp); free(out);
}


TEST_MAIN_BEGIN()
    RUN(multiblock_byte_feed_bs1);
    RUN(multiblock_byte_feed_bs1_small);
    RUN(multiblock_2byte_feed);
    RUN(multiblock_3byte_feed);
    RUN(multiblock_4byte_feed);
    RUN(multiblock_5byte_feed);
    RUN(multiblock_all_feed_sizes_identical);
    RUN(multiblock_3blocks_patterns);
    RUN(multiblock_1byte_output);
    RUN(multiblock_1byte_output_small);
    RUN(multiblock_1byte_input_and_output);
    RUN(multiblock_byte_feed_vs_reference);
    RUN(multiblock_all_blocksizes_byte_feed);
    RUN(multiblock_zeros_byte_feed);
    RUN(multiblock_repeat_byte_feed);
    RUN(multiblock_feed_drain_cycle);
    RUN(multiblock_exact_block_boundary);
    RUN(multiblock_totals_vs_reference);
    RUN(multiblock_entropy_transition);
    RUN(multiblock_many_blocks_byte_feed);
TEST_MAIN_END()
