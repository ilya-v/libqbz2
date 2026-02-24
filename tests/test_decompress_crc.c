/* test_decompress_crc.c — CRC correctness during streaming decompression
 *
 * Targets the decompression CRC computation paths in unRLE_obuf_to_output_FAST
 * and unRLE_obuf_to_output_SMALL. These tests verify that buffered/deferred CRC
 * computation produces correct block CRCs regardless of output buffer sizes,
 * RLE run lengths, and buffer boundary alignment.
 *
 * Key scenarios:
 *   - Tiny output buffers (1-16 bytes) force CRC flushes at every boundary
 *   - RLE runs that span multiple output buffer refills
 *   - Mixed single-byte outputs and multi-byte RLE runs
 *   - Multi-block streams where combined CRC must accumulate correctly
 *   - Both FAST and SMALL decompress modes
 *   - Differential comparison against reference libbz2
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

/* Reference library function pointers */
static void *ref_lib = NULL;
typedef int (*ref_b2b_compress_fn)(char*, unsigned int*, char*, unsigned int, int, int, int);
typedef int (*ref_b2b_decompress_fn)(char*, unsigned int*, char*, unsigned int, int, int);
typedef int (*ref_decompress_init_fn)(bz_stream*, int, int);
typedef int (*ref_decompress_fn)(bz_stream*);
typedef int (*ref_decompress_end_fn)(bz_stream*);

static ref_b2b_compress_fn ref_compress = NULL;
static ref_b2b_decompress_fn ref_b2b_decompress = NULL;
static ref_decompress_init_fn ref_dinit = NULL;
static ref_decompress_fn ref_drun = NULL;
static ref_decompress_end_fn ref_dend = NULL;

static int load_ref(void) {
    if (ref_lib) return 1;
    ref_lib = dlopen("./reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_lib) ref_lib = dlopen("reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_lib) {
        fprintf(stderr, "WARNING: cannot load reference library: %s\n", dlerror());
        return 0;
    }
    ref_compress = (ref_b2b_compress_fn)dlsym(ref_lib, "BZ2_bzBuffToBuffCompress");
    ref_b2b_decompress = (ref_b2b_decompress_fn)dlsym(ref_lib, "BZ2_bzBuffToBuffDecompress");
    ref_dinit = (ref_decompress_init_fn)dlsym(ref_lib, "BZ2_bzDecompressInit");
    ref_drun = (ref_decompress_fn)dlsym(ref_lib, "BZ2_bzDecompress");
    ref_dend = (ref_decompress_end_fn)dlsym(ref_lib, "BZ2_bzDecompressEnd");
    return (ref_compress && ref_b2b_decompress && ref_dinit && ref_drun && ref_dend) ? 1 : 0;
}

/*
 * Streaming decompress with a fixed output chunk size.
 * Returns total bytes decompressed, or -1 on error.
 * The error code is stored in *err_out.
 */
static int streaming_decompress_chunked(const char *comp, unsigned int clen,
                                         char *out, unsigned int outlen,
                                         int out_chunk, int small,
                                         int *err_out) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) { *err_out = ret; return -1; }

    strm.next_in = (char *)comp;
    strm.avail_in = clen;

    unsigned int total = 0;
    while (1) {
        unsigned int chunk = (unsigned int)out_chunk;
        if (chunk > outlen - total) chunk = outlen - total;
        if (chunk == 0) { *err_out = BZ_OUTBUFF_FULL; BZ2_bzDecompressEnd(&strm); return -1; }
        strm.next_out = out + total;
        strm.avail_out = chunk;
        ret = BZ2_bzDecompress(&strm);
        total = (unsigned int)(outlen - strm.avail_out - (unsigned int)(strm.next_out - (out + total)) + total);
        /* Simpler: total is next_out - out */
        total = (unsigned int)(strm.next_out - out);
        if (ret == BZ_STREAM_END) { *err_out = ret; BZ2_bzDecompressEnd(&strm); return (int)total; }
        if (ret != BZ_OK) { *err_out = ret; BZ2_bzDecompressEnd(&strm); return -1; }
    }
}

/*
 * Same but using the reference library.
 */
static int ref_streaming_decompress_chunked(const char *comp, unsigned int clen,
                                             char *out, unsigned int outlen,
                                             int out_chunk, int small,
                                             int *err_out) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = ref_dinit(&strm, 0, small);
    if (ret != BZ_OK) { *err_out = ret; return -1; }

    strm.next_in = (char *)comp;
    strm.avail_in = clen;

    unsigned int total = 0;
    while (1) {
        unsigned int chunk = (unsigned int)out_chunk;
        if (chunk > outlen - total) chunk = outlen - total;
        if (chunk == 0) { *err_out = BZ_OUTBUFF_FULL; ref_dend(&strm); return -1; }
        strm.next_out = out + total;
        strm.avail_out = chunk;
        ret = ref_drun(&strm);
        total = (unsigned int)(strm.next_out - out);
        if (ret == BZ_STREAM_END) { *err_out = ret; ref_dend(&strm); return (int)total; }
        if (ret != BZ_OK) { *err_out = ret; ref_dend(&strm); return -1; }
    }
}

/* Fill buffer with text pattern */
static void fill_text(char *buf, unsigned int len) {
    const char *phrase = "The quick brown fox jumps over the lazy dog. ";
    unsigned int plen = (unsigned int)strlen(phrase);
    for (unsigned int i = 0; i < len; i++)
        buf[i] = phrase[i % plen];
}

/* Fill buffer with pseudo-random data */
static void fill_random(char *buf, unsigned int len, unsigned int seed) {
    for (unsigned int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
}

/* Fill buffer with data that produces long RLE runs in BWT output.
 * Repeated patterns compress into RLE sequences in the BWT transform. */
static void fill_rle_heavy(char *buf, unsigned int len) {
    unsigned int pos = 0;
    unsigned char ch = 0;
    while (pos < len) {
        /* Generate runs of varying length: 1, 4, 5, 10, 50, 100, 255 bytes */
        unsigned int run_lengths[] = {1, 4, 5, 10, 50, 100, 255, 3, 7, 15, 63};
        for (unsigned int r = 0; r < sizeof(run_lengths)/sizeof(run_lengths[0]) && pos < len; r++) {
            unsigned int n = run_lengths[r];
            if (n > len - pos) n = len - pos;
            memset(buf + pos, ch, n);
            pos += n;
            ch = (unsigned char)((ch + 1) % 256);
        }
    }
}

/* Fill with all-same byte (maximal RLE) */
static void fill_constant(char *buf, unsigned int len, unsigned char val) {
    memset(buf, val, len);
}

/* Fill with binary data that has high entropy (many distinct bytes, few runs) */
static void fill_diverse(char *buf, unsigned int len) {
    for (unsigned int i = 0; i < len; i++)
        buf[i] = (char)(i * 137 + 73);
}


/* ================================================================
 * Section 1: Tiny output buffer streaming — exercises CRC flush at
 * every boundary in unRLE_obuf_to_output_FAST
 * ================================================================ */

/* Helper: compress data, then streaming decompress with given chunk size,
 * verify output matches original */
static void verify_chunked_roundtrip(const char *data, unsigned int len,
                                      int blockSize, int outChunk, int small) {
    unsigned int maxcomp = len + len / 100 + 600;
    char *comp = malloc(maxcomp);
    char *decomp = malloc(len + 1);
    ASSERT(comp != NULL);
    ASSERT(decomp != NULL);

    unsigned int clen = maxcomp;
    int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char *)data, len, blockSize, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    int err;
    int dlen = streaming_decompress_chunked(comp, clen, decomp, len, outChunk, small, &err);
    ASSERT_EQ(err, BZ_STREAM_END);
    ASSERT_EQ(dlen, (int)len);
    ASSERT_MEM_EQ(data, decomp, len);

    free(comp);
    free(decomp);
}

/* Text data with 1-byte output chunks */
TEST(crc_text_1byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 1, 0);
}

/* Text data with 2-byte output chunks */
TEST(crc_text_2byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 2, 0);
}

/* Text data with 3-byte output chunks (odd, not power of 2) */
TEST(crc_text_3byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 3, 0);
}

/* Text data with 4-byte output chunks */
TEST(crc_text_4byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 4, 0);
}

/* Text data with 7-byte output chunks (prime, stresses alignment) */
TEST(crc_text_7byte_out) {
    char data[5000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 7, 0);
}

/* Text data with 8-byte output chunks */
TEST(crc_text_8byte_out) {
    char data[5000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 8, 0);
}

/* Text data with 15-byte output chunks */
TEST(crc_text_15byte_out) {
    char data[5000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 15, 0);
}

/* Text data with 16-byte output chunks */
TEST(crc_text_16byte_out) {
    char data[5000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 16, 0);
}

/* Text data with 31-byte output chunks */
TEST(crc_text_31byte_out) {
    char data[10000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 31, 0);
}

/* Text data with 64-byte output chunks */
TEST(crc_text_64byte_out) {
    char data[10000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 64, 0);
}

/* Text data with 128-byte output chunks */
TEST(crc_text_128byte_out) {
    char data[10000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 128, 0);
}


/* ================================================================
 * Section 2: RLE-heavy data with tiny output buffers
 * RLE runs in the BWT output cross output buffer boundaries.
 * ================================================================ */

TEST(crc_rle_1byte_out) {
    char data[3000];
    fill_rle_heavy(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 1, 0);
}

TEST(crc_rle_3byte_out) {
    char data[3000];
    fill_rle_heavy(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 3, 0);
}

TEST(crc_rle_5byte_out) {
    char data[3000];
    fill_rle_heavy(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 5, 0);
}

TEST(crc_rle_7byte_out) {
    char data[5000];
    fill_rle_heavy(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 7, 0);
}

TEST(crc_rle_16byte_out) {
    char data[5000];
    fill_rle_heavy(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 16, 0);
}

TEST(crc_rle_63byte_out) {
    char data[5000];
    fill_rle_heavy(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 63, 0);
}


/* ================================================================
 * Section 3: Constant data (maximal RLE) with tiny output
 * All bytes are the same -- maximum RLE run lengths in BWT output.
 * ================================================================ */

TEST(crc_constant_1byte_out) {
    char data[2000];
    fill_constant(data, sizeof(data), 'A');
    verify_chunked_roundtrip(data, sizeof(data), 1, 1, 0);
}

TEST(crc_constant_3byte_out) {
    char data[2000];
    fill_constant(data, sizeof(data), 'A');
    verify_chunked_roundtrip(data, sizeof(data), 1, 3, 0);
}

TEST(crc_constant_4byte_out) {
    char data[5000];
    fill_constant(data, sizeof(data), 0xFF);
    verify_chunked_roundtrip(data, sizeof(data), 1, 4, 0);
}

TEST(crc_constant_7byte_out) {
    char data[5000];
    fill_constant(data, sizeof(data), 0x00);
    verify_chunked_roundtrip(data, sizeof(data), 1, 7, 0);
}

TEST(crc_constant_15byte_out) {
    char data[10000];
    fill_constant(data, sizeof(data), 0x42);
    verify_chunked_roundtrip(data, sizeof(data), 1, 15, 0);
}


/* ================================================================
 * Section 4: High-entropy (diverse) data with tiny output
 * Few RLE runs, mostly single-byte outputs.
 * ================================================================ */

TEST(crc_diverse_1byte_out) {
    char data[2000];
    fill_diverse(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 1, 0);
}

TEST(crc_diverse_3byte_out) {
    char data[2000];
    fill_diverse(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 3, 0);
}

TEST(crc_diverse_7byte_out) {
    char data[5000];
    fill_diverse(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 7, 0);
}


/* ================================================================
 * Section 5: SMALL mode — exercises unRLE_obuf_to_output_SMALL CRC
 * ================================================================ */

TEST(crc_small_text_1byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 1, 1);
}

TEST(crc_small_text_3byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 3, 1);
}

TEST(crc_small_text_7byte_out) {
    char data[5000];
    fill_text(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 7, 1);
}

TEST(crc_small_rle_1byte_out) {
    char data[3000];
    fill_rle_heavy(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 1, 1);
}

TEST(crc_small_rle_5byte_out) {
    char data[3000];
    fill_rle_heavy(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 5, 1);
}

TEST(crc_small_constant_3byte_out) {
    char data[2000];
    fill_constant(data, sizeof(data), 'Z');
    verify_chunked_roundtrip(data, sizeof(data), 1, 3, 1);
}

TEST(crc_small_diverse_7byte_out) {
    char data[2000];
    fill_diverse(data, sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 7, 1);
}


/* ================================================================
 * Section 6: Multi-block data — combined CRC across blocks
 * Block size 1 = 100KB blocks, so >100KB input produces multiple blocks.
 * ================================================================ */

TEST(crc_multiblock_1byte_out) {
    /* 150KB -- two blocks at bs=1 */
    unsigned int len = 150 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_text(data, len);
    verify_chunked_roundtrip(data, len, 1, 1, 0);
    free(data);
}

TEST(crc_multiblock_7byte_out) {
    unsigned int len = 150 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_text(data, len);
    verify_chunked_roundtrip(data, len, 1, 7, 0);
    free(data);
}

TEST(crc_multiblock_31byte_out) {
    unsigned int len = 150 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_random(data, len, 42);
    verify_chunked_roundtrip(data, len, 1, 31, 0);
    free(data);
}

TEST(crc_multiblock_rle_5byte_out) {
    unsigned int len = 150 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_rle_heavy(data, len);
    verify_chunked_roundtrip(data, len, 1, 5, 0);
    free(data);
}

TEST(crc_multiblock_small_3byte_out) {
    unsigned int len = 150 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_text(data, len);
    verify_chunked_roundtrip(data, len, 1, 3, 1);
    free(data);
}

/* Three blocks (300KB at bs=1) */
TEST(crc_3block_13byte_out) {
    unsigned int len = 300 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_random(data, len, 99);
    verify_chunked_roundtrip(data, len, 1, 13, 0);
    free(data);
}


/* ================================================================
 * Section 7: All block sizes with tiny output buffers
 * Verifies CRC correctness across all block sizes 1-9.
 * ================================================================ */

TEST(crc_all_blocksizes_5byte_out) {
    char data[5000];
    fill_text(data, sizeof(data));
    for (int bs = 1; bs <= 9; bs++) {
        verify_chunked_roundtrip(data, sizeof(data), bs, 5, 0);
    }
}

TEST(crc_all_blocksizes_11byte_out) {
    char data[5000];
    fill_random(data, sizeof(data), 777);
    for (int bs = 1; bs <= 9; bs++) {
        verify_chunked_roundtrip(data, sizeof(data), bs, 11, 0);
    }
}

TEST(crc_all_blocksizes_small_3byte_out) {
    char data[3000];
    fill_text(data, sizeof(data));
    for (int bs = 1; bs <= 9; bs++) {
        verify_chunked_roundtrip(data, sizeof(data), bs, 3, 1);
    }
}


/* ================================================================
 * Section 8: Differential comparison — verify byte-for-byte match
 * between qbz2 and reference library streaming decompression with
 * various output chunk sizes.
 * ================================================================ */

static void diff_chunked_decompress(const char *data, unsigned int len,
                                     int blockSize, int outChunk, int small) {
    if (!load_ref()) return;

    unsigned int maxcomp = len + len / 100 + 600;
    char *comp = malloc(maxcomp);
    char *q_out = malloc(len + 1);
    char *r_out = malloc(len + 1);
    ASSERT(comp != NULL);
    ASSERT(q_out != NULL);
    ASSERT(r_out != NULL);

    /* Compress with reference to get a standard bz2 stream */
    unsigned int clen = maxcomp;
    int ret = ref_compress(comp, &clen, (char *)data, len, blockSize, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    /* Decompress with qbz2 using chunked output */
    int q_err, r_err;
    int q_len = streaming_decompress_chunked(comp, clen, q_out, len, outChunk, small, &q_err);
    int r_len = ref_streaming_decompress_chunked(comp, clen, r_out, len, outChunk, small, &r_err);

    ASSERT_EQ(q_err, BZ_STREAM_END);
    ASSERT_EQ(r_err, BZ_STREAM_END);
    ASSERT_EQ(q_len, r_len);
    ASSERT_EQ(q_len, (int)len);
    ASSERT_MEM_EQ(q_out, r_out, len);

    /* Also verify both match original data */
    ASSERT_MEM_EQ(data, q_out, len);

    free(comp);
    free(q_out);
    free(r_out);
}

TEST(diff_crc_text_1byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    diff_chunked_decompress(data, sizeof(data), 1, 1, 0);
}

TEST(diff_crc_text_3byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    diff_chunked_decompress(data, sizeof(data), 1, 3, 0);
}

TEST(diff_crc_text_7byte_out) {
    char data[5000];
    fill_text(data, sizeof(data));
    diff_chunked_decompress(data, sizeof(data), 1, 7, 0);
}

TEST(diff_crc_rle_1byte_out) {
    char data[3000];
    fill_rle_heavy(data, sizeof(data));
    diff_chunked_decompress(data, sizeof(data), 1, 1, 0);
}

TEST(diff_crc_rle_5byte_out) {
    char data[3000];
    fill_rle_heavy(data, sizeof(data));
    diff_chunked_decompress(data, sizeof(data), 1, 5, 0);
}

TEST(diff_crc_constant_3byte_out) {
    char data[2000];
    fill_constant(data, sizeof(data), 'A');
    diff_chunked_decompress(data, sizeof(data), 1, 3, 0);
}

TEST(diff_crc_diverse_7byte_out) {
    char data[2000];
    fill_diverse(data, sizeof(data));
    diff_chunked_decompress(data, sizeof(data), 1, 7, 0);
}

TEST(diff_crc_multiblock_5byte_out) {
    unsigned int len = 150 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_text(data, len);
    diff_chunked_decompress(data, len, 1, 5, 0);
    free(data);
}

TEST(diff_crc_multiblock_13byte_out) {
    unsigned int len = 150 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_random(data, len, 42);
    diff_chunked_decompress(data, len, 1, 13, 0);
    free(data);
}

TEST(diff_crc_small_text_3byte_out) {
    char data[2000];
    fill_text(data, sizeof(data));
    diff_chunked_decompress(data, sizeof(data), 1, 3, 1);
}

TEST(diff_crc_small_rle_1byte_out) {
    char data[3000];
    fill_rle_heavy(data, sizeof(data));
    diff_chunked_decompress(data, sizeof(data), 1, 1, 1);
}


/* ================================================================
 * Section 9: Edge cases — very small inputs, single byte, empty
 * ================================================================ */

TEST(crc_single_byte_1byte_out) {
    char data[1] = {'X'};
    verify_chunked_roundtrip(data, 1, 1, 1, 0);
}

TEST(crc_two_bytes_1byte_out) {
    char data[2] = {'A', 'B'};
    verify_chunked_roundtrip(data, 2, 1, 1, 0);
}

TEST(crc_three_bytes_1byte_out) {
    char data[3] = {'A', 'A', 'A'};
    verify_chunked_roundtrip(data, 3, 1, 1, 0);
}

/* Four identical bytes -- triggers RLE encoding (run of exactly 4) */
TEST(crc_four_same_1byte_out) {
    char data[4] = {'Q', 'Q', 'Q', 'Q'};
    verify_chunked_roundtrip(data, 4, 1, 1, 0);
}

/* Five identical bytes -- triggers RLE with repeat count */
TEST(crc_five_same_1byte_out) {
    char data[5] = {'Z', 'Z', 'Z', 'Z', 'Z'};
    verify_chunked_roundtrip(data, 5, 1, 1, 0);
}

/* 256 identical bytes -- maximal single-value RLE */
TEST(crc_256_same_1byte_out) {
    char data[256];
    memset(data, 'W', sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 1, 0);
}

TEST(crc_256_same_3byte_out) {
    char data[256];
    memset(data, 'W', sizeof(data));
    verify_chunked_roundtrip(data, sizeof(data), 1, 3, 0);
}


/* ================================================================
 * Section 10: Fragmented input AND output — both input and output
 * are delivered in small chunks. This stresses the resumable state
 * machine and CRC computation simultaneously.
 * ================================================================ */

static int streaming_both_chunked(const char *comp, unsigned int clen,
                                   char *out, unsigned int outlen,
                                   int in_chunk, int out_chunk, int small) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) return -1;

    unsigned int in_pos = 0;
    unsigned int out_pos = 0;

    while (1) {
        /* Feed input in small chunks */
        unsigned int in_avail = (unsigned int)in_chunk;
        if (in_avail > clen - in_pos) in_avail = clen - in_pos;
        strm.next_in = (char *)comp + in_pos;
        strm.avail_in = in_avail;

        /* Provide output in small chunks */
        unsigned int out_avail = (unsigned int)out_chunk;
        if (out_avail > outlen - out_pos) out_avail = outlen - out_pos;
        strm.next_out = out + out_pos;
        strm.avail_out = out_avail;

        ret = BZ2_bzDecompress(&strm);

        in_pos = (unsigned int)(strm.next_in - (char *)comp);
        out_pos = (unsigned int)(strm.next_out - out);

        if (ret == BZ_STREAM_END) { BZ2_bzDecompressEnd(&strm); return (int)out_pos; }
        if (ret != BZ_OK) { BZ2_bzDecompressEnd(&strm); return -1; }
    }
}

TEST(crc_both_chunked_text_3in_5out) {
    char data[5000];
    fill_text(data, sizeof(data));

    unsigned int maxcomp = sizeof(data) + sizeof(data) / 100 + 600;
    char *comp = malloc(maxcomp);
    char *decomp = malloc(sizeof(data));
    ASSERT(comp != NULL);
    ASSERT(decomp != NULL);

    unsigned int clen = maxcomp;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, sizeof(data), 1, 0, 0), BZ_OK);

    int dlen = streaming_both_chunked(comp, clen, decomp, sizeof(data), 3, 5, 0);
    ASSERT_EQ(dlen, (int)sizeof(data));
    ASSERT_MEM_EQ(data, decomp, sizeof(data));

    free(comp);
    free(decomp);
}

TEST(crc_both_chunked_rle_1in_1out) {
    char data[2000];
    fill_rle_heavy(data, sizeof(data));

    unsigned int maxcomp = sizeof(data) + sizeof(data) / 100 + 600;
    char *comp = malloc(maxcomp);
    char *decomp = malloc(sizeof(data));
    ASSERT(comp != NULL);
    ASSERT(decomp != NULL);

    unsigned int clen = maxcomp;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, sizeof(data), 1, 0, 0), BZ_OK);

    int dlen = streaming_both_chunked(comp, clen, decomp, sizeof(data), 1, 1, 0);
    ASSERT_EQ(dlen, (int)sizeof(data));
    ASSERT_MEM_EQ(data, decomp, sizeof(data));

    free(comp);
    free(decomp);
}

TEST(crc_both_chunked_random_7in_11out) {
    char data[5000];
    fill_random(data, sizeof(data), 12345);

    unsigned int maxcomp = sizeof(data) + sizeof(data) / 100 + 600;
    char *comp = malloc(maxcomp);
    char *decomp = malloc(sizeof(data));
    ASSERT(comp != NULL);
    ASSERT(decomp != NULL);

    unsigned int clen = maxcomp;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, sizeof(data), 1, 0, 0), BZ_OK);

    int dlen = streaming_both_chunked(comp, clen, decomp, sizeof(data), 7, 11, 0);
    ASSERT_EQ(dlen, (int)sizeof(data));
    ASSERT_MEM_EQ(data, decomp, sizeof(data));

    free(comp);
    free(decomp);
}

TEST(crc_both_chunked_multiblock_5in_7out) {
    unsigned int len = 150 * 1024;
    char *data = malloc(len);
    ASSERT(data != NULL);
    fill_text(data, len);

    unsigned int maxcomp = len + len / 100 + 600;
    char *comp = malloc(maxcomp);
    char *decomp = malloc(len);
    ASSERT(comp != NULL);
    ASSERT(decomp != NULL);

    unsigned int clen = maxcomp;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)data, len, 1, 0, 0), BZ_OK);

    int dlen = streaming_both_chunked(comp, clen, decomp, len, 5, 7, 0);
    ASSERT_EQ(dlen, (int)len);
    ASSERT_MEM_EQ(data, decomp, len);

    free(data);
    free(comp);
    free(decomp);
}


/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    printf("=== Decompression CRC correctness tests ===\n");

    /* Section 1: Tiny output buffer - text */
    printf("\n--- Text data with tiny output buffers ---\n");
    RUN(crc_text_1byte_out);
    RUN(crc_text_2byte_out);
    RUN(crc_text_3byte_out);
    RUN(crc_text_4byte_out);
    RUN(crc_text_7byte_out);
    RUN(crc_text_8byte_out);
    RUN(crc_text_15byte_out);
    RUN(crc_text_16byte_out);
    RUN(crc_text_31byte_out);
    RUN(crc_text_64byte_out);
    RUN(crc_text_128byte_out);

    /* Section 2: RLE-heavy data with tiny output buffers */
    printf("\n--- RLE-heavy data with tiny output buffers ---\n");
    RUN(crc_rle_1byte_out);
    RUN(crc_rle_3byte_out);
    RUN(crc_rle_5byte_out);
    RUN(crc_rle_7byte_out);
    RUN(crc_rle_16byte_out);
    RUN(crc_rle_63byte_out);

    /* Section 3: Constant data (maximal RLE) */
    printf("\n--- Constant data (maximal RLE) with tiny output buffers ---\n");
    RUN(crc_constant_1byte_out);
    RUN(crc_constant_3byte_out);
    RUN(crc_constant_4byte_out);
    RUN(crc_constant_7byte_out);
    RUN(crc_constant_15byte_out);

    /* Section 4: High-entropy data */
    printf("\n--- High-entropy (diverse) data with tiny output buffers ---\n");
    RUN(crc_diverse_1byte_out);
    RUN(crc_diverse_3byte_out);
    RUN(crc_diverse_7byte_out);

    /* Section 5: SMALL mode */
    printf("\n--- SMALL mode with tiny output buffers ---\n");
    RUN(crc_small_text_1byte_out);
    RUN(crc_small_text_3byte_out);
    RUN(crc_small_text_7byte_out);
    RUN(crc_small_rle_1byte_out);
    RUN(crc_small_rle_5byte_out);
    RUN(crc_small_constant_3byte_out);
    RUN(crc_small_diverse_7byte_out);

    /* Section 6: Multi-block */
    printf("\n--- Multi-block with tiny output buffers ---\n");
    RUN(crc_multiblock_1byte_out);
    RUN(crc_multiblock_7byte_out);
    RUN(crc_multiblock_31byte_out);
    RUN(crc_multiblock_rle_5byte_out);
    RUN(crc_multiblock_small_3byte_out);
    RUN(crc_3block_13byte_out);

    /* Section 7: All block sizes */
    printf("\n--- All block sizes with tiny output buffers ---\n");
    RUN(crc_all_blocksizes_5byte_out);
    RUN(crc_all_blocksizes_11byte_out);
    RUN(crc_all_blocksizes_small_3byte_out);

    /* Section 8: Differential comparison */
    printf("\n--- Differential CRC comparison with reference ---\n");
    RUN(diff_crc_text_1byte_out);
    RUN(diff_crc_text_3byte_out);
    RUN(diff_crc_text_7byte_out);
    RUN(diff_crc_rle_1byte_out);
    RUN(diff_crc_rle_5byte_out);
    RUN(diff_crc_constant_3byte_out);
    RUN(diff_crc_diverse_7byte_out);
    RUN(diff_crc_multiblock_5byte_out);
    RUN(diff_crc_multiblock_13byte_out);
    RUN(diff_crc_small_text_3byte_out);
    RUN(diff_crc_small_rle_1byte_out);

    /* Section 9: Edge cases */
    printf("\n--- Edge cases ---\n");
    RUN(crc_single_byte_1byte_out);
    RUN(crc_two_bytes_1byte_out);
    RUN(crc_three_bytes_1byte_out);
    RUN(crc_four_same_1byte_out);
    RUN(crc_five_same_1byte_out);
    RUN(crc_256_same_1byte_out);
    RUN(crc_256_same_3byte_out);

    /* Section 10: Fragmented input AND output */
    printf("\n--- Fragmented input AND output ---\n");
    RUN(crc_both_chunked_text_3in_5out);
    RUN(crc_both_chunked_rle_1in_1out);
    RUN(crc_both_chunked_random_7in_11out);
    RUN(crc_both_chunked_multiblock_5in_7out);

TEST_MAIN_END()
