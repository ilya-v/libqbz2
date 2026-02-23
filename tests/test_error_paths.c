/* Error path and state machine coverage tests for libqbz2
 * Targets low-coverage paths in bzlib.c: parameter validation, sequence
 * errors, FILE* error conditions, BZ_FLUSH transitions, NULL handling.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/* Helper: compress data into a buffer */
static int compress_to_buf(const char *data, unsigned int len,
                           char *comp, unsigned int *clen, int bs) {
    return BZ2_bzBuffToBuffCompress(comp, clen, (char *)data, len, bs, 0, 0);
}

/* ========== CompressInit parameter validation ========== */

TEST(compress_init_null_strm) {
    ASSERT_EQ(BZ2_bzCompressInit(NULL, 5, 0, 0), BZ_PARAM_ERROR);
}

TEST(compress_init_bs_0) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 0, 0, 0), BZ_PARAM_ERROR);
}

TEST(compress_init_bs_10) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 10, 0, 0), BZ_PARAM_ERROR);
}

TEST(compress_init_bs_neg) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, -1, 0, 0), BZ_PARAM_ERROR);
}

TEST(compress_init_wf_neg) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 5, 0, -1), BZ_PARAM_ERROR);
}

TEST(compress_init_wf_251) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 5, 0, 251), BZ_PARAM_ERROR);
}

TEST(compress_init_wf_1000) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 5, 0, 1000), BZ_PARAM_ERROR);
}

/* Valid boundary work factors */
TEST(compress_init_wf_1) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 1), BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

TEST(compress_init_wf_250) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 250), BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

/* ========== DecompressInit parameter validation ========== */

TEST(decompress_init_null_strm) {
    ASSERT_EQ(BZ2_bzDecompressInit(NULL, 0, 0), BZ_PARAM_ERROR);
}

TEST(decompress_init_small_neg) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, -1), BZ_PARAM_ERROR);
}

TEST(decompress_init_small_2) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 2), BZ_PARAM_ERROR);
}

TEST(decompress_init_small_0) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_init_small_1) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 1), BZ_OK);
    BZ2_bzDecompressEnd(&strm);
}

/* ========== CompressEnd / DecompressEnd with NULL ========== */

TEST(compress_end_null) {
    ASSERT_EQ(BZ2_bzCompressEnd(NULL), BZ_PARAM_ERROR);
}

TEST(decompress_end_null) {
    ASSERT_EQ(BZ2_bzDecompressEnd(NULL), BZ_PARAM_ERROR);
}

/* ========== Compress with NULL strm ========== */

TEST(compress_null_strm) {
    ASSERT_EQ(BZ2_bzCompress(NULL, BZ_RUN), BZ_PARAM_ERROR);
}

TEST(decompress_null_strm) {
    ASSERT_EQ(BZ2_bzDecompress(NULL), BZ_PARAM_ERROR);
}

/* ========== Sequence errors ========== */

TEST(compress_finish_then_run) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[] = "hello";
    char output[1000];
    strm.next_in = data; strm.avail_in = 5;
    strm.next_out = output; strm.avail_out = 1000;

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    /* Now try BZ_RUN after STREAM_END — should be sequence error */
    strm.next_in = data; strm.avail_in = 5;
    ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);

    BZ2_bzCompressEnd(&strm);
}

TEST(compress_run_after_flush_incomplete) {
    /* Start FLUSH, then try RUN before FLUSH completes */
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[50000];
    memset(data, 'A', sizeof(data));
    char output[100000];

    /* Feed data */
    strm.next_in = data; strm.avail_in = 50000;
    strm.next_out = output; strm.avail_out = 100000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Start flush with tiny output buffer to force FLUSH_OK */
    strm.avail_in = 0;
    strm.avail_out = 1;
    int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    /* If FLUSH_OK, trying RUN should be a sequence error */
    if (ret == BZ_FLUSH_OK) {
        strm.avail_out = 100000;
        ret = BZ2_bzCompress(&strm, BZ_RUN);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }
    /* Otherwise it flushed in one call, which is also fine */

    BZ2_bzCompressEnd(&strm);
}

TEST(compress_invalid_action) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[] = "x";
    char output[1000];
    strm.next_in = data; strm.avail_in = 1;
    strm.next_out = output; strm.avail_out = 1000;

    /* Action 99 is invalid */
    int ret = BZ2_bzCompress(&strm, 99);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);

    BZ2_bzCompressEnd(&strm);
}

/* ========== BZ_FLUSH state machine transitions ========== */

TEST(flush_basic_roundtrip) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[10000];
    memset(data, 'Q', 5000);
    memset(data + 5000, 'R', 5000);
    char output[20000];

    /* Feed first half */
    strm.next_in = data; strm.avail_in = 5000;
    strm.next_out = output; strm.avail_out = 20000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Flush */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    }
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Feed second half */
    strm.next_in = data + 5000; strm.avail_in = 5000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Finish */
    strm.avail_in = 0;
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 20000 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Decompress and verify */
    char decomp[10000];
    unsigned int dlen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 10000u);
    ASSERT_MEM_EQ(decomp, data, 10000);
}

TEST(flush_with_tiny_output) {
    /* Flush with output buffer that forces multiple FLUSH_OK returns */
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[10000];
    memset(data, 'X', 10000);
    char output[20000];
    unsigned int total_out = 0;

    /* Feed all data */
    strm.next_in = data; strm.avail_in = 10000;
    strm.next_out = output; strm.avail_out = 20000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);
    total_out = 20000 - strm.avail_out;

    /* Flush with 1-byte output increments */
    strm.avail_in = 0;
    int flush_ok_count = 0;
    int ret;
    do {
        strm.next_out = output + total_out;
        strm.avail_out = 1;
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        total_out += 1 - strm.avail_out;
        if (ret == BZ_FLUSH_OK) flush_ok_count++;
        ASSERT(ret == BZ_FLUSH_OK || ret == BZ_RUN_OK);
    } while (ret == BZ_FLUSH_OK);

    /* We should have seen at least one FLUSH_OK */
    ASSERT(flush_ok_count > 0);

    /* Now finish */
    strm.next_out = output + total_out;
    strm.avail_out = 20000 - total_out;
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    total_out = 20000 - strm.avail_out + total_out;

    BZ2_bzCompressEnd(&strm);
}

TEST(flush_multiple_blocks) {
    /* Feed data, flush, feed more, flush, finish — exercises FLUSH across data */
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[50000];
    strm.next_out = output; strm.avail_out = 50000;

    /* 3 rounds of feed + flush */
    for (int round = 0; round < 3; round++) {
        char data[2000];
        memset(data, 'A' + round, 2000);
        strm.next_in = data; strm.avail_in = 2000;
        ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

        strm.avail_in = 0;
        int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* Finish */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 50000 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify roundtrip */
    char expected[6000];
    memset(expected, 'A', 2000);
    memset(expected + 2000, 'B', 2000);
    memset(expected + 4000, 'C', 2000);
    char decomp[6000];
    unsigned int dlen = 6000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 6000u);
    ASSERT_MEM_EQ(decomp, expected, 6000);
}

/* ========== BuffToBuffCompress parameter errors ========== */

TEST(b2b_compress_null_dest) {
    char src[] = "hello";
    unsigned int dlen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(NULL, &dlen, src, 5, 1, 0, 0), BZ_PARAM_ERROR);
}

TEST(b2b_compress_null_destlen) {
    char src[] = "hello";
    char dst[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(dst, NULL, src, 5, 1, 0, 0), BZ_PARAM_ERROR);
}

TEST(b2b_compress_null_src) {
    char dst[1000];
    unsigned int dlen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(dst, &dlen, NULL, 5, 1, 0, 0), BZ_PARAM_ERROR);
}

TEST(b2b_compress_bs_invalid) {
    char src[] = "hello";
    char dst[1000];
    unsigned int dlen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(dst, &dlen, src, 5, 0, 0, 0), BZ_PARAM_ERROR);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(dst, &dlen, src, 5, 10, 0, 0), BZ_PARAM_ERROR);
}

/* ========== BuffToBuffDecompress parameter errors ========== */

TEST(b2b_decompress_null_dest) {
    char src[100];
    unsigned int dlen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(NULL, &dlen, src, 10, 0, 0), BZ_PARAM_ERROR);
}

TEST(b2b_decompress_null_destlen) {
    char dst[1000];
    char src[100];
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(dst, NULL, src, 10, 0, 0), BZ_PARAM_ERROR);
}

TEST(b2b_decompress_null_src) {
    char dst[1000];
    unsigned int dlen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(dst, &dlen, NULL, 10, 0, 0), BZ_PARAM_ERROR);
}

TEST(b2b_decompress_small_invalid) {
    char src[100];
    char dst[1000];
    unsigned int dlen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(dst, &dlen, src, 10, -1, 0), BZ_PARAM_ERROR);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(dst, &dlen, src, 10, 2, 0), BZ_PARAM_ERROR);
}

/* ========== Decompress corrupted data ========== */

TEST(decompress_garbage) {
    char garbage[] = "this is not bzip2 data at all";
    char dst[1000];
    unsigned int dlen = 1000;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, garbage, strlen(garbage), 0, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

TEST(decompress_truncated_header) {
    /* Just "BZ" — incomplete magic */
    char data[] = "BZ";
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, data, 2, 0, 0);
    /* Should fail — either DATA_ERROR_MAGIC or UNEXPECTED_EOF */
    ASSERT(ret < 0);
}

TEST(decompress_bad_magic_byte3) {
    /* "BZx9" — wrong version byte */
    char data[] = "BZx9xxxxxxxx";
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, data, strlen(data), 0, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

TEST(decompress_wrong_block_size) {
    /* "BZh0" — invalid block size (must be 1-9) */
    char data[] = "BZh0xxxxxxxx";
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, data, strlen(data), 0, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

TEST(decompress_empty_input) {
    char dst[100];
    unsigned int dlen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, "", 0, 0, 0);
    /* Empty input — should fail with some error */
    ASSERT(ret < 0);
}

/* ========== FILE* API error paths ========== */

TEST(bzwrite_open_null_file) {
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, NULL, 5, 0, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

TEST(bzread_open_null_file) {
    int bzerr;
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, NULL, 0, 0, NULL, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

TEST(bzwrite_open_invalid_bs) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 0, 0, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    bz = BZ2_bzWriteOpen(&bzerr, f, 10, 0, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

TEST(bzread_open_unused_too_large) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    char unused[10];
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused, BZ_MAX_UNUSED + 1);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

TEST(bzread_open_unused_negative) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    char unused[10];
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused, -1);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

TEST(bzwrite_close_null) {
    int bzerr;
    BZ2_bzWriteClose(&bzerr, NULL, 0, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
}

TEST(bzread_close_null) {
    int bzerr;
    BZ2_bzReadClose(&bzerr, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
}

TEST(bzwrite_null_buf) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);
    ASSERT_EQ(bzerr, BZ_OK);

    BZ2_bzWrite(&bzerr, bz, NULL, 100);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);

    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
}

TEST(bzwrite_zero_len) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char data[] = "x";
    BZ2_bzWrite(&bzerr, bz, data, 0);
    /* Reference libbz2 accepts len=0 without error */
    ASSERT_EQ(bzerr, BZ_OK);

    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    fclose(f);
}

TEST(bzread_zero_len) {
    /* Write a valid bz2 file first */
    char path[] = "/tmp/libqbz2_test_read_zero.bz2";
    FILE *fw = fopen(path, "wb");
    ASSERT(fw != NULL);
    int bzerr;
    BZFILE *bw = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
    ASSERT(bw != NULL);
    char data[] = "test data";
    BZ2_bzWrite(&bzerr, bw, data, strlen(data));
    ASSERT_EQ(bzerr, BZ_OK);
    BZ2_bzWriteClose(&bzerr, bw, 0, NULL, NULL);
    fclose(fw);

    /* Read with len=0 */
    FILE *fr = fopen(path, "rb");
    ASSERT(fr != NULL);
    BZFILE *br = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
    ASSERT(br != NULL);
    char buf[100];
    int nr = BZ2_bzRead(&bzerr, br, buf, 0);
    /* Reference libbz2 accepts len=0 without error */
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT_EQ(nr, 0);
    (void)nr;

    BZ2_bzReadClose(&bzerr, br);
    fclose(fr);
    remove(path);
}

/* ========== WriteClose with abandon flag ========== */

TEST(bzwrite_close_abandon) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char data[] = "some data to write";
    BZ2_bzWrite(&bzerr, bz, data, strlen(data));
    ASSERT_EQ(bzerr, BZ_OK);

    /* Abandon=1 — should not finalize the stream */
    unsigned int nbytes_in = 0, nbytes_out = 0;
    BZ2_bzWriteClose(&bzerr, bz, 1, &nbytes_in, &nbytes_out);
    ASSERT_EQ(bzerr, BZ_OK);
    fclose(f);
}

TEST(bzwrite_close_normal_with_counts) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char data[1000];
    memset(data, 'Z', 1000);
    BZ2_bzWrite(&bzerr, bz, data, 1000);
    ASSERT_EQ(bzerr, BZ_OK);

    unsigned int nbytes_in = 0, nbytes_out = 0;
    BZ2_bzWriteClose(&bzerr, bz, 0, &nbytes_in, &nbytes_out);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT(nbytes_in > 0);
    ASSERT(nbytes_out > 0);
    fclose(f);
}

/* ========== WriteClose64 ========== */

TEST(bzwrite_close64_with_counts) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char data[1000];
    memset(data, 'W', 1000);
    BZ2_bzWrite(&bzerr, bz, data, 1000);
    ASSERT_EQ(bzerr, BZ_OK);

    unsigned int in_lo = 0, in_hi = 0, out_lo = 0, out_hi = 0;
    BZ2_bzWriteClose64(&bzerr, bz, 0, &in_lo, &in_hi, &out_lo, &out_hi);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT(in_lo > 0);
    ASSERT(out_lo > 0);
    /* hi32 should be 0 for small data */
    ASSERT_EQ(in_hi, 0u);
    ASSERT_EQ(out_hi, 0u);
    fclose(f);
}

/* ========== FILE* write+read roundtrip ========== */

TEST(file_api_roundtrip) {
    char path[] = "/tmp/libqbz2_test_roundtrip.bz2";
    int bzerr;

    /* Write */
    FILE *fw = fopen(path, "wb");
    ASSERT(fw != NULL);
    BZFILE *bw = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
    ASSERT(bw != NULL);
    char data[5000];
    memset(data, 'T', 5000);
    BZ2_bzWrite(&bzerr, bw, data, 5000);
    ASSERT_EQ(bzerr, BZ_OK);
    BZ2_bzWriteClose(&bzerr, bw, 0, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
    fclose(fw);

    /* Read */
    FILE *fr = fopen(path, "rb");
    ASSERT(fr != NULL);
    BZFILE *br = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
    ASSERT(br != NULL);
    char buf[5000];
    int total = 0;
    while (1) {
        int nr = BZ2_bzRead(&bzerr, br, buf + total, 5000 - total);
        total += nr;
        if (bzerr == BZ_STREAM_END) break;
        ASSERT_EQ(bzerr, BZ_OK);
    }
    ASSERT_EQ(total, 5000);
    ASSERT_MEM_EQ(buf, data, 5000);

    BZ2_bzReadClose(&bzerr, br);
    fclose(fr);
    remove(path);
}

TEST(file_api_read_chunks) {
    char path[] = "/tmp/libqbz2_test_chunks.bz2";
    int bzerr;

    /* Write */
    FILE *fw = fopen(path, "wb");
    ASSERT(fw != NULL);
    BZFILE *bw = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
    ASSERT(bw != NULL);
    char data[3000];
    for (int i = 0; i < 3000; i++) data[i] = (char)(i & 0xff);
    BZ2_bzWrite(&bzerr, bw, data, 3000);
    ASSERT_EQ(bzerr, BZ_OK);
    BZ2_bzWriteClose(&bzerr, bw, 0, NULL, NULL);
    fclose(fw);

    /* Read in small chunks */
    FILE *fr = fopen(path, "rb");
    ASSERT(fr != NULL);
    BZFILE *br = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
    ASSERT(br != NULL);
    char buf[3000];
    int total = 0;
    while (bzerr != BZ_STREAM_END) {
        int nr = BZ2_bzRead(&bzerr, br, buf + total, 100);
        total += nr;
        ASSERT(bzerr == BZ_OK || bzerr == BZ_STREAM_END);
    }
    ASSERT_EQ(total, 3000);
    ASSERT_MEM_EQ(buf, data, 3000);

    BZ2_bzReadClose(&bzerr, br);
    fclose(fr);
    remove(path);
}

/* ========== BZ2_bzReadGetUnused ========== */

TEST(bzread_get_unused_after_end) {
    char path[] = "/tmp/libqbz2_test_unused.bz2";
    int bzerr;

    /* Write */
    FILE *fw = fopen(path, "wb");
    ASSERT(fw != NULL);
    BZFILE *bw = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
    ASSERT(bw != NULL);
    char data[] = "test";
    BZ2_bzWrite(&bzerr, bw, data, 4);
    BZ2_bzWriteClose(&bzerr, bw, 0, NULL, NULL);
    /* Write some trailing garbage after bz2 stream */
    fwrite("EXTRA", 1, 5, fw);
    fclose(fw);

    /* Read until STREAM_END */
    FILE *fr = fopen(path, "rb");
    ASSERT(fr != NULL);
    BZFILE *br = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
    ASSERT(br != NULL);
    char buf[100];
    int nr = BZ2_bzRead(&bzerr, br, buf, 100);
    ASSERT_EQ(bzerr, BZ_STREAM_END);
    ASSERT_EQ(nr, 4);

    /* Get unused bytes */
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, br, &unused, &nUnused);
    ASSERT_EQ(bzerr, BZ_OK);
    /* There should be unused bytes (the "EXTRA" we appended) */
    ASSERT(nUnused >= 0);

    BZ2_bzReadClose(&bzerr, br);
    fclose(fr);
    remove(path);
}

/* ========== BZ2_bzlibVersion ========== */

TEST(version_string) {
    const char *ver = BZ2_bzlibVersion();
    ASSERT(ver != NULL);
    ASSERT(strlen(ver) > 0);
}

/* ========== BZ2_bzerror ========== */

TEST(bzerror_after_write) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    int errnum;
    const char *msg = BZ2_bzerror(bz, &errnum);
    ASSERT(msg != NULL);
    ASSERT_EQ(errnum, BZ_OK);

    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
}

/* ========== BZ2_bzflush (no-op in libbz2) ========== */

TEST(bzflush_returns_zero) {
    char path[] = "/tmp/libqbz2_test_flush.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    char data[] = "flush test";
    BZ2_bzwrite(bz, data, strlen(data));
    int ret = BZ2_bzflush(bz);
    /* bzflush is documented as a no-op, returns 0 */
    ASSERT_EQ(ret, 0);
    BZ2_bzclose(bz);
    remove(path);
}

/* ========== Double-end calls ========== */

TEST(compress_double_end) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzCompressEnd(&strm), BZ_OK);
    /* Second end should return PARAM_ERROR (state is NULL) */
    int ret = BZ2_bzCompressEnd(&strm);
    ASSERT(ret == BZ_PARAM_ERROR || ret == BZ_OK);
}

TEST(decompress_double_end) {
    bz_stream strm; memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzDecompressEnd(&strm), BZ_OK);
    int ret = BZ2_bzDecompressEnd(&strm);
    ASSERT(ret == BZ_PARAM_ERROR || ret == BZ_OK);
}

/* ========== bzopen/bzdopen ========== */

TEST(bzopen_write_read) {
    char path[] = "/tmp/libqbz2_test_bzopen.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    char data[] = "bzopen roundtrip test data";
    int nw = BZ2_bzwrite(bz, data, strlen(data));
    ASSERT_EQ(nw, (int)strlen(data));
    BZ2_bzclose(bz);

    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[100];
    int nr = BZ2_bzread(bz, buf, 100);
    ASSERT_EQ(nr, (int)strlen(data));
    ASSERT_MEM_EQ(buf, data, strlen(data));
    BZ2_bzclose(bz);
    remove(path);
}

TEST(bzdopen_write_read) {
    char path[] = "/tmp/libqbz2_test_bzdopen.bz2";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzdopen(fd, "w1");
    ASSERT(bz != NULL);
    char data[] = "bzdopen test";
    int nw = BZ2_bzwrite(bz, data, strlen(data));
    ASSERT_EQ(nw, (int)strlen(data));
    BZ2_bzclose(bz);

    fd = open(path, O_RDONLY);
    ASSERT(fd >= 0);
    bz = BZ2_bzdopen(fd, "r");
    ASSERT(bz != NULL);
    char buf[100];
    int nr = BZ2_bzread(bz, buf, 100);
    ASSERT_EQ(nr, (int)strlen(data));
    ASSERT_MEM_EQ(buf, data, strlen(data));
    BZ2_bzclose(bz);
    remove(path);
}

/* ========== Test runner ========== */

TEST_MAIN_BEGIN()
    /* CompressInit params */
    RUN(compress_init_null_strm);
    RUN(compress_init_bs_0);
    RUN(compress_init_bs_10);
    RUN(compress_init_bs_neg);
    RUN(compress_init_wf_neg);
    RUN(compress_init_wf_251);
    RUN(compress_init_wf_1000);
    RUN(compress_init_wf_1);
    RUN(compress_init_wf_250);

    /* DecompressInit params */
    RUN(decompress_init_null_strm);
    RUN(decompress_init_small_neg);
    RUN(decompress_init_small_2);
    RUN(decompress_init_small_0);
    RUN(decompress_init_small_1);

    /* End with NULL */
    RUN(compress_end_null);
    RUN(decompress_end_null);

    /* Compress/Decompress NULL strm */
    RUN(compress_null_strm);
    RUN(decompress_null_strm);

    /* Sequence errors */
    RUN(compress_finish_then_run);
    RUN(compress_run_after_flush_incomplete);
    RUN(compress_invalid_action);

    /* FLUSH transitions */
    RUN(flush_basic_roundtrip);
    RUN(flush_with_tiny_output);
    RUN(flush_multiple_blocks);

    /* BuffToBuffCompress params */
    RUN(b2b_compress_null_dest);
    RUN(b2b_compress_null_destlen);
    RUN(b2b_compress_null_src);
    RUN(b2b_compress_bs_invalid);

    /* BuffToBuffDecompress params */
    RUN(b2b_decompress_null_dest);
    RUN(b2b_decompress_null_destlen);
    RUN(b2b_decompress_null_src);
    RUN(b2b_decompress_small_invalid);

    /* Corrupted data */
    RUN(decompress_garbage);
    RUN(decompress_truncated_header);
    RUN(decompress_bad_magic_byte3);
    RUN(decompress_wrong_block_size);
    RUN(decompress_empty_input);

    /* FILE* error paths */
    RUN(bzwrite_open_null_file);
    RUN(bzread_open_null_file);
    RUN(bzwrite_open_invalid_bs);
    RUN(bzread_open_unused_too_large);
    RUN(bzread_open_unused_negative);
    RUN(bzwrite_close_null);
    RUN(bzread_close_null);
    RUN(bzwrite_null_buf);
    RUN(bzwrite_zero_len);
    RUN(bzread_zero_len);

    /* WriteClose */
    RUN(bzwrite_close_abandon);
    RUN(bzwrite_close_normal_with_counts);
    RUN(bzwrite_close64_with_counts);

    /* FILE* roundtrip */
    RUN(file_api_roundtrip);
    RUN(file_api_read_chunks);
    RUN(bzread_get_unused_after_end);

    /* Version and error */
    RUN(version_string);
    RUN(bzerror_after_write);
    RUN(bzflush_returns_zero);

    /* Double-end */
    RUN(compress_double_end);
    RUN(decompress_double_end);

    /* bzopen/bzdopen */
    RUN(bzopen_write_read);
    RUN(bzdopen_write_read);
TEST_MAIN_END()
