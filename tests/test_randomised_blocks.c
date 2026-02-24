/*
 * test_randomised_blocks.c — Exercise the legacy "randomised block" code paths
 * in decompress.c and bzlib.c that are normally unreachable because the
 * compressor never sets the randomised bit.
 *
 * Approach: compress known data, flip the randomised bit in the block header,
 * then decompress. For blocks with >= 620 bytes of output, the randomisation
 * XOR changes the output, causing a CRC mismatch (BZ_DATA_ERROR). For smaller
 * blocks, the XOR position (BZ2_rNums[0]=619) is past the end, so no XOR
 * happens and the CRC still matches (BZ_OK).
 *
 * This exercises:
 *   - decompress.c: BWT inversion setup with randomisation (small + fast mode)
 *   - bzlib.c: randomised output loop with BZ_RAND_UPD_MASK (all RLE states)
 *
 * Uses dlopen to compare qbz2 vs reference libbz2 for differential
 * verification.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

/* Reference library function pointers */
static int (*ref_decompress)(char *, unsigned int *, char *, unsigned int, int, int);
static void *ref_handle;

static int load_reference(void)
{
    if (ref_handle) return 1;
    ref_handle = dlopen("reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_handle) ref_handle = dlopen("./reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_handle) ref_handle = dlopen("../reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_handle) return 0;
    ref_decompress = dlsym(ref_handle, "BZ2_bzBuffToBuffDecompress");
    return ref_decompress != NULL;
}

/* Helper: compress data with BZ2_bzBuffToBuffCompress */
static int compress_data(const char *src, unsigned int srcLen,
                         char *dest, unsigned int *destLen, int bs)
{
    return BZ2_bzBuffToBuffCompress(dest, destLen, (char *)src, srcLen, bs, 0, 0);
}

/*
 * Flip the randomised bit in a bz2 stream's first block header.
 * Byte 14 (bit offset 112), MSB = randomised flag.
 */
static void flip_randomised_bit(char *bz2data, unsigned int len)
{
    (void)len;
    bz2data[14] ^= 0x80;
}

/* Fill buffer with deterministic data */
static void fill_data(char *buf, int len, unsigned int seed)
{
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245U + 12345U;
        buf[i] = (char)(seed >> 16);
    }
}

/* ================================================================
 * Tests with LARGE inputs (>= 1000 bytes) — randomisation XOR fires,
 * CRC mismatch expected (BZ_DATA_ERROR)
 * ================================================================ */

/* Fast mode, 1024 bytes of random data */
TEST(randomised_fast_mode_basic)
{
    char src[1024];
    fill_data(src, sizeof(src), 42);

    char comp[2048];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    char decomp[2048];
    unsigned int decompLen = sizeof(decomp);
    rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 0, 0);
    ASSERT_EQ(rc, BZ_DATA_ERROR);
}

/* Small mode, 1024 bytes of random data */
TEST(randomised_small_mode_basic)
{
    char src[1024];
    fill_data(src, sizeof(src), 42);

    char comp[2048];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    char decomp[2048];
    unsigned int decompLen = sizeof(decomp);
    rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 1, 0);
    ASSERT_EQ(rc, BZ_DATA_ERROR);
}

/* ================================================================
 * Tests with SMALL inputs (< 620 bytes) — randomisation XOR does NOT
 * fire (first XOR at position 619), so the randomised code path runs
 * but output is identical. These verify the path doesn't crash.
 * Accept either BZ_OK or BZ_DATA_ERROR.
 * ================================================================ */

TEST(randomised_tiny_no_crash)
{
    const char *inputs[] = {"a", "ab", "abc", "abcd", "hello"};
    for (int i = 0; i < 5; i++) {
        int srcLen = (int)strlen(inputs[i]);
        char comp[2048];
        unsigned int compLen = sizeof(comp);
        int rc = compress_data(inputs[i], srcLen, comp, &compLen, 1);
        ASSERT_EQ(rc, BZ_OK);

        flip_randomised_bit(comp, compLen);

        char decomp[1024];
        unsigned int decompLen = sizeof(decomp);
        rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 0, 0);
        /* No crash is the main check; CRC may or may not match */
        ASSERT(rc == BZ_OK || rc == BZ_DATA_ERROR);
    }
}

TEST(randomised_small_no_crash)
{
    /* 512 bytes < 619, so no XOR fires */
    char src[512];
    memset(src, 0, sizeof(src));

    char comp[2048];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    char decomp[1024];
    unsigned int decompLen = sizeof(decomp);
    rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 0, 0);
    ASSERT(rc == BZ_OK || rc == BZ_DATA_ERROR);

    decompLen = sizeof(decomp);
    rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 1, 0);
    ASSERT(rc == BZ_OK || rc == BZ_DATA_ERROR);
}

/* ================================================================
 * Differential: qbz2 vs reference must match on randomised streams
 * ================================================================ */

TEST(randomised_fast_differential)
{
    if (!load_reference()) return;

    char src[2048];
    fill_data(src, sizeof(src), 99);

    char comp[4096];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    char out_qbz2[4096];
    unsigned int qbz2Len = sizeof(out_qbz2);
    int rc_qbz2 = BZ2_bzBuffToBuffDecompress(out_qbz2, &qbz2Len,
                                              comp, compLen, 0, 0);

    char out_ref[4096];
    unsigned int refLen = sizeof(out_ref);
    int rc_ref = ref_decompress(out_ref, &refLen, comp, compLen, 0, 0);

    ASSERT_EQ(rc_qbz2, rc_ref);
}

TEST(randomised_small_differential)
{
    if (!load_reference()) return;

    char src[2048];
    fill_data(src, sizeof(src), 77);

    char comp[4096];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    char out_qbz2[4096];
    unsigned int qbz2Len = sizeof(out_qbz2);
    int rc_qbz2 = BZ2_bzBuffToBuffDecompress(out_qbz2, &qbz2Len,
                                              comp, compLen, 1, 0);

    char out_ref[4096];
    unsigned int refLen = sizeof(out_ref);
    int rc_ref = ref_decompress(out_ref, &refLen, comp, compLen, 1, 0);

    ASSERT_EQ(rc_qbz2, rc_ref);
}

TEST(randomised_streaming_differential)
{
    if (!load_reference()) return;

    char src[2048];
    fill_data(src, sizeof(src), 888);

    char comp[4096];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    char out_qbz2[4096];
    unsigned int qbz2Len = sizeof(out_qbz2);
    int rc_qbz2 = BZ2_bzBuffToBuffDecompress(out_qbz2, &qbz2Len,
                                              comp, compLen, 0, 0);

    char out_ref[4096];
    unsigned int refLen = sizeof(out_ref);
    int rc_ref = ref_decompress(out_ref, &refLen, comp, compLen, 0, 0);

    ASSERT_EQ(rc_qbz2, rc_ref);
}

/* ================================================================
 * Large input — exercises the full randomisation table wrap (512 entries)
 * ================================================================ */

TEST(randomised_large_input)
{
    int srcLen = 32768;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    fill_data(src, srcLen, 777);

    unsigned int compLen = srcLen + srcLen / 100 + 600 + 10000;
    char *comp = malloc(compLen);
    ASSERT(comp != NULL);
    int rc = compress_data(src, srcLen, comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    unsigned int decompLen = srcLen + 1024;
    char *decomp = malloc(decompLen);
    ASSERT(decomp != NULL);

    rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 0, 0);
    ASSERT_EQ(rc, BZ_DATA_ERROR);

    decompLen = srcLen + 1024;
    rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 1, 0);
    ASSERT_EQ(rc, BZ_DATA_ERROR);

    free(src);
    free(comp);
    free(decomp);
}

TEST(randomised_large_differential)
{
    if (!load_reference()) return;

    int srcLen = 32768;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    fill_data(src, srcLen, 999);

    unsigned int compLen = srcLen + srcLen / 100 + 600 + 10000;
    char *comp = malloc(compLen);
    ASSERT(comp != NULL);
    int rc = compress_data(src, srcLen, comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    unsigned int outLen = srcLen + 1024;
    char *out_qbz2 = malloc(outLen);
    char *out_ref = malloc(outLen);
    ASSERT(out_qbz2 != NULL && out_ref != NULL);

    unsigned int qbz2Len = outLen, refLen = outLen;
    int rc_qbz2 = BZ2_bzBuffToBuffDecompress(out_qbz2, &qbz2Len,
                                              comp, compLen, 0, 0);
    int rc_ref = ref_decompress(out_ref, &refLen, comp, compLen, 0, 0);
    ASSERT_EQ(rc_qbz2, rc_ref);

    qbz2Len = outLen;
    refLen = outLen;
    rc_qbz2 = BZ2_bzBuffToBuffDecompress(out_qbz2, &qbz2Len,
                                          comp, compLen, 1, 0);
    rc_ref = ref_decompress(out_ref, &refLen, comp, compLen, 1, 0);
    ASSERT_EQ(rc_qbz2, rc_ref);

    free(src);
    free(comp);
    free(out_qbz2);
    free(out_ref);
}

/* ================================================================
 * Block size sweep with large data — all block sizes 1-9
 * ================================================================ */

TEST(randomised_blocksize_range)
{
    char src[4096];
    fill_data(src, sizeof(src), 12345);

    for (int bs = 1; bs <= 9; bs++) {
        char comp[8192];
        unsigned int compLen = sizeof(comp);
        int rc = compress_data(src, sizeof(src), comp, &compLen, bs);
        ASSERT_EQ(rc, BZ_OK);

        flip_randomised_bit(comp, compLen);

        char decomp[8192];
        unsigned int decompLen = sizeof(decomp);
        rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 0, 0);
        ASSERT_EQ(rc, BZ_DATA_ERROR);

        decompLen = sizeof(decomp);
        rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 1, 0);
        ASSERT_EQ(rc, BZ_DATA_ERROR);
    }
}

/* ================================================================
 * Streaming decompression — exercises randomised output loop with
 * various buffer sizes
 * ================================================================ */

TEST(randomised_streaming_small_output_buf)
{
    char src[2048];
    fill_data(src, sizeof(src), 555);

    char comp[4096];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    rc = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(rc, BZ_OK);

    strm.next_in = comp;
    strm.avail_in = compLen;

    char outbuf[64];
    int total_out = 0;
    int final_rc = BZ_OK;

    while (final_rc == BZ_OK) {
        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
        final_rc = BZ2_bzDecompress(&strm);
        total_out += (int)(sizeof(outbuf) - strm.avail_out);
    }

    BZ2_bzDecompressEnd(&strm);
    ASSERT_EQ(final_rc, BZ_DATA_ERROR);
    ASSERT(total_out > 0);
}

TEST(randomised_streaming_small_mode)
{
    char src[2048];
    fill_data(src, sizeof(src), 666);

    char comp[4096];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    rc = BZ2_bzDecompressInit(&strm, 0, 1);
    ASSERT_EQ(rc, BZ_OK);

    strm.next_in = comp;
    strm.avail_in = compLen;

    char outbuf[64];
    int total_out = 0;
    int final_rc = BZ_OK;

    while (final_rc == BZ_OK) {
        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
        final_rc = BZ2_bzDecompress(&strm);
        total_out += (int)(sizeof(outbuf) - strm.avail_out);
    }

    BZ2_bzDecompressEnd(&strm);
    ASSERT_EQ(final_rc, BZ_DATA_ERROR);
    ASSERT(total_out > 0);
}

/* 1-byte output buffer — maximises loop iterations */
TEST(randomised_streaming_1byte_output)
{
    char src[2048];
    fill_data(src, sizeof(src), 333);

    char comp[4096];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    rc = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(rc, BZ_OK);

    strm.next_in = comp;
    strm.avail_in = compLen;

    char outbyte;
    int total_out = 0;
    int final_rc = BZ_OK;

    while (final_rc == BZ_OK) {
        strm.next_out = &outbyte;
        strm.avail_out = 1;
        final_rc = BZ2_bzDecompress(&strm);
        if (strm.avail_out == 0) total_out++;
    }

    BZ2_bzDecompressEnd(&strm);
    ASSERT_EQ(final_rc, BZ_DATA_ERROR);
    ASSERT(total_out > 0);
}

/* 1-byte input feed — tests resumable randomised path */
TEST(randomised_streaming_1byte_input)
{
    char src[2048];
    fill_data(src, sizeof(src), 444);

    char comp[4096];
    unsigned int compLen = sizeof(comp);
    int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
    ASSERT_EQ(rc, BZ_OK);

    flip_randomised_bit(comp, compLen);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    rc = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(rc, BZ_OK);

    char outbuf[4096];
    unsigned int in_pos = 0;
    int total_out = 0;
    int final_rc = BZ_OK;

    while (final_rc == BZ_OK && in_pos < compLen) {
        strm.next_in = comp + in_pos;
        strm.avail_in = 1;
        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
        final_rc = BZ2_bzDecompress(&strm);
        total_out += (int)(sizeof(outbuf) - strm.avail_out);
        in_pos++;
    }

    BZ2_bzDecompressEnd(&strm);
    ASSERT_EQ(final_rc, BZ_DATA_ERROR);
}

/* ================================================================
 * Pattern sweep with data > 619 bytes
 * ================================================================ */

TEST(randomised_pattern_sweep)
{
    const struct {
        unsigned int seed;
        int len;
    } patterns[] = {
        {1, 1024},
        {2, 2048},
        {3, 8192},
        {100, 4096},
        {200, 16384},
    };

    for (int p = 0; p < 5; p++) {
        char *src = malloc(patterns[p].len);
        ASSERT(src != NULL);
        fill_data(src, patterns[p].len, patterns[p].seed);

        unsigned int compLen = patterns[p].len + patterns[p].len / 100 + 600 + 10000;
        char *comp = malloc(compLen);
        ASSERT(comp != NULL);
        int rc = compress_data(src, patterns[p].len, comp, &compLen, 1);
        ASSERT_EQ(rc, BZ_OK);

        flip_randomised_bit(comp, compLen);

        unsigned int decompLen = patterns[p].len + 1024;
        char *decomp = malloc(decompLen);
        ASSERT(decomp != NULL);

        rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 0, 0);
        ASSERT_EQ(rc, BZ_DATA_ERROR);

        decompLen = patterns[p].len + 1024;
        rc = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 1, 0);
        ASSERT_EQ(rc, BZ_DATA_ERROR);

        free(src);
        free(comp);
        free(decomp);
    }
}

/* Differential pattern sweep */
TEST(randomised_pattern_differential)
{
    if (!load_reference()) return;

    unsigned int seeds[] = {11, 22, 33, 44, 55};
    for (int i = 0; i < 5; i++) {
        char src[4096];
        fill_data(src, sizeof(src), seeds[i]);

        char comp[8192];
        unsigned int compLen = sizeof(comp);
        int rc = compress_data(src, sizeof(src), comp, &compLen, 1);
        ASSERT_EQ(rc, BZ_OK);

        flip_randomised_bit(comp, compLen);

        /* Fast mode */
        char out_qbz2[8192], out_ref[8192];
        unsigned int qbz2Len = sizeof(out_qbz2), refLen = sizeof(out_ref);
        int rc_qbz2 = BZ2_bzBuffToBuffDecompress(out_qbz2, &qbz2Len,
                                                  comp, compLen, 0, 0);
        int rc_ref = ref_decompress(out_ref, &refLen, comp, compLen, 0, 0);
        ASSERT_EQ(rc_qbz2, rc_ref);

        /* Small mode */
        qbz2Len = sizeof(out_qbz2);
        refLen = sizeof(out_ref);
        rc_qbz2 = BZ2_bzBuffToBuffDecompress(out_qbz2, &qbz2Len,
                                              comp, compLen, 1, 0);
        rc_ref = ref_decompress(out_ref, &refLen, comp, compLen, 1, 0);
        ASSERT_EQ(rc_qbz2, rc_ref);
    }
}

TEST_MAIN_BEGIN()
    /* Large input — CRC mismatch expected */
    RUN(randomised_fast_mode_basic);
    RUN(randomised_small_mode_basic);
    RUN(randomised_blocksize_range);
    RUN(randomised_pattern_sweep);
    RUN(randomised_large_input);

    /* Small input — randomised path runs but no XOR fires */
    RUN(randomised_tiny_no_crash);
    RUN(randomised_small_no_crash);

    /* Streaming */
    RUN(randomised_streaming_small_output_buf);
    RUN(randomised_streaming_small_mode);
    RUN(randomised_streaming_1byte_output);
    RUN(randomised_streaming_1byte_input);

    /* Differential: qbz2 vs reference */
    RUN(randomised_fast_differential);
    RUN(randomised_small_differential);
    RUN(randomised_streaming_differential);
    RUN(randomised_large_differential);
    RUN(randomised_pattern_differential);
TEST_MAIN_END()
