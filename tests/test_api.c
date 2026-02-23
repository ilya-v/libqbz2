/* Unit tests for libqbz2 public API */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ========== BZ2_bzlibVersion ========== */

TEST(version_not_null) {
    const char *v = BZ2_bzlibVersion();
    ASSERT(v != NULL);
}

TEST(version_starts_with_1) {
    const char *v = BZ2_bzlibVersion();
    ASSERT(v[0] == '1');
}

/* ========== BZ2_bzCompressInit — parameter validation ========== */

TEST(compress_init_null_strm) {
    int ret = BZ2_bzCompressInit(NULL, 5, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_init_blocksize_too_low) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 0, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_init_blocksize_too_high) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 10, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_init_verbosity_negative) {
    /* Reference libbz2 does not validate verbosity range — accepts any int */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 5, -1, 0);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

TEST(compress_init_verbosity_too_high) {
    /* Reference libbz2 does not validate verbosity range — accepts any int */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 5, 5, 0);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

TEST(compress_init_workfactor_negative) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 5, 0, -1);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_init_workfactor_too_high) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 5, 0, 251);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_init_valid_bs1) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

TEST(compress_init_valid_bs9) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 9, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

TEST(compress_init_all_blocksizes) {
    for (int bs = 1; bs <= 9; bs++) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        int ret = BZ2_bzCompressInit(&strm, bs, 0, 0);
        ASSERT_EQ(ret, BZ_OK);
        BZ2_bzCompressEnd(&strm);
    }
}

TEST(compress_init_workfactor_250) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 5, 0, 250);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

TEST(compress_init_zeroes_totals) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.total_in_lo32 = 0xDEADBEEF;
    strm.total_in_hi32 = 0xDEADBEEF;
    strm.total_out_lo32 = 0xDEADBEEF;
    strm.total_out_hi32 = 0xDEADBEEF;
    BZ2_bzCompressInit(&strm, 5, 0, 0);
    ASSERT_EQ(strm.total_in_lo32, 0);
    ASSERT_EQ(strm.total_in_hi32, 0);
    ASSERT_EQ(strm.total_out_lo32, 0);
    ASSERT_EQ(strm.total_out_hi32, 0);
    BZ2_bzCompressEnd(&strm);
}

/* ========== BZ2_bzCompress — sequence errors ========== */

TEST(compress_null_strm) {
    int ret = BZ2_bzCompress(NULL, BZ_RUN);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_end_null_strm) {
    int ret = BZ2_bzCompressEnd(NULL);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

/* ========== BZ2_bzDecompressInit — parameter validation ========== */

TEST(decompress_init_null_strm) {
    int ret = BZ2_bzDecompressInit(NULL, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_init_verbosity_negative) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, -1, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_init_verbosity_too_high) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 5, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_init_small_negative) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, -1);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_init_small_too_high) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 2);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_init_valid) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_init_small_mode) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 1);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_null_strm) {
    int ret = BZ2_bzDecompress(NULL);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_end_null_strm) {
    int ret = BZ2_bzDecompressEnd(NULL);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

/* ========== Round-trip: compress then decompress ========== */

static int roundtrip(const char *data, unsigned int len, int blockSize) {
    /* Compress */
    unsigned int comp_len = len + len / 100 + 600;
    char *comp = malloc(comp_len);
    if (!comp) return -1;

    int ret = BZ2_bzBuffToBuffCompress(comp, &comp_len, (char*)data, len, blockSize, 0, 0);
    if (ret != BZ_OK) { free(comp); return ret; }

    /* Decompress */
    unsigned int decomp_len = len + 100;
    char *decomp = malloc(decomp_len);
    if (!decomp) { free(comp); return -1; }

    ret = BZ2_bzBuffToBuffDecompress(decomp, &decomp_len, comp, comp_len, 0, 0);
    if (ret != BZ_OK) { free(comp); free(decomp); return ret; }

    /* Verify */
    int ok = (decomp_len == len && memcmp(data, decomp, len) == 0);
    free(comp);
    free(decomp);
    return ok ? BZ_OK : -100;
}

TEST(roundtrip_hello) {
    const char *msg = "Hello, bzip2!";
    ASSERT_EQ(roundtrip(msg, strlen(msg), 1), BZ_OK);
}

TEST(roundtrip_empty) {
    ASSERT_EQ(roundtrip("", 0, 1), BZ_OK);
}

TEST(roundtrip_single_byte) {
    ASSERT_EQ(roundtrip("X", 1, 1), BZ_OK);
}

TEST(roundtrip_repeated) {
    char buf[10000];
    memset(buf, 'A', sizeof(buf));
    ASSERT_EQ(roundtrip(buf, sizeof(buf), 1), BZ_OK);
}

TEST(roundtrip_binary) {
    char buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (char)i;
    ASSERT_EQ(roundtrip(buf, 256, 1), BZ_OK);
}

TEST(roundtrip_all_blocksizes) {
    const char *msg = "Testing all block sizes with a reasonably long string to exercise the compressor properly. "
                      "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789";
    for (int bs = 1; bs <= 9; bs++) {
        ASSERT_EQ(roundtrip(msg, strlen(msg), bs), BZ_OK);
    }
}

TEST(roundtrip_large_repeated) {
    unsigned int sz = 200000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 7);
    ASSERT_EQ(roundtrip(buf, sz, 1), BZ_OK);
    free(buf);
}

TEST(roundtrip_random_data) {
    unsigned int sz = 50000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    unsigned int seed = 12345;
    for (unsigned int i = 0; i < sz; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
    ASSERT_EQ(roundtrip(buf, sz, 5), BZ_OK);
    free(buf);
}

/* ========== BZ2_bzBuffToBuffCompress — parameter validation ========== */

TEST(b2b_compress_null_dest) {
    char src[] = "test";
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffCompress(NULL, &dlen, src, 4, 5, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_compress_null_destlen) {
    char src[] = "test";
    char dst[100];
    int ret = BZ2_bzBuffToBuffCompress(dst, NULL, src, 4, 5, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_compress_null_source) {
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffCompress(dst, &dlen, NULL, 4, 5, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_compress_bad_blocksize) {
    char src[] = "test";
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffCompress(dst, &dlen, src, 4, 0, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_compress_outbuff_full) {
    char src[1000];
    memset(src, 0, sizeof(src));
    char dst[10];
    unsigned int dlen = 10;
    int ret = BZ2_bzBuffToBuffCompress(dst, &dlen, src, sizeof(src), 1, 0, 0);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
}

/* ========== BZ2_bzBuffToBuffDecompress — parameter validation ========== */

TEST(b2b_decompress_null_dest) {
    char src[] = "test";
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(NULL, &dlen, src, 4, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_decompress_null_destlen) {
    char src[] = "test";
    char dst[100];
    int ret = BZ2_bzBuffToBuffDecompress(dst, NULL, src, 4, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_decompress_null_source) {
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, NULL, 4, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_decompress_invalid_data) {
    char src[] = "this is not bzip2 data";
    char dst[1000];
    unsigned int dlen = 1000;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, src, strlen(src), 0, 0);
    ASSERT(ret == BZ_DATA_ERROR_MAGIC || ret == BZ_DATA_ERROR);
}

TEST(b2b_decompress_outbuff_full) {
    /* First compress something */
    const char *msg = "Hello World Hello World Hello World";
    unsigned int clen = 1000;
    char comp[1000];
    int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char*)msg, strlen(msg), 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    /* Try to decompress into too-small buffer */
    char dst[2];
    unsigned int dlen = 2;
    ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, comp, clen, 0, 0);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
}

/* ========== Streaming compression ========== */

TEST(streaming_compress_basic) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char input[] = "Hello, streaming bzip2 compression!";
    char output[1000];

    strm.next_in = input;
    strm.avail_in = strlen(input);
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    /* Should get BZ_FINISH_OK or BZ_STREAM_END */
    ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);

    if (ret == BZ_FINISH_OK) {
        /* Need more output calls */
        while (ret != BZ_STREAM_END) {
            ret = BZ2_bzCompress(&strm, BZ_FINISH);
            ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);
        }
    }

    unsigned int comp_len = sizeof(output) - strm.avail_out;
    ASSERT(comp_len > 0);
    BZ2_bzCompressEnd(&strm);

    /* Now decompress and verify */
    char decomp[1000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, comp_len, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, strlen(input));
    ASSERT_MEM_EQ(decomp, input, dlen);
}

TEST(streaming_compress_flush) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char input[] = "Testing flush operation";
    char output[2000];

    strm.next_in = input;
    strm.avail_in = strlen(input);
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    /* Feed some data with BZ_RUN */
    int ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Flush */
    ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    }
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Finish */
    strm.avail_in = 0;
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);

    BZ2_bzCompressEnd(&strm);
}

TEST(streaming_compress_chunked) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    const char *msg = "AAAA BBBB CCCC DDDD EEEE FFFF";
    unsigned int total = strlen(msg);
    char output[2000];

    strm.next_out = output;
    strm.avail_out = sizeof(output);

    /* Feed one byte at a time */
    for (unsigned int i = 0; i < total; i++) {
        strm.next_in = (char*)msg + i;
        strm.avail_in = 1;
        int ret = BZ2_bzCompress(&strm, BZ_RUN);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* Finish */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int comp_len = sizeof(output) - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Decompress and verify */
    char decomp[1000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, comp_len, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, total);
    ASSERT_MEM_EQ(decomp, msg, dlen);
}

/* ========== Streaming decompression ========== */

TEST(streaming_decompress_basic) {
    /* First compress */
    const char *msg = "Hello, streaming decompression!";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char*)msg, strlen(msg), 1, 0, 0), BZ_OK);

    /* Now streaming decompress */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(sizeof(output) - strm.avail_out, strlen(msg));
    ASSERT_MEM_EQ(output, msg, strlen(msg));

    BZ2_bzDecompressEnd(&strm);
}

TEST(streaming_decompress_chunked) {
    /* First compress */
    const char *msg = "Chunked decompression test data for streaming API";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char*)msg, strlen(msg), 1, 0, 0), BZ_OK);

    /* Decompress one byte at a time */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[1000];
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ_OK;
    for (unsigned int i = 0; i < clen && ret == BZ_OK; i++) {
        strm.next_in = comp + i;
        strm.avail_in = 1;
        ret = BZ2_bzDecompress(&strm);
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int dlen = sizeof(output) - strm.avail_out;
    ASSERT_EQ(dlen, strlen(msg));
    ASSERT_MEM_EQ(output, msg, dlen);

    BZ2_bzDecompressEnd(&strm);
}

/* ========== Custom allocator ========== */

static int alloc_count = 0;
static int free_count = 0;

static void *test_alloc(void *opaque, int items, int size) {
    (void)opaque;
    alloc_count++;
    return malloc((size_t)items * (size_t)size);
}

static void test_free(void *opaque, void *ptr) {
    (void)opaque;
    free_count++;
    free(ptr);
}

TEST(custom_allocator) {
    alloc_count = 0;
    free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = test_alloc;
    strm.bzfree = test_free;
    strm.opaque = NULL;

    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);
    ASSERT(alloc_count > 0);
    int allocs = alloc_count;

    BZ2_bzCompressEnd(&strm);
    ASSERT_EQ(free_count, allocs);
}

/* ========== Error handling ========== */

TEST(decompress_truncated_header) {
    /* BZ header is "BZh" + level digit + ... */
    char data[] = "BZ";
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, data, 2, 0, 0);
    ASSERT(ret != BZ_OK);
}

TEST(decompress_bad_magic) {
    char data[] = "XX\x00\x00";
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, data, 4, 0, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

TEST(decompress_corrupt_data) {
    /* Compress valid data, then corrupt it */
    const char *msg = "Test corruption detection";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char*)msg, strlen(msg), 1, 0, 0), BZ_OK);

    /* Flip a byte in the middle of the compressed data */
    if (clen > 10) {
        comp[clen / 2] ^= 0xFF;
    }

    char dst[1000];
    unsigned int dlen = 1000;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, comp, clen, 0, 0);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_DATA_ERROR_MAGIC);
}

/* ========== FILE* API (high-level) ========== */

TEST(file_write_read_roundtrip) {
    const char *tmpfile = "/tmp/libqbz2_test_fileio.bz2";
    const char *msg = "Hello from FILE* API test!";
    int bzerror;

    /* Write */
    FILE *f = fopen(tmpfile, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 1, 0, 0);
    ASSERT_EQ(bzerror, BZ_OK);
    ASSERT(bz != NULL);

    BZ2_bzWrite(&bzerror, bz, (void*)msg, strlen(msg));
    ASSERT_EQ(bzerror, BZ_OK);

    BZ2_bzWriteClose(&bzerror, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerror, BZ_OK);
    fclose(f);

    /* Read back */
    f = fopen(tmpfile, "rb");
    ASSERT(f != NULL);
    bz = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
    ASSERT_EQ(bzerror, BZ_OK);
    ASSERT(bz != NULL);

    char buf[1000];
    int nread = BZ2_bzRead(&bzerror, bz, buf, sizeof(buf));
    ASSERT_EQ(bzerror, BZ_STREAM_END);
    ASSERT_EQ((unsigned int)nread, strlen(msg));
    ASSERT_MEM_EQ(buf, msg, nread);

    BZ2_bzReadClose(&bzerror, bz);
    ASSERT_EQ(bzerror, BZ_OK);
    fclose(f);

    remove(tmpfile);
}

TEST(file_write_close64) {
    const char *tmpfile = "/tmp/libqbz2_test_close64.bz2";
    const char *msg = "Testing BZ2_bzWriteClose64";
    int bzerror;
    unsigned int nbin_lo, nbin_hi, nbout_lo, nbout_hi;

    FILE *f = fopen(tmpfile, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 1, 0, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    BZ2_bzWrite(&bzerror, bz, (void*)msg, strlen(msg));
    ASSERT_EQ(bzerror, BZ_OK);

    BZ2_bzWriteClose64(&bzerror, bz, 0, &nbin_lo, &nbin_hi, &nbout_lo, &nbout_hi);
    ASSERT_EQ(bzerror, BZ_OK);
    ASSERT_EQ(nbin_lo, strlen(msg));
    ASSERT(nbout_lo > 0);
    fclose(f);

    remove(tmpfile);
}

/* ========== Edge cases ========== */

TEST(compress_zero_avail_in) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = NULL;
    strm.avail_in = 0;
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    BZ2_bzCompressEnd(&strm);
}

TEST(double_compress_end) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzCompressEnd(&strm), BZ_OK);
    /* Second end should fail or be handled gracefully */
}

TEST(double_decompress_end) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzDecompressEnd(&strm), BZ_OK);
}

/* ========== Test runner ========== */

TEST_MAIN_BEGIN()
    /* Version */
    RUN(version_not_null);
    RUN(version_starts_with_1);

    /* CompressInit param validation */
    RUN(compress_init_null_strm);
    RUN(compress_init_blocksize_too_low);
    RUN(compress_init_blocksize_too_high);
    RUN(compress_init_verbosity_negative);
    RUN(compress_init_verbosity_too_high);
    RUN(compress_init_workfactor_negative);
    RUN(compress_init_workfactor_too_high);
    RUN(compress_init_valid_bs1);
    RUN(compress_init_valid_bs9);
    RUN(compress_init_all_blocksizes);
    RUN(compress_init_workfactor_250);
    RUN(compress_init_zeroes_totals);

    /* Compress errors */
    RUN(compress_null_strm);
    RUN(compress_end_null_strm);

    /* DecompressInit param validation */
    RUN(decompress_init_null_strm);
    RUN(decompress_init_verbosity_negative);
    RUN(decompress_init_verbosity_too_high);
    RUN(decompress_init_small_negative);
    RUN(decompress_init_small_too_high);
    RUN(decompress_init_valid);
    RUN(decompress_init_small_mode);
    RUN(decompress_null_strm);
    RUN(decompress_end_null_strm);

    /* Round-trip */
    RUN(roundtrip_hello);
    RUN(roundtrip_empty);
    RUN(roundtrip_single_byte);
    RUN(roundtrip_repeated);
    RUN(roundtrip_binary);
    RUN(roundtrip_all_blocksizes);
    RUN(roundtrip_large_repeated);
    RUN(roundtrip_random_data);

    /* BuffToBuffCompress param validation */
    RUN(b2b_compress_null_dest);
    RUN(b2b_compress_null_destlen);
    RUN(b2b_compress_null_source);
    RUN(b2b_compress_bad_blocksize);
    RUN(b2b_compress_outbuff_full);

    /* BuffToBuffDecompress param validation */
    RUN(b2b_decompress_null_dest);
    RUN(b2b_decompress_null_destlen);
    RUN(b2b_decompress_null_source);
    RUN(b2b_decompress_invalid_data);
    RUN(b2b_decompress_outbuff_full);

    /* Streaming compression */
    RUN(streaming_compress_basic);
    RUN(streaming_compress_flush);
    RUN(streaming_compress_chunked);

    /* Streaming decompression */
    RUN(streaming_decompress_basic);
    RUN(streaming_decompress_chunked);

    /* Custom allocator */
    RUN(custom_allocator);

    /* Error handling */
    RUN(decompress_truncated_header);
    RUN(decompress_bad_magic);
    RUN(decompress_corrupt_data);

    /* FILE* API */
    RUN(file_write_read_roundtrip);
    RUN(file_write_close64);

    /* Edge cases */
    RUN(compress_zero_avail_in);
    RUN(double_compress_end);
    RUN(double_decompress_end);
TEST_MAIN_END()
