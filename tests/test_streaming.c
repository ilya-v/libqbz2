/* Streaming API tests for libqbz2
 * Thorough coverage of BZ2_bzCompress and BZ2_bzDecompress with various
 * chunk sizes, flush patterns, and multi-block scenarios.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: generate patterned data */
static void fill_pattern(char *buf, unsigned int len, unsigned int seed) {
    for (unsigned int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
}

/* Helper: streaming compress, returns compressed size or -1 on failure */
static int streaming_compress(const char *input, unsigned int inlen,
                              char *output, unsigned int outlen,
                              int blockSize, int workFactor) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzCompressInit(&strm, blockSize, 0, workFactor) != BZ_OK) return -1;

    strm.next_in = (char *)input;
    strm.avail_in = inlen;
    strm.next_out = output;
    strm.avail_out = outlen;

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        if (strm.avail_out == 0) { BZ2_bzCompressEnd(&strm); return -1; }
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
    }
    if (ret != BZ_STREAM_END) { BZ2_bzCompressEnd(&strm); return -1; }

    int result = (int)(outlen - strm.avail_out);
    BZ2_bzCompressEnd(&strm);
    return result;
}

/* Helper: streaming decompress, returns decompressed size or -1 on failure */
static int streaming_decompress(const char *input, unsigned int inlen,
                                char *output, unsigned int outlen, int small) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzDecompressInit(&strm, 0, small) != BZ_OK) return -1;

    strm.next_in = (char *)input;
    strm.avail_in = inlen;
    strm.next_out = output;
    strm.avail_out = outlen;

    int ret = BZ2_bzDecompress(&strm);
    while (ret == BZ_OK) {
        if (strm.avail_out == 0) { BZ2_bzDecompressEnd(&strm); return -1; }
        ret = BZ2_bzDecompress(&strm);
    }
    if (ret != BZ_STREAM_END) { BZ2_bzDecompressEnd(&strm); return -1; }

    int result = (int)(outlen - strm.avail_out);
    BZ2_bzDecompressEnd(&strm);
    return result;
}

/* ========== Streaming compress: various input chunk sizes ========== */

static int compress_chunked(const char *input, unsigned int inlen,
                            char *output, unsigned int outlen,
                            unsigned int chunk_size, int blockSize) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzCompressInit(&strm, blockSize, 0, 0) != BZ_OK) return -1;

    strm.next_out = output;
    strm.avail_out = outlen;

    unsigned int pos = 0;
    while (pos < inlen) {
        unsigned int n = inlen - pos;
        if (n > chunk_size) n = chunk_size;
        strm.next_in = (char *)input + pos;
        strm.avail_in = n;
        int ret = BZ2_bzCompress(&strm, BZ_RUN);
        if (ret != BZ_RUN_OK) { BZ2_bzCompressEnd(&strm); return -1; }
        pos += n;
    }

    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    if (ret != BZ_STREAM_END) { BZ2_bzCompressEnd(&strm); return -1; }

    int result = (int)(outlen - strm.avail_out);
    BZ2_bzCompressEnd(&strm);
    return result;
}

static int decompress_chunked(const char *input, unsigned int inlen,
                              char *output, unsigned int outlen,
                              unsigned int chunk_size) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzDecompressInit(&strm, 0, 0) != BZ_OK) return -1;

    strm.next_out = output;
    strm.avail_out = outlen;

    unsigned int pos = 0;
    int ret = BZ_OK;
    while (ret == BZ_OK && pos < inlen) {
        unsigned int n = inlen - pos;
        if (n > chunk_size) n = chunk_size;
        strm.next_in = (char *)input + pos;
        strm.avail_in = n;
        ret = BZ2_bzDecompress(&strm);
        pos += n - strm.avail_in;
    }
    if (ret != BZ_STREAM_END) { BZ2_bzDecompressEnd(&strm); return -1; }

    int result = (int)(outlen - strm.avail_out);
    BZ2_bzDecompressEnd(&strm);
    return result;
}

/* Compress with chunk=1, verify round-trip */
TEST(compress_chunk_1) {
    char data[5000];
    fill_pattern(data, 5000, 42);
    char comp[6000];
    int clen = compress_chunked(data, 5000, comp, 6000, 1, 1);
    ASSERT(clen > 0);
    char decomp[5000];
    unsigned int dlen = 5000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 5000u);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

TEST(compress_chunk_7) {
    char data[5000];
    fill_pattern(data, 5000, 43);
    char comp[6000];
    int clen = compress_chunked(data, 5000, comp, 6000, 7, 1);
    ASSERT(clen > 0);
    char decomp[5000];
    unsigned int dlen = 5000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 5000u);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

TEST(compress_chunk_100) {
    char data[5000];
    fill_pattern(data, 5000, 44);
    char comp[6000];
    int clen = compress_chunked(data, 5000, comp, 6000, 100, 1);
    ASSERT(clen > 0);
    char decomp[5000];
    unsigned int dlen = 5000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 5000u);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

TEST(compress_chunk_4096) {
    char data[5000];
    fill_pattern(data, 5000, 45);
    char comp[6000];
    int clen = compress_chunked(data, 5000, comp, 6000, 4096, 1);
    ASSERT(clen > 0);
    char decomp[5000];
    unsigned int dlen = 5000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 5000u);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

/* Decompress with chunk=1 */
TEST(decompress_chunk_1) {
    char data[5000];
    fill_pattern(data, 5000, 46);
    unsigned int clen = 6000;
    char comp[6000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);
    char decomp[5000];
    int dlen = decompress_chunked(comp, clen, decomp, 5000, 1);
    ASSERT_EQ(dlen, 5000);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

TEST(decompress_chunk_13) {
    char data[5000];
    fill_pattern(data, 5000, 47);
    unsigned int clen = 6000;
    char comp[6000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);
    char decomp[5000];
    int dlen = decompress_chunked(comp, clen, decomp, 5000, 13);
    ASSERT_EQ(dlen, 5000);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

TEST(decompress_chunk_256) {
    char data[5000];
    fill_pattern(data, 5000, 48);
    unsigned int clen = 6000;
    char comp[6000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);
    char decomp[5000];
    int dlen = decompress_chunked(comp, clen, decomp, 5000, 256);
    ASSERT_EQ(dlen, 5000);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

/* ========== Flush patterns ========== */

TEST(flush_empty_run) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[2000];
    strm.next_out = output;
    strm.avail_out = 2000;

    /* Flush with no data fed */
    strm.next_in = NULL;
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Now finish */
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    BZ2_bzCompressEnd(&strm);
}

TEST(flush_after_each_byte) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[10000];
    strm.next_out = output;
    strm.avail_out = 10000;

    /* Feed 50 bytes, flushing after each */
    char input[50];
    memset(input, 'Z', 50);
    for (int i = 0; i < 50; i++) {
        strm.next_in = &input[i];
        strm.avail_in = 1;
        int ret = BZ2_bzCompress(&strm, BZ_RUN);
        ASSERT_EQ(ret, BZ_RUN_OK);

        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* Finish */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 10000 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify round-trip */
    char decomp[100];
    unsigned int dlen = 100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 50u);
    for (int i = 0; i < 50; i++) ASSERT_EQ(decomp[i], 'Z');
}

TEST(multiple_flushes_no_data) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[5000];
    strm.next_out = output;
    strm.avail_out = 5000;

    /* Feed some data */
    char data[] = "some initial data";
    strm.next_in = data;
    strm.avail_in = strlen(data);
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Multiple consecutive flushes */
    for (int i = 0; i < 5; i++) {
        strm.avail_in = 0;
        int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* Finish */
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);
    BZ2_bzCompressEnd(&strm);
}

/* ========== Multi-block with various block sizes ========== */

TEST(multiblock_bs1_200k) {
    unsigned int sz = 200000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    fill_pattern(buf, sz, 100);
    unsigned int csz = sz + sz / 100 + 600;
    char *comp = malloc(csz);
    ASSERT(comp != NULL);
    int clen = streaming_compress(buf, sz, comp, csz, 1, 0);
    ASSERT(clen > 0);
    char *decomp = malloc(sz);
    ASSERT(decomp != NULL);
    int dlen = streaming_decompress(comp, clen, decomp, sz, 0);
    ASSERT_EQ(dlen, (int)sz);
    ASSERT_MEM_EQ(decomp, buf, sz);
    free(buf); free(comp); free(decomp);
}

TEST(multiblock_bs2_500k) {
    unsigned int sz = 500000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    fill_pattern(buf, sz, 200);
    unsigned int csz = sz + sz / 100 + 600;
    char *comp = malloc(csz);
    ASSERT(comp != NULL);
    int clen = streaming_compress(buf, sz, comp, csz, 2, 0);
    ASSERT(clen > 0);
    char *decomp = malloc(sz);
    ASSERT(decomp != NULL);
    int dlen = streaming_decompress(comp, clen, decomp, sz, 0);
    ASSERT_EQ(dlen, (int)sz);
    ASSERT_MEM_EQ(decomp, buf, sz);
    free(buf); free(comp); free(decomp);
}

TEST(multiblock_bs9_1M) {
    unsigned int sz = 1000000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    fill_pattern(buf, sz, 300);
    unsigned int csz = sz + sz / 100 + 600;
    char *comp = malloc(csz);
    ASSERT(comp != NULL);
    int clen = streaming_compress(buf, sz, comp, csz, 9, 0);
    ASSERT(clen > 0);
    char *decomp = malloc(sz);
    ASSERT(decomp != NULL);
    int dlen = streaming_decompress(comp, clen, decomp, sz, 0);
    ASSERT_EQ(dlen, (int)sz);
    ASSERT_MEM_EQ(decomp, buf, sz);
    free(buf); free(comp); free(decomp);
}

/* ========== Small mode decompression ========== */

TEST(small_decompress_bs1) {
    char data[5000];
    fill_pattern(data, 5000, 51);
    unsigned int clen = 6000;
    char comp[6000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);
    char decomp[5000];
    int dlen = streaming_decompress(comp, clen, decomp, 5000, 1);
    ASSERT_EQ(dlen, 5000);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

TEST(small_decompress_bs9) {
    char data[5000];
    fill_pattern(data, 5000, 52);
    unsigned int clen = 6000;
    char comp[6000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 9, 0, 0), BZ_OK);
    char decomp[5000];
    int dlen = streaming_decompress(comp, clen, decomp, 5000, 1);
    ASSERT_EQ(dlen, 5000);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

TEST(small_decompress_multiblock) {
    unsigned int sz = 200000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    fill_pattern(buf, sz, 53);
    unsigned int csz = sz + sz / 100 + 600;
    char *comp = malloc(csz);
    ASSERT(comp != NULL);
    int clen = streaming_compress(buf, sz, comp, csz, 1, 0);
    ASSERT(clen > 0);
    char *decomp = malloc(sz);
    ASSERT(decomp != NULL);
    int dlen = streaming_decompress(comp, clen, decomp, sz, 1);
    ASSERT_EQ(dlen, (int)sz);
    ASSERT_MEM_EQ(decomp, buf, sz);
    free(buf); free(comp); free(decomp);
}

/* ========== Output buffer pressure ========== */

TEST(compress_tiny_output_buffer) {
    /* Compress with a very small output buffer, requiring multiple FINISH calls */
    char data[5000];
    fill_pattern(data, 5000, 60);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    strm.next_in = data;
    strm.avail_in = 5000;

    char output[10000];
    unsigned int total = 0;

    int ret = BZ_FINISH_OK;
    while (ret != BZ_STREAM_END) {
        strm.next_out = output + total;
        strm.avail_out = 10; /* only 10 bytes at a time */
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        total += 10 - strm.avail_out;
        ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);
    }
    BZ2_bzCompressEnd(&strm);

    /* Verify */
    char decomp[5000];
    unsigned int dlen = 5000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, total, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 5000u);
    ASSERT_MEM_EQ(decomp, data, 5000);
}

TEST(decompress_tiny_output_buffer) {
    char data[5000];
    fill_pattern(data, 5000, 61);
    unsigned int clen = 6000;
    char comp[6000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    strm.next_in = comp;
    strm.avail_in = clen;

    char output[5000];
    unsigned int total = 0;

    int ret = BZ_OK;
    while (ret == BZ_OK) {
        strm.next_out = output + total;
        strm.avail_out = 10;
        ret = BZ2_bzDecompress(&strm);
        total += 10 - strm.avail_out;
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(total, 5000u);
    ASSERT_MEM_EQ(output, data, 5000);
    BZ2_bzDecompressEnd(&strm);
}

/* ========== Compress with all block sizes, verify with BuffToBuffDecompress ========== */

TEST(all_bs_roundtrip_5k) {
    char data[5000];
    fill_pattern(data, 5000, 70);
    for (int bs = 1; bs <= 9; bs++) {
        char comp[6000];
        int clen = streaming_compress(data, 5000, comp, 6000, bs, 0);
        ASSERT(clen > 0);
        char decomp[5000];
        unsigned int dlen = 5000;
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
        ASSERT_EQ(dlen, 5000u);
        ASSERT_MEM_EQ(decomp, data, 5000);
    }
}

/* ========== All block sizes x work factors ========== */

TEST(bs_wf_combinations) {
    char data[2000];
    fill_pattern(data, 2000, 80);
    int wfs[] = {0, 1, 30, 100, 250};
    for (int bs = 1; bs <= 9; bs++) {
        for (int w = 0; w < 5; w++) {
            char comp[3000];
            int clen = streaming_compress(data, 2000, comp, 3000, bs, wfs[w]);
            ASSERT(clen > 0);
            char decomp[2000];
            unsigned int dlen = 2000;
            ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
            ASSERT_EQ(dlen, 2000u);
            ASSERT_MEM_EQ(decomp, data, 2000);
        }
    }
}

/* ========== Data types that stress different paths ========== */

TEST(compress_all_same_byte) {
    char data[50000];
    memset(data, 'Q', 50000);
    char comp[51000];
    int clen = streaming_compress(data, 50000, comp, 51000, 1, 0);
    ASSERT(clen > 0);
    /* Highly repetitive data should compress very well */
    ASSERT(clen < 1000);
    char decomp[50000];
    unsigned int dlen = 50000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 50000u);
    ASSERT_MEM_EQ(decomp, data, 50000);
}

TEST(compress_random_data_50k) {
    char *data = malloc(50000);
    ASSERT(data != NULL);
    fill_pattern(data, 50000, 90);
    unsigned int csz = 60000;
    char *comp = malloc(csz);
    ASSERT(comp != NULL);
    int clen = streaming_compress(data, 50000, comp, csz, 5, 0);
    ASSERT(clen > 0);
    char *decomp = malloc(50000);
    ASSERT(decomp != NULL);
    unsigned int dlen = 50000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 50000u);
    ASSERT_MEM_EQ(decomp, data, 50000);
    free(data); free(comp); free(decomp);
}

TEST(compress_two_byte_alphabet) {
    /* Data using only bytes 0x00 and 0xFF */
    char data[10000];
    for (int i = 0; i < 10000; i++) data[i] = (i * 7 + 3) % 2 ? '\xff' : '\x00';
    char comp[11000];
    int clen = streaming_compress(data, 10000, comp, 11000, 1, 0);
    ASSERT(clen > 0);
    char decomp[10000];
    unsigned int dlen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 10000u);
    ASSERT_MEM_EQ(decomp, data, 10000);
}

TEST(compress_run_of_runs) {
    /* Runs of different lengths of different bytes */
    char data[10000];
    int pos = 0;
    for (int len = 1; len <= 100 && pos + len <= 10000; len++) {
        memset(data + pos, (char)len, len);
        pos += len;
    }
    if (pos < 10000) memset(data + pos, 0, 10000 - pos);

    char comp[11000];
    int clen = streaming_compress(data, 10000, comp, 11000, 1, 0);
    ASSERT(clen > 0);
    char decomp[10000];
    unsigned int dlen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 10000u);
    ASSERT_MEM_EQ(decomp, data, 10000);
}

/* ========== OOM injection via custom allocator ========== */

static int oom_fail_at = -1;
static int oom_alloc_count = 0;
static int oom_free_count = 0;

static void *oom_alloc(void *opaque, int items, int size) {
    (void)opaque;
    oom_alloc_count++;
    if (oom_fail_at >= 0 && oom_alloc_count > oom_fail_at) return NULL;
    return malloc((size_t)items * (size_t)size);
}

static void oom_free(void *opaque, void *ptr) {
    (void)opaque;
    if (ptr) oom_free_count++;
    free(ptr);
}

TEST(oom_compress_init_fail_1st) {
    oom_fail_at = 0;
    oom_alloc_count = 0;
    oom_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = oom_alloc;
    strm.bzfree = oom_free;

    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
    /* All allocations that succeeded must have been freed */
    ASSERT_EQ(oom_free_count, oom_fail_at);
}

TEST(oom_compress_init_fail_2nd) {
    oom_fail_at = 1;
    oom_alloc_count = 0;
    oom_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = oom_alloc;
    strm.bzfree = oom_free;

    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
    ASSERT_EQ(oom_free_count, oom_fail_at);
}

TEST(oom_compress_init_fail_3rd) {
    oom_fail_at = 2;
    oom_alloc_count = 0;
    oom_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = oom_alloc;
    strm.bzfree = oom_free;

    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
    ASSERT_EQ(oom_free_count, oom_fail_at);
}

TEST(oom_compress_init_fail_4th) {
    oom_fail_at = 3;
    oom_alloc_count = 0;
    oom_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = oom_alloc;
    strm.bzfree = oom_free;

    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    /* May succeed if only 3 allocations needed, or fail */
    if (ret == BZ_MEM_ERROR) {
        ASSERT_EQ(oom_free_count, oom_fail_at);
    } else {
        ASSERT_EQ(ret, BZ_OK);
        BZ2_bzCompressEnd(&strm);
    }
}

TEST(oom_decompress_init_fail_1st) {
    oom_fail_at = 0;
    oom_alloc_count = 0;
    oom_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = oom_alloc;
    strm.bzfree = oom_free;

    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
    ASSERT_EQ(oom_free_count, oom_fail_at);
}

TEST(oom_decompress_init_fail_2nd) {
    oom_fail_at = 1;
    oom_alloc_count = 0;
    oom_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = oom_alloc;
    strm.bzfree = oom_free;

    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    /* DecompressInit may only need 1 allocation, so failing at #2 may succeed */
    if (ret == BZ_MEM_ERROR) {
        ASSERT_EQ(oom_free_count, oom_fail_at);
    } else {
        ASSERT_EQ(ret, BZ_OK);
        BZ2_bzDecompressEnd(&strm);
    }
}

/* ========== Test runner ========== */

TEST_MAIN_BEGIN()
    /* Chunked compression */
    RUN(compress_chunk_1);
    RUN(compress_chunk_7);
    RUN(compress_chunk_100);
    RUN(compress_chunk_4096);

    /* Chunked decompression */
    RUN(decompress_chunk_1);
    RUN(decompress_chunk_13);
    RUN(decompress_chunk_256);

    /* Flush patterns */
    RUN(flush_empty_run);
    RUN(flush_after_each_byte);
    RUN(multiple_flushes_no_data);

    /* Multi-block */
    RUN(multiblock_bs1_200k);
    RUN(multiblock_bs2_500k);
    RUN(multiblock_bs9_1M);

    /* Small mode decompression */
    RUN(small_decompress_bs1);
    RUN(small_decompress_bs9);
    RUN(small_decompress_multiblock);

    /* Output buffer pressure */
    RUN(compress_tiny_output_buffer);
    RUN(decompress_tiny_output_buffer);

    /* All block sizes */
    RUN(all_bs_roundtrip_5k);

    /* Block size x work factor combinations */
    RUN(bs_wf_combinations);

    /* Data stress patterns */
    RUN(compress_all_same_byte);
    RUN(compress_random_data_50k);
    RUN(compress_two_byte_alphabet);
    RUN(compress_run_of_runs);

    /* OOM injection */
    RUN(oom_compress_init_fail_1st);
    RUN(oom_compress_init_fail_2nd);
    RUN(oom_compress_init_fail_3rd);
    RUN(oom_compress_init_fail_4th);
    RUN(oom_decompress_init_fail_1st);
    RUN(oom_decompress_init_fail_2nd);
TEST_MAIN_END()
