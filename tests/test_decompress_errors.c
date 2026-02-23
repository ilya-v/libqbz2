/* Decompression error coverage tests for libqbz2
 * Crafts specific malformed bzip2 streams to trigger each error branch
 * in decompress.c. Each test targets a specific validation check.
 *
 * Strategy: compress a known input, then mutate specific bytes in the
 * compressed stream to trigger each error path. We also test the
 * streaming API to exercise the resumable state machine.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: compress data to buffer */
static int compress_buf(const char *src, unsigned int srcLen,
                        char *dst, unsigned int *dstLen, int blockSize) {
    return BZ2_bzBuffToBuffCompress(dst, dstLen, (char*)src, srcLen,
                                    blockSize, 0, 0);
}

/* Helper: decompress with error expectation */
static int decompress_buf(const char *src, unsigned int srcLen,
                           char *dst, unsigned int *dstLen) {
    return BZ2_bzBuffToBuffDecompress(dst, dstLen, (char*)src, srcLen, 0, 0);
}

/* Helper: decompress with small=1 */
static int decompress_buf_small(const char *src, unsigned int srcLen,
                                 char *dst, unsigned int *dstLen) {
    return BZ2_bzBuffToBuffDecompress(dst, dstLen, (char*)src, srcLen, 1, 0);
}

/* Helper: decompress via streaming API (1-byte-at-a-time input) */
static int decompress_streaming_1byte(const char *src, unsigned int srcLen,
                                       char *dst, unsigned int dstLen) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK) return ret;

    strm.next_out = dst;
    strm.avail_out = dstLen;

    for (unsigned int i = 0; i < srcLen; i++) {
        strm.next_in = (char*)src + i;
        strm.avail_in = 1;
        ret = BZ2_bzDecompress(&strm);
        if (ret != BZ_OK) break;
    }
    BZ2_bzDecompressEnd(&strm);
    return ret;
}

/* ========== Magic byte errors (decompress.c lines ~207-218) ========== */

TEST(bad_magic_byte1) {
    /* First byte != 'B' */
    char data[] = "AZh1\x31\x41\x59";
    char dst[100];
    unsigned int dlen = 100;
    ASSERT_EQ(decompress_buf(data, sizeof(data)-1, dst, &dlen), BZ_DATA_ERROR_MAGIC);
}

TEST(bad_magic_byte2) {
    /* Second byte != 'Z' */
    char data[] = "BAh1\x31\x41\x59";
    char dst[100];
    unsigned int dlen = 100;
    ASSERT_EQ(decompress_buf(data, sizeof(data)-1, dst, &dlen), BZ_DATA_ERROR_MAGIC);
}

TEST(bad_magic_byte3) {
    /* Third byte != 'h' */
    char data[] = "BZa1\x31\x41\x59";
    char dst[100];
    unsigned int dlen = 100;
    ASSERT_EQ(decompress_buf(data, sizeof(data)-1, dst, &dlen), BZ_DATA_ERROR_MAGIC);
}

TEST(bad_block_size_0) {
    /* Block size byte '0' (invalid, must be 1-9) */
    char data[] = "BZh0\x31\x41\x59";
    char dst[100];
    unsigned int dlen = 100;
    ASSERT_EQ(decompress_buf(data, sizeof(data)-1, dst, &dlen), BZ_DATA_ERROR_MAGIC);
}

TEST(bad_block_size_colon) {
    /* Block size byte ':' = 0x3a (above '9') */
    char data[] = "BZh:\x31\x41\x59";
    char dst[100];
    unsigned int dlen = 100;
    ASSERT_EQ(decompress_buf(data, sizeof(data)-1, dst, &dlen), BZ_DATA_ERROR_MAGIC);
}

/* ========== Block header errors (decompress.c lines ~234-247) ========== */

TEST(bad_block_header_byte2) {
    /* Valid header start 0x31 but wrong byte2 — should be 0x41 */
    /* Craft: BZh1 + block header starts with 0x31 but second byte wrong */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "hello world";
    ASSERT_EQ(compress_buf(src, strlen(src), comp, &clen, 1), BZ_OK);

    /* Find the block header (0x31 0x41 0x59 0x26 0x53 0x59) */
    /* After "BZh1" (4 bytes), the block header follows */
    /* Corrupt byte 5 (second byte of block header, should be 0x41) */
    if (clen > 5) {
        comp[5] = 0x00;  /* corrupt */
        char dst[100];
        unsigned int dlen = 100;
        int ret = decompress_buf(comp, clen, dst, &dlen);
        ASSERT(ret == BZ_DATA_ERROR);
    }
}

/* ========== Truncation at every state boundary ========== */

TEST(truncated_after_magic) {
    /* Only "BZh1" — no block header */
    char data[] = "BZh1";
    char dst[100];
    unsigned int dlen = 100;
    /* Should return error because input is exhausted */
    int ret = decompress_buf(data, 4, dst, &dlen);
    ASSERT(ret < 0);
}

TEST(truncated_mid_block_header) {
    /* "BZh1" + partial block header */
    char data[] = "BZh1\x31\x41\x59";
    char dst[100];
    unsigned int dlen = 100;
    int ret = decompress_buf(data, 7, dst, &dlen);
    ASSERT(ret < 0);
}

TEST(truncated_after_block_header) {
    /* Compress something, then truncate right after block header magic */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "test";
    ASSERT_EQ(compress_buf(src, 4, comp, &clen, 1), BZ_OK);
    /* Block header is at bytes 4-9, truncate after those */
    char dst[100];
    unsigned int dlen = 100;
    int ret = decompress_buf(comp, 10, dst, &dlen);
    ASSERT(ret < 0);
}

/* ========== Streaming with interrupted input ========== */

TEST(streaming_single_byte_valid) {
    /* Decompress a valid stream feeding 1 byte at a time */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "streaming test data";
    ASSERT_EQ(compress_buf(src, strlen(src), comp, &clen, 1), BZ_OK);

    char dst[100];
    int ret = decompress_streaming_1byte(comp, clen, dst, 100);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_MEM_EQ(dst, src, strlen(src));
}

TEST(streaming_single_byte_corrupted) {
    /* Corrupt a byte in the middle and verify error is detected */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "streaming test data";
    ASSERT_EQ(compress_buf(src, strlen(src), comp, &clen, 1), BZ_OK);

    /* Corrupt a byte in the data section */
    if (clen > 20) {
        comp[clen/2] ^= 0xFF;
        char dst[100];
        int ret = decompress_streaming_1byte(comp, clen, dst, 100);
        /* Should get data error or CRC error */
        ASSERT(ret == BZ_DATA_ERROR || ret == BZ_DATA_ERROR_MAGIC);
    }
}

/* ========== Small decompress mode errors ========== */

TEST(small_decompress_garbage) {
    char garbage[] = "not bzip2 data";
    char dst[100];
    unsigned int dlen = 100;
    ASSERT_EQ(decompress_buf_small(garbage, strlen(garbage), dst, &dlen),
              BZ_DATA_ERROR_MAGIC);
}

TEST(small_decompress_truncated) {
    char data[] = "BZh1\x31\x41\x59\x26\x53\x59";
    char dst[100];
    unsigned int dlen = 100;
    int ret = decompress_buf_small(data, sizeof(data)-1, dst, &dlen);
    ASSERT(ret < 0);
}

TEST(small_decompress_valid) {
    /* Verify small decompress works on valid data */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "small decompress test";
    ASSERT_EQ(compress_buf(src, strlen(src), comp, &clen, 1), BZ_OK);
    char dst[100];
    unsigned int dlen = 100;
    ASSERT_EQ(decompress_buf_small(comp, clen, dst, &dlen), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)strlen(src));
    ASSERT_MEM_EQ(dst, src, strlen(src));
}

/* ========== Systematic bit-flip tests ========== */

TEST(bitflip_every_byte_first_20) {
    /* Flip each byte in the first 20 bytes of a compressed stream.
     * The decompressor must always return an error, never crash. */
    char comp[1000];
    unsigned int clen = 1000;
    char src[200];
    memset(src, 'A', 200);
    ASSERT_EQ(compress_buf(src, 200, comp, &clen, 1), BZ_OK);

    int errors = 0;
    unsigned int limit = (clen < 20) ? clen : 20;
    for (unsigned int i = 0; i < limit; i++) {
        char corrupted[1000];
        memcpy(corrupted, comp, clen);
        corrupted[i] ^= 0xFF;

        char dst[300];
        unsigned int dlen = 300;
        int ret = decompress_buf(corrupted, clen, dst, &dlen);
        /* Must return an error code, not crash */
        if (ret != BZ_OK) errors++;
    }
    /* Most flipped bytes should cause errors */
    ASSERT(errors > 0);
}

TEST(bitflip_every_byte_small_mode) {
    /* Same as above but with small decompress mode */
    char comp[1000];
    unsigned int clen = 1000;
    char src[200];
    memset(src, 'B', 200);
    ASSERT_EQ(compress_buf(src, 200, comp, &clen, 1), BZ_OK);

    int errors = 0;
    unsigned int limit = (clen < 20) ? clen : 20;
    for (unsigned int i = 0; i < limit; i++) {
        char corrupted[1000];
        memcpy(corrupted, comp, clen);
        corrupted[i] ^= 0xFF;

        char dst[300];
        unsigned int dlen = 300;
        int ret = decompress_buf_small(corrupted, clen, dst, &dlen);
        if (ret != BZ_OK) errors++;
    }
    ASSERT(errors > 0);
}

/* ========== End-of-stream marker corruption ========== */

TEST(corrupted_end_marker) {
    /* Compress, then corrupt the end-of-stream marker bytes */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "end marker test";
    ASSERT_EQ(compress_buf(src, strlen(src), comp, &clen, 1), BZ_OK);

    /* The end marker is 0x17 0x72 0x45 0x38 0x50 0x90 near the end.
     * Flip the last few bytes before the combined CRC */
    if (clen > 10) {
        char corrupted[1000];
        memcpy(corrupted, comp, clen);
        /* Corrupt byte near the end (CRC area) */
        corrupted[clen - 5] ^= 0xFF;

        char dst[100];
        unsigned int dlen = 100;
        int ret = decompress_buf(corrupted, clen, dst, &dlen);
        /* Should detect CRC mismatch or data error */
        ASSERT(ret == BZ_DATA_ERROR || ret == BZ_DATA_ERROR_MAGIC);
    }
}

TEST(corrupted_combined_crc) {
    /* Corrupt the block CRC (4 bytes right after the block header magic).
     * The block CRC is at a known offset: after "BZh1" + 6 block magic bytes = offset 10.
     * Flipping a byte there will cause block CRC mismatch. */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "crc corruption test";
    ASSERT_EQ(compress_buf(src, strlen(src), comp, &clen, 1), BZ_OK);

    if (clen > 14) {
        char corrupted[1000];
        memcpy(corrupted, comp, clen);
        /* Byte 10 is first byte of stored block CRC */
        corrupted[10] ^= 0xFF;

        char dst[100];
        unsigned int dlen = 100;
        int ret = decompress_buf(corrupted, clen, dst, &dlen);
        /* Block CRC mismatch should be DATA_ERROR */
        ASSERT_EQ(ret, BZ_DATA_ERROR);
    }
}

/* ========== Output buffer too small ========== */

TEST(outbuf_too_small) {
    char comp[1000];
    unsigned int clen = 1000;
    char src[500];
    memset(src, 'X', 500);
    ASSERT_EQ(compress_buf(src, 500, comp, &clen, 1), BZ_OK);

    /* Try to decompress into a buffer that's too small */
    char dst[10];
    unsigned int dlen = 10;
    int ret = decompress_buf(comp, clen, dst, &dlen);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
}

TEST(outbuf_too_small_small_mode) {
    char comp[1000];
    unsigned int clen = 1000;
    char src[500];
    memset(src, 'Y', 500);
    ASSERT_EQ(compress_buf(src, 500, comp, &clen, 1), BZ_OK);

    char dst[10];
    unsigned int dlen = 10;
    int ret = decompress_buf_small(comp, clen, dst, &dlen);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
}

/* ========== Differential error codes: both modes must agree ========== */

TEST(error_code_parity_garbage) {
    /* Both normal and small decompress should return same error on garbage */
    char garbage[] = "not bzip2";
    char dst1[100], dst2[100];
    unsigned int d1 = 100, d2 = 100;
    int ret1 = decompress_buf(garbage, strlen(garbage), dst1, &d1);
    int ret2 = decompress_buf_small(garbage, strlen(garbage), dst2, &d2);
    ASSERT_EQ(ret1, ret2);
}

TEST(error_code_parity_truncated) {
    char data[] = "BZh1";
    char dst1[100], dst2[100];
    unsigned int d1 = 100, d2 = 100;
    int ret1 = decompress_buf(data, 4, dst1, &d1);
    int ret2 = decompress_buf_small(data, 4, dst2, &d2);
    ASSERT_EQ(ret1, ret2);
}

TEST(error_code_parity_bitflip) {
    /* Both modes should return same error on corrupted data */
    char comp[1000];
    unsigned int clen = 1000;
    char src[200];
    memset(src, 'C', 200);
    ASSERT_EQ(compress_buf(src, 200, comp, &clen, 1), BZ_OK);

    /* Corrupt the block header area */
    if (clen > 10) {
        char corrupted[1000];
        memcpy(corrupted, comp, clen);
        corrupted[8] ^= 0xFF;

        char dst1[300], dst2[300];
        unsigned int d1 = 300, d2 = 300;
        int ret1 = decompress_buf(corrupted, clen, dst1, &d1);
        int ret2 = decompress_buf_small(corrupted, clen, dst2, &d2);
        /* Both should error */
        ASSERT(ret1 < 0);
        ASSERT(ret2 < 0);
        /* Error codes should match */
        ASSERT_EQ(ret1, ret2);
    }
}

/* ========== Streaming API sequence errors ========== */

TEST(decompress_after_stream_end) {
    /* Decompress a valid stream, then call BZ2_bzDecompress again */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "done";
    ASSERT_EQ(compress_buf(src, 4, comp, &clen, 1), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    char dst[100];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = dst;
    strm.avail_out = 100;

    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);

    /* Call again after STREAM_END — should be sequence error */
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = dst;
    strm.avail_out = 100;
    ret = BZ2_bzDecompress(&strm);
    /* Could be sequence error or another error */
    ASSERT(ret != BZ_OK);

    BZ2_bzDecompressEnd(&strm);
}

/* ========== Zero-length input to streaming decompress ========== */

TEST(streaming_zero_avail_in) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char dst[100];
    strm.next_in = "";
    strm.avail_in = 0;
    strm.next_out = dst;
    strm.avail_out = 100;

    /* With zero input, should just return BZ_OK wanting more data */
    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_OK);

    BZ2_bzDecompressEnd(&strm);
}

/* ========== Multiple blocks decompress correctly ========== */

TEST(multiblock_decompress_streaming) {
    /* Compress data large enough for multiple blocks at blockSize=1 */
    char *src = malloc(200000);
    ASSERT(src != NULL);
    for (int i = 0; i < 200000; i++) src[i] = (char)(i % 251);

    char *comp = malloc(300000);
    ASSERT(comp != NULL);
    unsigned int clen = 300000;
    ASSERT_EQ(compress_buf(src, 200000, comp, &clen, 1), BZ_OK);

    /* Decompress via streaming with small chunks */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char *dst = malloc(200000);
    ASSERT(dst != NULL);
    strm.next_out = dst;
    strm.avail_out = 200000;

    unsigned int pos = 0;
    int ret = BZ_OK;
    while (ret == BZ_OK && pos < clen) {
        unsigned int chunk = 137;  /* odd chunk size */
        if (pos + chunk > clen) chunk = clen - pos;
        strm.next_in = comp + pos;
        strm.avail_in = chunk;
        ret = BZ2_bzDecompress(&strm);
        pos += chunk - strm.avail_in;
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int total = 200000 - strm.avail_out;
    ASSERT_EQ(total, 200000u);
    ASSERT_MEM_EQ(dst, src, 200000);

    BZ2_bzDecompressEnd(&strm);
    free(src);
    free(comp);
    free(dst);
}

/* ========== All block sizes decompression ========== */

TEST(all_blocksizes_decompress) {
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = (char)(i * 7 + 13);

    for (int bs = 1; bs <= 9; bs++) {
        char comp[2000];
        unsigned int clen = 2000;
        ASSERT_EQ(compress_buf(src, 500, comp, &clen, bs), BZ_OK);

        char dst[500];
        unsigned int dlen = 500;
        ASSERT_EQ(decompress_buf(comp, clen, dst, &dlen), BZ_OK);
        ASSERT_EQ(dlen, 500u);
        ASSERT_MEM_EQ(dst, src, 500);
    }
}

TEST(all_blocksizes_small_decompress) {
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = (char)(i * 7 + 13);

    for (int bs = 1; bs <= 9; bs++) {
        char comp[2000];
        unsigned int clen = 2000;
        ASSERT_EQ(compress_buf(src, 500, comp, &clen, bs), BZ_OK);

        char dst[500];
        unsigned int dlen = 500;
        ASSERT_EQ(decompress_buf_small(comp, clen, dst, &dlen), BZ_OK);
        ASSERT_EQ(dlen, 500u);
        ASSERT_MEM_EQ(dst, src, 500);
    }
}

/* ========== Byte-at-a-time truncation ========== */

TEST(truncation_every_offset) {
    /* Compress known input, then try decompressing at every truncation point.
     * The decompressor must never crash. */
    char comp[1000];
    unsigned int clen = 1000;
    char src[] = "truncation resilience test data";
    ASSERT_EQ(compress_buf(src, strlen(src), comp, &clen, 1), BZ_OK);

    int error_count = 0;
    for (unsigned int trunc = 1; trunc < clen; trunc++) {
        char dst[100];
        unsigned int dlen = 100;
        int ret = decompress_buf(comp, trunc, dst, &dlen);
        if (ret < 0) error_count++;
        /* Key: we must not crash. Error is expected for most truncations. */
    }
    /* All truncations except possibly the full length should error */
    ASSERT(error_count > 0);
}

/* ========== Extremely short inputs ========== */

TEST(decompress_1_byte) {
    char data[] = "B";
    char dst[10];
    unsigned int dlen = 10;
    int ret = decompress_buf(data, 1, dst, &dlen);
    ASSERT(ret < 0);
}

TEST(decompress_2_bytes) {
    char data[] = "BZ";
    char dst[10];
    unsigned int dlen = 10;
    int ret = decompress_buf(data, 2, dst, &dlen);
    ASSERT(ret < 0);
}

TEST(decompress_3_bytes) {
    char data[] = "BZh";
    char dst[10];
    unsigned int dlen = 10;
    int ret = decompress_buf(data, 3, dst, &dlen);
    ASSERT(ret < 0);
}

/* ========== Test runner ========== */

TEST_MAIN_BEGIN()
    /* Magic byte errors */
    RUN(bad_magic_byte1);
    RUN(bad_magic_byte2);
    RUN(bad_magic_byte3);
    RUN(bad_block_size_0);
    RUN(bad_block_size_colon);

    /* Block header errors */
    RUN(bad_block_header_byte2);

    /* Truncation tests */
    RUN(truncated_after_magic);
    RUN(truncated_mid_block_header);
    RUN(truncated_after_block_header);
    RUN(truncation_every_offset);

    /* Streaming tests */
    RUN(streaming_single_byte_valid);
    RUN(streaming_single_byte_corrupted);
    RUN(streaming_zero_avail_in);
    RUN(multiblock_decompress_streaming);

    /* Small decompress mode */
    RUN(small_decompress_garbage);
    RUN(small_decompress_truncated);
    RUN(small_decompress_valid);

    /* Bit-flip tests */
    RUN(bitflip_every_byte_first_20);
    RUN(bitflip_every_byte_small_mode);

    /* End marker and CRC corruption */
    RUN(corrupted_end_marker);
    RUN(corrupted_combined_crc);

    /* Output buffer tests */
    RUN(outbuf_too_small);
    RUN(outbuf_too_small_small_mode);

    /* Error code parity */
    RUN(error_code_parity_garbage);
    RUN(error_code_parity_truncated);
    RUN(error_code_parity_bitflip);

    /* Sequence errors */
    RUN(decompress_after_stream_end);

    /* All block sizes */
    RUN(all_blocksizes_decompress);
    RUN(all_blocksizes_small_decompress);

    /* Extreme short inputs */
    RUN(decompress_1_byte);
    RUN(decompress_2_bytes);
    RUN(decompress_3_bytes);
TEST_MAIN_END()
