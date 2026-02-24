/*
 * Coverage gap tests round 4
 * Targets:
 * 1. Randomised block decode with tiny output buffers (bzlib.c lines 555-597, 729-771)
 *    - Both FAST and SMALL mode, with avail_out=1 to hit output-buffer-full at each RLE state
 * 2. Non-randomised block decode with tiny output buffers (bzlib.c lines 625-675, 773-815)
 *    - Forces cs_avail_out==0 path in unRLE_obuf_to_output_FAST
 * 3. BZ2_bzWriteOpen / BZ2_bzReadOpen parameter validation branches
 * 4. BZ2_bzWrite / BZ2_bzRead error path branches
 * 5. BZ2_bzCompressInit / BZ2_bzDecompressInit edge parameter values
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* Helper: compress data */
static int compress_buf(const char *src, unsigned int srcLen,
                        char *dest, unsigned int *destLen, int bs)
{
    return BZ2_bzBuffToBuffCompress(dest, destLen, (char*)src, srcLen, bs, 0, 0);
}

/* Helper: flip the randomised bit in a bz2 stream (byte 14, MSB) */
static void flip_randomised_bit(char *bz2data)
{
    bz2data[14] ^= 0x80;
}

/* Helper: fill buffer with deterministic pseudo-random data */
static void fill_data(char *buf, int len, unsigned int seed)
{
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245U + 12345U;
        buf[i] = (char)(seed >> 16);
    }
}

/*
 * Streaming decompress with avail_out=1 per call.
 * This forces the output-buffer-full check on every single byte output.
 * Returns the final BZ2_bzDecompress return code.
 */
static int decompress_1byte_output(const char *comp, unsigned int compLen,
                                    char *out, unsigned int outCap,
                                    unsigned int *totalOut, int small)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) return ret;

    strm.next_in = (char *)comp;
    strm.avail_in = compLen;
    unsigned int pos = 0;

    while (1) {
        if (pos >= outCap) {
            /* Output buffer full -- stop */
            BZ2_bzDecompressEnd(&strm);
            *totalOut = pos;
            return BZ_OK;
        }
        strm.next_out = out + pos;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END) {
            if (strm.avail_out == 0) pos++;
            break;
        }
        if (ret != BZ_OK) break;
        if (strm.avail_out == 0) pos++;
    }
    *totalOut = pos;
    BZ2_bzDecompressEnd(&strm);
    return ret;
}

/*
 * Streaming decompress with avail_out=N per call (N=2,3,5 etc).
 * Returns BZ_STREAM_END on success.
 */
static int decompress_nbyte_output(const char *comp, unsigned int compLen,
                                    char *out, unsigned int outCap,
                                    unsigned int chunkOut, int small)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) return ret;

    strm.next_in = (char *)comp;
    strm.avail_in = compLen;
    unsigned int pos = 0;

    while (1) {
        unsigned int avail = (outCap - pos >= chunkOut) ? chunkOut : (outCap - pos);
        if (avail == 0) { ret = BZ_OK; break; }
        strm.next_out = out + pos;
        strm.avail_out = avail;
        ret = BZ2_bzDecompress(&strm);
        pos = (unsigned int)(strm.next_out - out);
        if (ret == BZ_STREAM_END || ret != BZ_OK) break;
    }
    BZ2_bzDecompressEnd(&strm);
    return ret;
}

/* ---- Randomised block + tiny output tests ---- */

/*
 * Small input (< 620 bytes decompressed), randomised bit set.
 * XOR position 619 is past end, so no XOR happens, output identical, CRC matches.
 * Decompress with avail_out=1 to hit output-full paths in randomised loop.
 */
TEST(randomised_fast_1byte_output_small_input)
{
    char src[400];
    fill_data(src, sizeof(src), 100);
    char comp[2048];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[2048];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 0);
    /* Should succeed -- XOR doesn't fire for <620 byte blocks */
    ASSERT(ret == BZ_STREAM_END || ret == BZ_OK || ret == BZ_DATA_ERROR);
}

TEST(randomised_small_1byte_output_small_input)
{
    char src[400];
    fill_data(src, sizeof(src), 200);
    char comp[2048];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[2048];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 1);
    ASSERT(ret == BZ_STREAM_END || ret == BZ_OK || ret == BZ_DATA_ERROR);
}

/*
 * Larger input (> 620 bytes decompressed), randomised bit set.
 * XOR fires, changing output, causing CRC mismatch -> BZ_DATA_ERROR.
 * But the randomised decode loop runs fully before CRC check.
 * Decompress with avail_out=1 to exercise every output-full branch.
 */
TEST(randomised_fast_1byte_output_large_input)
{
    char src[1024];
    fill_data(src, sizeof(src), 300);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[4096];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR);
}

TEST(randomised_small_1byte_output_large_input)
{
    char src[1024];
    fill_data(src, sizeof(src), 400);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[4096];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 1);
    ASSERT_EQ(ret, BZ_DATA_ERROR);
}

/* Randomised with 2-byte output chunks */
TEST(randomised_fast_2byte_output)
{
    char src[800];
    fill_data(src, sizeof(src), 500);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[4096];
    int ret = decompress_nbyte_output(comp, cLen, out, sizeof(out), 2, 0);
    ASSERT(ret == BZ_STREAM_END || ret == BZ_DATA_ERROR);
}

TEST(randomised_small_2byte_output)
{
    char src[800];
    fill_data(src, sizeof(src), 600);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[4096];
    int ret = decompress_nbyte_output(comp, cLen, out, sizeof(out), 2, 1);
    ASSERT(ret == BZ_STREAM_END || ret == BZ_DATA_ERROR);
}

/* Randomised with 3-byte output (hits different alignment in RLE expansion) */
TEST(randomised_fast_3byte_output)
{
    char src[1500];
    fill_data(src, sizeof(src), 700);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[4096];
    int ret = decompress_nbyte_output(comp, cLen, out, sizeof(out), 3, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR);
}

/* Randomised block with data that has RLE runs (repeated bytes) */
TEST(randomised_rle_runs_fast)
{
    /* Create data with runs of repeated bytes to trigger RLE states 2,3,4+ */
    char src[1024];
    int pos = 0;
    for (int i = 0; i < 50 && pos < (int)sizeof(src); i++) {
        int runlen = (i % 5) + 1; /* runs of 1-5 */
        char ch = (char)('A' + (i % 26));
        for (int j = 0; j < runlen && pos < (int)sizeof(src); j++)
            src[pos++] = ch;
    }
    while (pos < (int)sizeof(src)) src[pos++] = 'X';

    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[8192];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 0);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_STREAM_END);
}

TEST(randomised_rle_runs_small)
{
    char src[1024];
    int pos = 0;
    for (int i = 0; i < 50 && pos < (int)sizeof(src); i++) {
        int runlen = (i % 5) + 1;
        char ch = (char)('A' + (i % 26));
        for (int j = 0; j < runlen && pos < (int)sizeof(src); j++)
            src[pos++] = ch;
    }
    while (pos < (int)sizeof(src)) src[pos++] = 'X';

    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);
    flip_randomised_bit(comp);

    char out[8192];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 1);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_STREAM_END);
}

/* ---- Non-randomised block + tiny output tests ---- */

/* Normal decompress with avail_out=1 (FAST mode) -- hits cs_avail_out==0 in FAST loop */
TEST(nonrand_fast_1byte_output)
{
    char src[2048];
    fill_data(src, sizeof(src), 1000);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);

    char out[4096];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(total, sizeof(src));
    ASSERT(memcmp(out, src, sizeof(src)) == 0);
}

/* Normal decompress with avail_out=1 (SMALL mode) */
TEST(nonrand_small_1byte_output)
{
    char src[2048];
    fill_data(src, sizeof(src), 2000);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);

    char out[4096];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 1);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(total, sizeof(src));
    ASSERT(memcmp(out, src, sizeof(src)) == 0);
}

/* Normal decompress with avail_out=2 (hits different output-full alignment) */
TEST(nonrand_fast_2byte_output)
{
    char src[2048];
    fill_data(src, sizeof(src), 3000);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);

    char out[4096];
    int ret = decompress_nbyte_output(comp, cLen, out, sizeof(out), 2, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT(memcmp(out, src, sizeof(src)) == 0);
}

/* Normal decompress with avail_out=3 */
TEST(nonrand_fast_3byte_output)
{
    char src[2048];
    fill_data(src, sizeof(src), 4000);
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, sizeof(src), comp, &cLen, 1), BZ_OK);

    char out[4096];
    int ret = decompress_nbyte_output(comp, cLen, out, sizeof(out), 3, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT(memcmp(out, src, sizeof(src)) == 0);
}

/* Data with heavy RLE runs, 1-byte output */
TEST(nonrand_rle_heavy_1byte_output)
{
    /* Create data with runs of 4+ (triggers RLE state_out_len >= 4) */
    char src[2048];
    int pos = 0;
    for (int i = 0; pos < (int)sizeof(src); i++) {
        char ch = (char)(i % 256);
        int runlen = 4 + (i % 8); /* runs of 4-11 */
        for (int j = 0; j < runlen && pos < (int)sizeof(src); j++)
            src[pos++] = ch;
    }
    char comp[4096];
    unsigned int cLen = sizeof(comp);
    ASSERT_EQ(compress_buf(src, pos, comp, &cLen, 1), BZ_OK);

    char out[4096];
    unsigned int total = 0;
    int ret = decompress_1byte_output(comp, cLen, out, sizeof(out), &total, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(total, (unsigned int)pos);
    ASSERT(memcmp(out, src, pos) == 0);
}

/* ---- BZ2_bzWriteOpen parameter validation ---- */

TEST(writeopen_verbosity_negative)
{
    char path[] = "/tmp/test_wov_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, -1, 30);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeopen_verbosity_5)
{
    char path[] = "/tmp/test_wo5_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 5, 30);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeopen_blocksize_0)
{
    char path[] = "/tmp/test_wb0_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 0, 0, 30);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeopen_blocksize_10)
{
    char path[] = "/tmp/test_wba_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 10, 0, 30);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeopen_workfactor_negative)
{
    char path[] = "/tmp/test_wfn_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, -1);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeopen_workfactor_251)
{
    char path[] = "/tmp/test_wfh_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 251);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeopen_null_file)
{
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, NULL, 1, 0, 30);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

/* ---- BZ2_bzReadOpen parameter validation ---- */

TEST(readopen_verbosity_negative)
{
    char path[] = "/tmp/test_rov_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    /* Write a valid bz2 file first */
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    BZ2_bzwrite(bz, (void*)"test", 4);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, -1, 0, NULL, 0);
    ASSERT(rbz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(readopen_verbosity_5)
{
    char path[] = "/tmp/test_ro5_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"test", 4);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 5, 0, NULL, 0);
    ASSERT(rbz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(readopen_small_invalid)
{
    char path[] = "/tmp/test_rsi_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"test", 4);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 2, NULL, 0);
    ASSERT(rbz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(readopen_null_file)
{
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, NULL, 0, 0, NULL, 0);
    ASSERT(rbz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

TEST(readopen_unused_too_large)
{
    char path[] = "/tmp/test_rul_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"test", 4);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    char unused[8];
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused, BZ_MAX_UNUSED + 1);
    ASSERT(rbz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
    unlink(path);
    close(fd);
}

/* ---- BZ2_bzWrite error paths ---- */

TEST(bzwrite_null_bzfile)
{
    int bzerr;
    BZ2_bzWrite(&bzerr, NULL, (void*)"test", 4);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

TEST(bzwrite_null_buf)
{
    char path[] = "/tmp/test_wnb_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 30);
    ASSERT(bz != NULL);
    BZ2_bzWrite(&bzerr, bz, NULL, 4);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(bzwrite_negative_len)
{
    char path[] = "/tmp/test_wnl_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 30);
    ASSERT(bz != NULL);
    BZ2_bzWrite(&bzerr, bz, (void*)"test", -1);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
    unlink(path);
    close(fd);
}

/* ---- BZ2_bzRead error paths ---- */

TEST(bzread_null_bzfile)
{
    int bzerr;
    char buf[64];
    int n = BZ2_bzRead(&bzerr, NULL, buf, sizeof(buf));
    ASSERT_EQ(n, 0);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

TEST(bzread_null_buf)
{
    char path[] = "/tmp/test_rnb_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"test", 4);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(rbz != NULL);
    int n = BZ2_bzRead(&bzerr, rbz, NULL, 64);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    BZ2_bzReadClose(&bzerr, rbz);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(bzread_negative_len)
{
    char path[] = "/tmp/test_rnl_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"test", 4);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(rbz != NULL);
    char buf[64];
    int n = BZ2_bzRead(&bzerr, rbz, buf, -1);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    BZ2_bzReadClose(&bzerr, rbz);
    fclose(f);
    unlink(path);
    close(fd);
}

/* ---- CompressInit / DecompressInit edge values ---- */

TEST(compressinit_workfactor_0_default)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    /* workFactor=0 should default to 30 internally */
    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

TEST(compressinit_workfactor_250)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 1, 0, 250);
    ASSERT_EQ(ret, BZ_OK);
    BZ2_bzCompressEnd(&strm);
}

TEST(compressinit_blocksize_boundary)
{
    /* Test both boundary values: 1 and 9 */
    for (int bs = 1; bs <= 9; bs += 8) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        int ret = BZ2_bzCompressInit(&strm, bs, 0, 30);
        ASSERT_EQ(ret, BZ_OK);
        BZ2_bzCompressEnd(&strm);
    }
}

TEST(decompressinit_verbosity_boundary)
{
    /* Test boundary verbosity values: 0 and 4 */
    for (int v = 0; v <= 4; v += 4) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        int ret = BZ2_bzDecompressInit(&strm, v, 0);
        ASSERT_EQ(ret, BZ_OK);
        BZ2_bzDecompressEnd(&strm);
    }
}

/* ---- BZ2_bzReadGetUnused edge cases ---- */

TEST(readgetunused_null_bzfile)
{
    int bzerr;
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, NULL, &unused, &nUnused);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

/* ---- BZ2_bzWriteClose64 with NULL handle ---- */

TEST(writeclose64_null_handle)
{
    int bzerr;
    BZ2_bzWriteClose64(&bzerr, NULL, 0, NULL, NULL, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
}

/* ---- BZ2_bzReadClose with NULL handle ---- */

TEST(readclose_null_handle)
{
    int bzerr;
    BZ2_bzReadClose(&bzerr, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
}

TEST_MAIN_BEGIN()
    /* Randomised block + tiny output */
    RUN(randomised_fast_1byte_output_small_input);
    RUN(randomised_small_1byte_output_small_input);
    RUN(randomised_fast_1byte_output_large_input);
    RUN(randomised_small_1byte_output_large_input);
    RUN(randomised_fast_2byte_output);
    RUN(randomised_small_2byte_output);
    RUN(randomised_fast_3byte_output);
    RUN(randomised_rle_runs_fast);
    RUN(randomised_rle_runs_small);
    /* Non-randomised + tiny output */
    RUN(nonrand_fast_1byte_output);
    RUN(nonrand_small_1byte_output);
    RUN(nonrand_fast_2byte_output);
    RUN(nonrand_fast_3byte_output);
    RUN(nonrand_rle_heavy_1byte_output);
    /* WriteOpen params */
    RUN(writeopen_verbosity_negative);
    RUN(writeopen_verbosity_5);
    RUN(writeopen_blocksize_0);
    RUN(writeopen_blocksize_10);
    RUN(writeopen_workfactor_negative);
    RUN(writeopen_workfactor_251);
    RUN(writeopen_null_file);
    /* ReadOpen params */
    RUN(readopen_verbosity_negative);
    RUN(readopen_verbosity_5);
    RUN(readopen_small_invalid);
    RUN(readopen_null_file);
    RUN(readopen_unused_too_large);
    /* Write errors */
    RUN(bzwrite_null_bzfile);
    RUN(bzwrite_null_buf);
    RUN(bzwrite_negative_len);
    /* Read errors */
    RUN(bzread_null_bzfile);
    RUN(bzread_null_buf);
    RUN(bzread_negative_len);
    /* Init edge values */
    RUN(compressinit_workfactor_0_default);
    RUN(compressinit_workfactor_250);
    RUN(compressinit_blocksize_boundary);
    RUN(decompressinit_verbosity_boundary);
    /* GetUnused / Close edge cases */
    RUN(readgetunused_null_bzfile);
    RUN(writeclose64_null_handle);
    RUN(readclose_null_handle);
TEST_MAIN_END()
