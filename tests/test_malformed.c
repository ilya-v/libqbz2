/* test_malformed.c — decompression of structurally malformed bz2 streams
 *
 * These tests construct deliberately corrupted bz2 streams and verify that
 * the decompressor rejects them with the correct error code (BZ_DATA_ERROR
 * or BZ_DATA_ERROR_MAGIC). This ensures the decompressor is resilient to
 * malformed input and does not crash, hang, or produce incorrect output.
 *
 * bz2 stream structure:
 *   Header:  'B' 'Z' 'h' <blocksize '1'-'9'>
 *   Block:   magic 0x314159265359, CRC(4), randbit(1), origPtr(3),
 *            symbol map, nGroups, nSelectors, selectors, code lengths, data
 *   Footer:  magic 0x177245385090, streamCRC(4)
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: compress known data to get a valid bz2 stream */
static int make_valid_stream(const char *data, unsigned int len,
                             char *out, unsigned int *outlen, int bs) {
    return BZ2_bzBuffToBuffCompress(out, outlen, (char *)data, len, bs, 0, 0);
}

/* Helper: try to decompress, return the error code */
static int try_decompress(const char *comp, unsigned int clen,
                          char *out, unsigned int *outlen) {
    return BZ2_bzBuffToBuffDecompress(out, outlen, (char *)comp, clen, 0, 0);
}

/* Helper: try to decompress in small mode */
static int try_decompress_small(const char *comp, unsigned int clen,
                                char *out, unsigned int *outlen) {
    return BZ2_bzBuffToBuffDecompress(out, outlen, (char *)comp, clen, 1, 0);
}

/* Helper: streaming decompress, return error code */
static int try_streaming_decompress(const char *comp, unsigned int clen,
                                    char *out, unsigned int outlen) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK) return ret;

    strm.next_in = (char *)comp;
    strm.avail_in = clen;
    strm.next_out = out;
    strm.avail_out = outlen;

    ret = BZ2_bzDecompress(&strm);
    BZ2_bzDecompressEnd(&strm);
    return ret;
}


/* ================================================================
 * Section 1: Stream header corruption
 * ================================================================ */

/* Empty input — reference libbz2 returns BZ_UNEXPECTED_EOF via BuffToBuffDecompress */
TEST(malformed_empty) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("", 0, out, &outlen), BZ_UNEXPECTED_EOF);
}

/* Single byte — too short for header, reference returns BZ_UNEXPECTED_EOF */
TEST(malformed_1byte) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("B", 1, out, &outlen), BZ_UNEXPECTED_EOF);
}

/* Two bytes — too short for header, reference returns BZ_UNEXPECTED_EOF */
TEST(malformed_2bytes) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("BZ", 2, out, &outlen), BZ_UNEXPECTED_EOF);
}

/* Three bytes (missing block size) — reference returns BZ_UNEXPECTED_EOF */
TEST(malformed_3bytes) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("BZh", 3, out, &outlen), BZ_UNEXPECTED_EOF);
}

/* Wrong first byte */
TEST(malformed_bad_magic_B) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("AZh9", 4, out, &outlen), BZ_DATA_ERROR_MAGIC);
}

/* Wrong second byte */
TEST(malformed_bad_magic_Z) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("BAh9", 4, out, &outlen), BZ_DATA_ERROR_MAGIC);
}

/* Wrong third byte */
TEST(malformed_bad_magic_h) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("BZx9", 4, out, &outlen), BZ_DATA_ERROR_MAGIC);
}

/* Block size '0' (invalid, must be 1-9) */
TEST(malformed_bs_0) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("BZh0", 4, out, &outlen), BZ_DATA_ERROR_MAGIC);
}

/* Block size 'a' (not a digit) */
TEST(malformed_bs_a) {
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress("BZha", 4, out, &outlen), BZ_DATA_ERROR_MAGIC);
}

/* Block size '\0' */
TEST(malformed_bs_null) {
    char data[] = {'B', 'Z', 'h', '\0'};
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress(data, 4, out, &outlen), BZ_DATA_ERROR_MAGIC);
}

/* Valid header but nothing after it */
TEST(malformed_header_only) {
    char out[100];
    unsigned int outlen = 100;
    /* Streaming decompress: header-only should return BZ_OK (need more data)
       or BZ_DATA_ERROR if the stream ends unexpectedly */
    int ret = try_streaming_decompress("BZh9", 4, out, 100);
    /* Either BZ_OK (needs more data) or BZ_DATA_ERROR (unexpected EOF) is valid */
    ASSERT(ret == BZ_OK || ret == BZ_DATA_ERROR);
}


/* ================================================================
 * Section 2: Block header corruption
 *
 * Get a valid stream, then corrupt the block header magic bytes.
 * ================================================================ */

/* Corrupt first byte of block header magic (0x31 -> 0x00) */
TEST(malformed_block_magic_byte1) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    /* Block header starts at byte 4 (after "BZh1") */
    comp[4] = 0x00;  /* Corrupt first block header byte */
    char out[100];
    unsigned int outlen = 100;
    int ret = try_decompress(comp, clen, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_DATA_ERROR_MAGIC);
}

/* Corrupt block magic to look like end-of-stream but with wrong continuation */
TEST(malformed_block_magic_fake_eos) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    /* Replace block magic first byte with end-of-stream marker first byte */
    comp[4] = 0x17;  /* Start of EOS magic */
    comp[5] = 0x00;  /* But rest is garbage */
    char out[100];
    unsigned int outlen = 100;
    int ret = try_decompress(comp, clen, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_DATA_ERROR_MAGIC);
}


/* ================================================================
 * Section 3: Truncation at various points in the stream
 * ================================================================ */

/* Truncate mid-block-header */
TEST(malformed_truncated_after_6bytes) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    char out[100];
    unsigned int outlen = 100;
    /* Keep only header + partial block magic */
    int ret = try_decompress(comp, 6, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_DATA_ERROR_MAGIC || ret == BZ_UNEXPECTED_EOF);
}

/* Truncate after block header but before block data */
TEST(malformed_truncated_after_block_header) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    char out[100];
    unsigned int outlen = 100;
    /* Keep ~half the stream (header + block header + some data) */
    unsigned int half = clen / 2;
    int ret = try_decompress(comp, half, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_UNEXPECTED_EOF);
}

/* Truncate just before end-of-stream CRC */
TEST(malformed_truncated_before_eos_crc) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    char out[100];
    unsigned int outlen = 100;
    /* Remove last 4 bytes (stream CRC) */
    int ret = try_decompress(comp, clen - 4, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_UNEXPECTED_EOF);
}

/* Truncate just 1 byte */
TEST(malformed_truncated_1byte_short) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    char out[100];
    unsigned int outlen = 100;
    int ret = try_decompress(comp, clen - 1, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_UNEXPECTED_EOF);
}


/* ================================================================
 * Section 4: CRC corruption
 * ================================================================ */

/* Corrupt the block CRC (bytes 10-13 in a typical stream) */
TEST(malformed_bad_block_crc) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    /* Block CRC starts after the 6-byte block magic at offset 4, so byte 10 */
    comp[10] ^= 0xFF;
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress(comp, clen, out, &outlen), BZ_DATA_ERROR);
}

/* Corrupt the stream CRC (last 4 bytes before any trailing padding) */
TEST(malformed_bad_stream_crc) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    /* Stream CRC is at the end. Flip a bit in the last few bytes. */
    /* The stream ends with: EOS magic (6 bytes) + CRC (4 bytes) + padding
       but bzip2 packs bits, so the end is byte-aligned after padding.
       Safest: flip a bit near the end. */
    comp[clen - 1] ^= 0x01;
    char out[100];
    unsigned int outlen = 100;
    int ret = try_decompress(comp, clen, out, &outlen);
    /* Should be BZ_DATA_ERROR due to CRC mismatch */
    ASSERT_EQ(ret, BZ_DATA_ERROR);
}


/* ================================================================
 * Section 5: Random garbage data
 * ================================================================ */

/* All zeros */
TEST(malformed_all_zeros) {
    char comp[100];
    memset(comp, 0, 100);
    char out[100];
    unsigned int outlen = 100;
    int ret = try_decompress(comp, 100, out, &outlen);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

/* All 0xFF */
TEST(malformed_all_ff) {
    char comp[100];
    memset(comp, 0xFF, 100);
    char out[100];
    unsigned int outlen = 100;
    int ret = try_decompress(comp, 100, out, &outlen);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

/* PRNG garbage */
TEST(malformed_random_garbage) {
    char comp[200];
    unsigned int seed = 99999;
    for (int i = 0; i < 200; i++) {
        seed = seed * 1103515245 + 12345;
        comp[i] = (char)(seed >> 16);
    }
    char out[200];
    unsigned int outlen = 200;
    int ret = try_decompress(comp, 200, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR_MAGIC || ret == BZ_DATA_ERROR);
}

/* Valid header + random garbage for block data */
TEST(malformed_valid_header_random_body) {
    char comp[200];
    comp[0] = 'B'; comp[1] = 'Z'; comp[2] = 'h'; comp[3] = '1';
    unsigned int seed = 88888;
    for (int i = 4; i < 200; i++) {
        seed = seed * 1103515245 + 12345;
        comp[i] = (char)(seed >> 16);
    }
    char out[1000];
    unsigned int outlen = 1000;
    int ret = try_decompress(comp, 200, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_DATA_ERROR_MAGIC);
}


/* ================================================================
 * Section 6: Bit-flip corruption at various offsets
 * ================================================================ */

/* Flip one bit in each byte of a valid compressed stream */
TEST(malformed_systematic_bitflip) {
    char data[] = "The quick brown fox jumps over the lazy dog";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, (unsigned int)strlen(data), comp, &clen, 1), BZ_OK);

    char corrupted[600];
    char out[100];

    /* Flip bit in every 4th byte, check that we get an error (not a crash) */
    int errors = 0;
    for (unsigned int i = 0; i < clen; i += 4) {
        memcpy(corrupted, comp, clen);
        corrupted[i] ^= 0x80;  /* Flip high bit */
        unsigned int outlen = 100;
        int ret = try_decompress(corrupted, clen, out, &outlen);
        if (ret != BZ_OK) errors++;
        /* The key assertion: we did not crash */
    }
    /* Most bit-flips should cause errors */
    ASSERT(errors > 0);
}


/* ================================================================
 * Section 7: Small mode decompression of malformed data
 * ================================================================ */

/* Same malformed inputs should behave the same in small mode */
TEST(malformed_small_mode_garbage) {
    char comp[100];
    memset(comp, 0xAB, 100);
    char out[100];
    unsigned int outlen = 100;
    int ret = try_decompress_small(comp, 100, out, &outlen);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

TEST(malformed_small_mode_bad_crc) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    comp[10] ^= 0xFF;  /* Corrupt block CRC */
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress_small(comp, clen, out, &outlen), BZ_DATA_ERROR);
}

TEST(malformed_small_mode_truncated) {
    char data[] = "Hello, World!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 13, comp, &clen, 1), BZ_OK);

    char out[100];
    unsigned int outlen = 100;
    int ret = try_decompress_small(comp, clen / 2, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_UNEXPECTED_EOF);
}


/* ================================================================
 * Section 8: Streaming API with malformed data (byte-at-a-time feed)
 * ================================================================ */

/* Feed garbage byte-at-a-time to streaming decompressor */
TEST(malformed_streaming_garbage_byte_at_a_time) {
    char garbage[50];
    unsigned int seed = 77777;
    for (int i = 0; i < 50; i++) {
        seed = seed * 1103515245 + 12345;
        garbage[i] = (char)(seed >> 16);
    }

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char out[100];
    int final_ret = BZ_OK;
    for (int i = 0; i < 50; i++) {
        strm.next_in = &garbage[i];
        strm.avail_in = 1;
        strm.next_out = out;
        strm.avail_out = 100;
        final_ret = BZ2_bzDecompress(&strm);
        if (final_ret != BZ_OK) break;
    }
    BZ2_bzDecompressEnd(&strm);
    /* Should eventually error, not hang */
    ASSERT(final_ret == BZ_DATA_ERROR_MAGIC || final_ret == BZ_DATA_ERROR);
}

/* Feed valid stream byte-at-a-time (should succeed) */
TEST(malformed_streaming_valid_byte_at_a_time) {
    char data[] = "Hello!";
    char comp[600];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 6, comp, &clen, 1), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char out[100];
    strm.next_out = out;
    strm.avail_out = 100;
    int ret = BZ_OK;
    for (unsigned int i = 0; i < clen; i++) {
        strm.next_in = comp + i;
        strm.avail_in = 1;
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int got = 100 - strm.avail_out;
    ASSERT_EQ(got, 6u);
    ASSERT_MEM_EQ(out, data, 6);
    BZ2_bzDecompressEnd(&strm);
}


/* ================================================================
 * Section 9: Multi-block stream with corruption in second block
 * ================================================================ */

/* Corrupt the second block of a multi-block stream */
TEST(malformed_multiblock_corrupt_second) {
    /* Generate multi-block data: >100KB at bs=1 */
    unsigned int sz = 150000;
    char *data = malloc(sz);
    ASSERT(data != NULL);
    unsigned int seed = 66666;
    for (unsigned int i = 0; i < sz; i++) {
        seed = seed * 1103515245 + 12345;
        data[i] = (char)(seed >> 16);
    }

    char *comp = malloc(sz + sz / 100 + 600);
    unsigned int clen = sz + sz / 100 + 600;
    ASSERT(comp != NULL);
    ASSERT_EQ(make_valid_stream(data, sz, comp, &clen, 1), BZ_OK);

    /* Corrupt somewhere in the second half of the compressed data */
    unsigned int corrupt_pos = clen * 2 / 3;
    comp[corrupt_pos] ^= 0xFF;
    comp[corrupt_pos + 1] ^= 0xFF;

    char *out = malloc(sz);
    unsigned int outlen = sz;
    int ret = try_decompress(comp, clen, out, &outlen);
    ASSERT(ret == BZ_DATA_ERROR);

    free(data); free(comp); free(out);
}


/* ================================================================
 * Section 10: Extra trailing data after valid stream
 * ================================================================ */

/* Valid stream with trailing garbage (should succeed — decompress ignores trailing) */
TEST(malformed_trailing_garbage) {
    char data[] = "Hello!";
    char comp[700];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 6, comp, &clen, 1), BZ_OK);

    /* Append garbage after the valid stream */
    memset(comp + clen, 0xAB, 100);

    /* BuffToBuffDecompress should still succeed (only reads one stream) */
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress(comp, clen + 100, out, &outlen), BZ_OK);
    ASSERT_EQ(outlen, 6u);
    ASSERT_MEM_EQ(out, data, 6);
}

/* Valid stream followed by another valid stream's header but truncated */
TEST(malformed_trailing_partial_header) {
    char data[] = "Hello!";
    char comp[700];
    unsigned int clen = 600;
    ASSERT_EQ(make_valid_stream(data, 6, comp, &clen, 1), BZ_OK);

    /* Append partial bz2 header */
    comp[clen] = 'B';
    comp[clen + 1] = 'Z';

    /* BuffToBuffDecompress should still succeed for the first stream */
    char out[100];
    unsigned int outlen = 100;
    ASSERT_EQ(try_decompress(comp, clen + 2, out, &outlen), BZ_OK);
    ASSERT_EQ(outlen, 6u);
}


/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    /* Header corruption */
    RUN(malformed_empty);
    RUN(malformed_1byte);
    RUN(malformed_2bytes);
    RUN(malformed_3bytes);
    RUN(malformed_bad_magic_B);
    RUN(malformed_bad_magic_Z);
    RUN(malformed_bad_magic_h);
    RUN(malformed_bs_0);
    RUN(malformed_bs_a);
    RUN(malformed_bs_null);
    RUN(malformed_header_only);

    /* Block header corruption */
    RUN(malformed_block_magic_byte1);
    RUN(malformed_block_magic_fake_eos);

    /* Truncation */
    RUN(malformed_truncated_after_6bytes);
    RUN(malformed_truncated_after_block_header);
    RUN(malformed_truncated_before_eos_crc);
    RUN(malformed_truncated_1byte_short);

    /* CRC corruption */
    RUN(malformed_bad_block_crc);
    RUN(malformed_bad_stream_crc);

    /* Random garbage */
    RUN(malformed_all_zeros);
    RUN(malformed_all_ff);
    RUN(malformed_random_garbage);
    RUN(malformed_valid_header_random_body);

    /* Systematic bit-flip */
    RUN(malformed_systematic_bitflip);

    /* Small mode */
    RUN(malformed_small_mode_garbage);
    RUN(malformed_small_mode_bad_crc);
    RUN(malformed_small_mode_truncated);

    /* Streaming */
    RUN(malformed_streaming_garbage_byte_at_a_time);
    RUN(malformed_streaming_valid_byte_at_a_time);

    /* Multi-block corruption */
    RUN(malformed_multiblock_corrupt_second);

    /* Trailing data */
    RUN(malformed_trailing_garbage);
    RUN(malformed_trailing_partial_header);
TEST_MAIN_END()
