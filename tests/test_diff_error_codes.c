/* test_diff_error_codes.c — differential error code comparison on malformed inputs
 *
 * For each malformed bz2 input, decompress with both qbz2 and the reference
 * libbz2 and verify the error codes match. This catches divergences where
 * the library returns a different error code than the reference on the same
 * bad input (e.g., BZ_DATA_ERROR vs BZ_DATA_ERROR_MAGIC).
 *
 * Covers: bit flips, byte substitutions, CRC corruption, block magic corruption,
 * stream footer corruption, truncation patterns, oversized origPtr, bad selectors,
 * and various structural mutations.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

/* Reference library function pointer */
typedef int (*ref_b2b_decompress_t)(char*, unsigned int*, char*, unsigned int, int, int);
static ref_b2b_decompress_t ref_decompress = NULL;
static void *ref_handle = NULL;

static int load_reference(void)
{
    if (ref_handle) return 1;
    const char *paths[] = {
        "reference/libbz2_ref.so",
        "./reference/libbz2_ref.so",
        "../reference/libbz2_ref.so",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        ref_handle = dlopen(paths[i], RTLD_NOW);
        if (ref_handle) break;
    }
    if (!ref_handle) return 0;
    ref_decompress = (ref_b2b_decompress_t)dlsym(ref_handle, "BZ2_bzBuffToBuffDecompress");
    return ref_decompress != NULL;
}

static int total_compared = 0;
static int total_divergences = 0;

/* Compare error codes from both libraries on a malformed input.
 * Returns 0 if codes match, 1 if divergence. */
static int diff_error(const char *desc, const char *data, unsigned int len, int small)
{
    char qout[65536], rout[65536];
    unsigned int qlen = sizeof(qout), rlen = sizeof(rout);

    int qret = BZ2_bzBuffToBuffDecompress(qout, &qlen, (char *)data, len, small, 0);
    int rret = ref_decompress(rout, &rlen, (char *)data, len, small, 0);

    total_compared++;
    if (qret != rret) {
        fprintf(stderr, "  DIVERGENCE [%s small=%d]: qbz2=%d ref=%d\n", desc, small, qret, rret);
        total_divergences++;
        return 1;
    }
    /* If both succeed, verify output matches */
    if (qret == BZ_OK) {
        if (qlen != rlen || memcmp(qout, rout, qlen) != 0) {
            fprintf(stderr, "  DIVERGENCE [%s small=%d]: both BZ_OK but output differs (qlen=%u rlen=%u)\n",
                    desc, small, qlen, rlen);
            total_divergences++;
            return 1;
        }
    }
    return 0;
}

/* Compare on both fast mode (small=0) and small mode (small=1) */
static void diff_error_both(const char *desc, const char *data, unsigned int len)
{
    diff_error(desc, data, len, 0);
    diff_error(desc, data, len, 1);
}

/* Make a valid bz2 stream, then return a mutable copy */
static char *make_stream(const char *input, unsigned int inlen, unsigned int *outlen, int bs)
{
    unsigned int maxlen = inlen + inlen / 100 + 700;
    char *buf = malloc(maxlen);
    if (!buf) return NULL;
    *outlen = maxlen;
    if (BZ2_bzBuffToBuffCompress(buf, outlen, (char *)input, inlen, bs, 0, 0) != BZ_OK) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* ---- Tests ---- */

TEST(ref_loads)
{
    ASSERT(load_reference());
}

/* Garbage inputs — not bz2 at all */
TEST(garbage_ascii)
{
    diff_error_both("garbage_ascii", "this is not bzip2 data", 22);
}

TEST(garbage_zeros)
{
    char zeros[100];
    memset(zeros, 0, sizeof(zeros));
    diff_error_both("garbage_zeros", zeros, sizeof(zeros));
}

TEST(garbage_0xff)
{
    char ff[100];
    memset(ff, 0xFF, sizeof(ff));
    diff_error_both("garbage_0xff", ff, sizeof(ff));
}

TEST(empty_input)
{
    diff_error_both("empty_input", "", 0);
}

TEST(single_byte_inputs)
{
    for (int b = 0; b < 256; b++) {
        char c = (char)b;
        char desc[32];
        snprintf(desc, sizeof(desc), "single_byte_0x%02x", b);
        diff_error(desc, &c, 1, 0);
    }
}

/* Header mutations */
TEST(truncated_headers)
{
    const char *hdr = "BZh9\x31\x41\x59\x26\x53\x59";
    for (unsigned int i = 1; i <= 10; i++) {
        char desc[32];
        snprintf(desc, sizeof(desc), "trunc_hdr@%u", i);
        diff_error_both(desc, hdr, i);
    }
}

TEST(bad_magic_byte0)
{
    unsigned int clen;
    char *stream = make_stream("hello", 5, &clen, 1);
    ASSERT(stream != NULL);
    for (int b = 0; b < 256; b++) {
        if (b == 'B') continue;
        stream[0] = (char)b;
        char desc[32];
        snprintf(desc, sizeof(desc), "magic0=0x%02x", b);
        diff_error_both(desc, stream, clen);
    }
    stream[0] = 'B'; /* restore */
    free(stream);
}

TEST(bad_magic_byte1)
{
    unsigned int clen;
    char *stream = make_stream("hello", 5, &clen, 1);
    ASSERT(stream != NULL);
    for (int b = 0; b < 256; b++) {
        if (b == 'Z') continue;
        stream[1] = (char)b;
        char desc[32];
        snprintf(desc, sizeof(desc), "magic1=0x%02x", b);
        diff_error_both(desc, stream, clen);
    }
    stream[1] = 'Z';
    free(stream);
}

TEST(bad_magic_byte2)
{
    unsigned int clen;
    char *stream = make_stream("hello", 5, &clen, 1);
    ASSERT(stream != NULL);
    for (int b = 0; b < 256; b++) {
        if (b == 'h') continue;
        stream[2] = (char)b;
        char desc[32];
        snprintf(desc, sizeof(desc), "magic2=0x%02x", b);
        diff_error_both(desc, stream, clen);
    }
    stream[2] = 'h';
    free(stream);
}

TEST(bad_block_level)
{
    unsigned int clen;
    char *stream = make_stream("hello", 5, &clen, 1);
    ASSERT(stream != NULL);
    char orig = stream[3];
    /* block level must be '1'-'9', test all invalid values */
    for (int b = 0; b < 256; b++) {
        if (b >= '1' && b <= '9') continue;
        stream[3] = (char)b;
        char desc[32];
        snprintf(desc, sizeof(desc), "block_level=0x%02x", b);
        diff_error_both(desc, stream, clen);
    }
    stream[3] = orig;
    free(stream);
}

/* Block magic corruption (bytes 4-9 of a block) */
TEST(block_magic_corruption)
{
    unsigned int clen;
    char *stream = make_stream("test data for block magic", 25, &clen, 1);
    ASSERT(stream != NULL);

    /* Flip each byte in the block magic area (offset 4..9 in the stream,
     * which is where pi-hex block magic starts) */
    for (int pos = 4; pos < 10 && pos < (int)clen; pos++) {
        char orig = stream[pos];
        for (int bit = 0; bit < 8; bit++) {
            stream[pos] = orig ^ (1 << bit);
            char desc[48];
            snprintf(desc, sizeof(desc), "block_magic_flip@%d_bit%d", pos, bit);
            diff_error_both(desc, stream, clen);
        }
        stream[pos] = orig;
    }
    free(stream);
}

/* CRC corruption — flip bits in the block CRC (bytes 10-13) */
TEST(block_crc_corruption)
{
    unsigned int clen;
    char *stream = make_stream("CRC corruption test input data that is long enough", 50, &clen, 1);
    ASSERT(stream != NULL);

    for (int pos = 10; pos < 14 && pos < (int)clen; pos++) {
        char orig = stream[pos];
        stream[pos] = orig ^ 0xFF;
        char desc[48];
        snprintf(desc, sizeof(desc), "block_crc_flip@%d", pos);
        diff_error_both(desc, stream, clen);
        stream[pos] = orig;
    }
    free(stream);
}

/* Truncation at every byte of a valid stream */
TEST(full_stream_truncation)
{
    unsigned int clen;
    char *stream = make_stream("truncation sweep input", 22, &clen, 1);
    ASSERT(stream != NULL);

    for (unsigned int i = 1; i < clen; i++) {
        char desc[48];
        snprintf(desc, sizeof(desc), "trunc@%u/%u", i, clen);
        diff_error_both(desc, stream, i);
    }
    free(stream);
}

/* Bit flip sweep over entire valid stream */
TEST(bitflip_sweep)
{
    unsigned int clen;
    char *stream = make_stream("bit flip sweep data", 19, &clen, 1);
    ASSERT(stream != NULL);

    for (unsigned int pos = 0; pos < clen; pos++) {
        char orig = stream[pos];
        for (int bit = 0; bit < 8; bit++) {
            stream[pos] = orig ^ (1 << bit);
            char desc[48];
            snprintf(desc, sizeof(desc), "bitflip@%u_bit%d", pos, bit);
            diff_error(desc, stream, clen, 0);
        }
        stream[pos] = orig;
    }
    free(stream);
}

/* Byte substitution at every position */
TEST(byte_substitution_sweep)
{
    unsigned int clen;
    char *stream = make_stream("substitution data", 17, &clen, 1);
    ASSERT(stream != NULL);

    /* Test 0x00 and 0xFF substitution at every byte */
    for (unsigned int pos = 0; pos < clen; pos++) {
        char orig = stream[pos];
        if (orig != 0x00) {
            stream[pos] = 0x00;
            char desc[48];
            snprintf(desc, sizeof(desc), "sub_zero@%u", pos);
            diff_error(desc, stream, clen, 0);
            stream[pos] = orig;
        }
        if (orig != (char)0xFF) {
            stream[pos] = (char)0xFF;
            char desc[48];
            snprintf(desc, sizeof(desc), "sub_ff@%u", pos);
            diff_error(desc, stream, clen, 0);
            stream[pos] = orig;
        }
    }
    free(stream);
}

/* Stream footer corruption (last 10 bytes) */
TEST(footer_corruption)
{
    unsigned int clen;
    char *stream = make_stream("footer corruption test data input", 33, &clen, 1);
    ASSERT(stream != NULL);

    /* The stream footer is the last 10 bytes: 6 magic + 4 CRC */
    int footer_start = (int)clen - 10;
    if (footer_start < 4) footer_start = 4;
    for (int pos = footer_start; pos < (int)clen; pos++) {
        char orig = stream[pos];
        stream[pos] = orig ^ 0xFF;
        char desc[48];
        snprintf(desc, sizeof(desc), "footer_flip@%d", pos - footer_start);
        diff_error_both(desc, stream, clen);
        stream[pos] = orig;
    }
    free(stream);
}

/* Appended junk after valid stream */
TEST(appended_junk)
{
    unsigned int clen;
    char *stream = make_stream("appended data test", 18, &clen, 1);
    ASSERT(stream != NULL);

    /* Extend with various junk */
    char *extended = malloc(clen + 100);
    ASSERT(extended != NULL);
    memcpy(extended, stream, clen);

    /* Append zeros */
    memset(extended + clen, 0, 100);
    diff_error_both("append_zeros", extended, clen + 10);
    diff_error_both("append_zeros_100", extended, clen + 100);

    /* Append 0xFF */
    memset(extended + clen, 0xFF, 100);
    diff_error_both("append_ff", extended, clen + 10);

    /* Append garbage */
    for (int i = 0; i < 100; i++) extended[clen + i] = (char)(i * 37);
    diff_error_both("append_garbage", extended, clen + 50);

    /* Append another valid stream header */
    memcpy(extended + clen, "BZh1", 4);
    diff_error_both("append_new_header", extended, clen + 4);

    free(extended);
    free(stream);
}

/* Multi-block stream corruption: corrupt second block */
TEST(multiblock_second_block_corruption)
{
    /* Create a stream large enough for multiple blocks at bs=1 (100KB block) */
    unsigned int inlen = 110000;
    char *input = malloc(inlen);
    ASSERT(input != NULL);
    unsigned int seed = 12345;
    for (unsigned int i = 0; i < inlen; i++) {
        seed = seed * 1103515245 + 12345;
        input[i] = (char)(seed >> 16);
    }

    unsigned int clen;
    char *stream = make_stream(input, inlen, &clen, 1);
    free(input);
    ASSERT(stream != NULL);

    /* Corrupt the middle of the stream — likely hits the second block */
    int mid = (int)(clen / 2);
    for (int offset = -5; offset <= 5; offset++) {
        int pos = mid + offset;
        if (pos < 4 || pos >= (int)clen) continue;
        char orig = stream[pos];
        stream[pos] = orig ^ 0xFF;
        char desc[48];
        snprintf(desc, sizeof(desc), "multiblk_corrupt@mid%+d", offset);
        diff_error_both(desc, stream, clen);
        stream[pos] = orig;
    }
    free(stream);
}

/* Different block sizes — ensure error codes match across all bs values */
TEST(error_codes_all_block_sizes)
{
    const char *input = "block size comparison test data for error code differential";
    unsigned int inlen = (unsigned int)strlen(input);

    for (int bs = 1; bs <= 9; bs++) {
        unsigned int clen;
        char *stream = make_stream(input, inlen, &clen, bs);
        ASSERT(stream != NULL);

        /* Truncate at 50% */
        char desc[48];
        snprintf(desc, sizeof(desc), "trunc50_bs%d", bs);
        diff_error_both(desc, stream, clen / 2);

        /* Flip a byte in the data area */
        int data_pos = (int)(clen * 3 / 4);
        if (data_pos < 4) data_pos = 4;
        char orig = stream[data_pos];
        stream[data_pos] = orig ^ 0xAA;
        snprintf(desc, sizeof(desc), "flip_data_bs%d", bs);
        diff_error_both(desc, stream, clen);
        stream[data_pos] = orig;

        free(stream);
    }
}

/* Streaming API error code comparison */
TEST(streaming_error_comparison)
{
    if (!ref_handle) return;

    typedef int (*ref_init_t)(bz_stream*, int, int);
    typedef int (*ref_decomp_t)(bz_stream*);
    typedef int (*ref_end_t)(bz_stream*);

    ref_init_t ref_init = (ref_init_t)dlsym(ref_handle, "BZ2_bzDecompressInit");
    ref_decomp_t ref_dec = (ref_decomp_t)dlsym(ref_handle, "BZ2_bzDecompress");
    ref_end_t ref_end = (ref_end_t)dlsym(ref_handle, "BZ2_bzDecompressEnd");
    if (!ref_init || !ref_dec || !ref_end) return;

    /* Create a truncated stream */
    unsigned int clen;
    char *stream = make_stream("streaming error test data", 24, &clen, 1);
    ASSERT(stream != NULL);
    unsigned int trunc_len = clen / 2;

    /* Decompress with qbz2 streaming */
    bz_stream qs;
    memset(&qs, 0, sizeof(qs));
    int qinit = BZ2_bzDecompressInit(&qs, 0, 0);

    /* Decompress with reference streaming */
    bz_stream rs;
    memset(&rs, 0, sizeof(rs));
    int rinit = ref_init(&rs, 0, 0);

    total_compared++;
    if (qinit != rinit) {
        fprintf(stderr, "  DIVERGENCE [streaming_init]: qbz2=%d ref=%d\n", qinit, rinit);
        total_divergences++;
    }

    if (qinit == BZ_OK && rinit == BZ_OK) {
        char qout[65536], rout[65536];
        qs.next_in = stream;
        qs.avail_in = trunc_len;
        qs.next_out = qout;
        qs.avail_out = sizeof(qout);

        rs.next_in = stream;
        rs.avail_in = trunc_len;
        rs.next_out = rout;
        rs.avail_out = sizeof(rout);

        int qret = BZ2_bzDecompress(&qs);
        int rret = ref_dec(&rs);

        total_compared++;
        if (qret != rret) {
            fprintf(stderr, "  DIVERGENCE [streaming_trunc]: qbz2=%d ref=%d\n", qret, rret);
            total_divergences++;
        }

        BZ2_bzDecompressEnd(&qs);
        ref_end(&rs);
    }

    free(stream);
}

TEST(summary)
{
    /* Previously had 4 known divergences (bitflip positions where qbz2 rejected
     * degenerate Huffman codes that the reference accepted). Fixed in 5ba2e2c by
     * gracefully degrading the fast Huffman table instead of rejecting.
     */
    printf("\nDifferential error code comparison:\n");
    printf("  Total comparisons: %d\n", total_compared);
    printf("  Total divergences: %d\n", total_divergences);
    ASSERT(total_divergences == 0);
    ASSERT(total_compared > 100);
}

TEST_MAIN_BEGIN()
    RUN(ref_loads);
    RUN(garbage_ascii);
    RUN(garbage_zeros);
    RUN(garbage_0xff);
    RUN(empty_input);
    RUN(single_byte_inputs);
    RUN(truncated_headers);
    RUN(bad_magic_byte0);
    RUN(bad_magic_byte1);
    RUN(bad_magic_byte2);
    RUN(bad_block_level);
    RUN(block_magic_corruption);
    RUN(block_crc_corruption);
    RUN(full_stream_truncation);
    RUN(bitflip_sweep);
    RUN(byte_substitution_sweep);
    RUN(footer_corruption);
    RUN(appended_junk);
    RUN(multiblock_second_block_corruption);
    RUN(error_codes_all_block_sizes);
    RUN(streaming_error_comparison);
    RUN(summary);
TEST_MAIN_END()
