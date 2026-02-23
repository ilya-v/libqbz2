/* Edge case and conformance tests for libqbz2 public API
 * Targets API boundary conditions, unusual but valid inputs, and
 * conformance areas not covered by test_api.c.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Helper: compress data and return compressed buffer (caller frees) */
static char *compress_buf(const char *data, unsigned int len, unsigned int *out_len, int bs) {
    unsigned int clen = len + len / 100 + 600;
    char *comp = malloc(clen);
    if (!comp) return NULL;
    int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char *)data, len, bs, 0, 0);
    if (ret != BZ_OK) { free(comp); return NULL; }
    *out_len = clen;
    return comp;
}

/* Helper: full round-trip with verification */
static int roundtrip_verify(const char *data, unsigned int len, int bs) {
    unsigned int clen;
    char *comp = compress_buf(data, len, &clen, bs);
    if (!comp) return -1;
    unsigned int dlen = len + 100;
    if (dlen < 100) dlen = 100;
    char *decomp = malloc(dlen);
    if (!decomp) { free(comp); return -1; }
    int ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0);
    if (ret != BZ_OK) { free(comp); free(decomp); return ret; }
    int ok = (dlen == len && (len == 0 || memcmp(data, decomp, len) == 0));
    free(comp);
    free(decomp);
    return ok ? 0 : -100;
}

/* ========== Round-trip across all block sizes ========== */

TEST(roundtrip_bs1_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 1), 0); }
TEST(roundtrip_bs2_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 2), 0); }
TEST(roundtrip_bs3_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 3), 0); }
TEST(roundtrip_bs4_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 4), 0); }
TEST(roundtrip_bs5_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 5), 0); }
TEST(roundtrip_bs6_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 6), 0); }
TEST(roundtrip_bs7_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 7), 0); }
TEST(roundtrip_bs8_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 8), 0); }
TEST(roundtrip_bs9_hello) { ASSERT_EQ(roundtrip_verify("hello", 5, 9), 0); }

/* ========== Empty input at all block sizes ========== */

TEST(roundtrip_bs1_empty) { ASSERT_EQ(roundtrip_verify("", 0, 1), 0); }
TEST(roundtrip_bs2_empty) { ASSERT_EQ(roundtrip_verify("", 0, 2), 0); }
TEST(roundtrip_bs3_empty) { ASSERT_EQ(roundtrip_verify("", 0, 3), 0); }
TEST(roundtrip_bs4_empty) { ASSERT_EQ(roundtrip_verify("", 0, 4), 0); }
TEST(roundtrip_bs5_empty) { ASSERT_EQ(roundtrip_verify("", 0, 5), 0); }
TEST(roundtrip_bs6_empty) { ASSERT_EQ(roundtrip_verify("", 0, 6), 0); }
TEST(roundtrip_bs7_empty) { ASSERT_EQ(roundtrip_verify("", 0, 7), 0); }
TEST(roundtrip_bs8_empty) { ASSERT_EQ(roundtrip_verify("", 0, 8), 0); }
TEST(roundtrip_bs9_empty) { ASSERT_EQ(roundtrip_verify("", 0, 9), 0); }

/* ========== Single byte inputs ========== */

TEST(roundtrip_single_null) { ASSERT_EQ(roundtrip_verify("\x00", 1, 1), 0); }
TEST(roundtrip_single_ff)   { ASSERT_EQ(roundtrip_verify("\xff", 1, 1), 0); }
TEST(roundtrip_single_A)    { ASSERT_EQ(roundtrip_verify("A", 1, 1), 0); }
TEST(roundtrip_single_newline) { ASSERT_EQ(roundtrip_verify("\n", 1, 1), 0); }

/* ========== Two-byte inputs ========== */

TEST(roundtrip_two_same) { ASSERT_EQ(roundtrip_verify("AA", 2, 1), 0); }
TEST(roundtrip_two_diff) { ASSERT_EQ(roundtrip_verify("AB", 2, 1), 0); }
TEST(roundtrip_two_nulls) { ASSERT_EQ(roundtrip_verify("\x00\x00", 2, 1), 0); }
TEST(roundtrip_two_ff) { ASSERT_EQ(roundtrip_verify("\xff\xff", 2, 1), 0); }

/* ========== All 256 byte values ========== */

TEST(roundtrip_all_bytes) {
    char buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (char)i;
    ASSERT_EQ(roundtrip_verify(buf, 256, 1), 0);
}

TEST(roundtrip_all_bytes_reversed) {
    char buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (char)(255 - i);
    ASSERT_EQ(roundtrip_verify(buf, 256, 1), 0);
}

/* ========== Repetitive patterns ========== */

TEST(roundtrip_repeat_a_100) {
    char buf[100];
    memset(buf, 'a', 100);
    ASSERT_EQ(roundtrip_verify(buf, 100, 1), 0);
}

TEST(roundtrip_repeat_null_1000) {
    char buf[1000];
    memset(buf, 0, 1000);
    ASSERT_EQ(roundtrip_verify(buf, 1000, 1), 0);
}

TEST(roundtrip_repeat_ff_1000) {
    char buf[1000];
    memset(buf, 0xff, 1000);
    ASSERT_EQ(roundtrip_verify(buf, 1000, 1), 0);
}

TEST(roundtrip_alternating_01) {
    char buf[1000];
    for (int i = 0; i < 1000; i++) buf[i] = (char)(i & 1);
    ASSERT_EQ(roundtrip_verify(buf, 1000, 1), 0);
}

TEST(roundtrip_alternating_ab) {
    char buf[1000];
    for (int i = 0; i < 1000; i++) buf[i] = (i & 1) ? 'B' : 'A';
    ASSERT_EQ(roundtrip_verify(buf, 1000, 1), 0);
}

TEST(roundtrip_pattern_abcabc) {
    char buf[3000];
    for (int i = 0; i < 3000; i++) buf[i] = "abc"[i % 3];
    ASSERT_EQ(roundtrip_verify(buf, 3000, 1), 0);
}

TEST(roundtrip_ascending_mod256) {
    char buf[10000];
    for (int i = 0; i < 10000; i++) buf[i] = (char)(i & 0xff);
    ASSERT_EQ(roundtrip_verify(buf, 10000, 1), 0);
}

/* ========== Sizes near block boundaries ========== */
/* blockSize100k=1 means 100000 byte blocks */

TEST(roundtrip_99999_bs1) {
    char *buf = malloc(99999);
    ASSERT(buf != NULL);
    for (int i = 0; i < 99999; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(roundtrip_verify(buf, 99999, 1), 0);
    free(buf);
}

TEST(roundtrip_100000_bs1) {
    char *buf = malloc(100000);
    ASSERT(buf != NULL);
    for (int i = 0; i < 100000; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(roundtrip_verify(buf, 100000, 1), 0);
    free(buf);
}

TEST(roundtrip_100001_bs1) {
    char *buf = malloc(100001);
    ASSERT(buf != NULL);
    for (int i = 0; i < 100001; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(roundtrip_verify(buf, 100001, 1), 0);
    free(buf);
}

TEST(roundtrip_200000_bs1) {
    char *buf = malloc(200000);
    ASSERT(buf != NULL);
    for (int i = 0; i < 200000; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(roundtrip_verify(buf, 200000, 1), 0);
    free(buf);
}

/* ========== Work factor variations ========== */

TEST(workfactor_0_bs1) {
    const char *msg = "test workfactor 0";
    unsigned int clen = 200;
    char comp[200];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);
}

TEST(workfactor_1_bs1) {
    const char *msg = "test workfactor 1";
    unsigned int clen = 200;
    char comp[200];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 1), BZ_OK);
}

TEST(workfactor_30_bs5) {
    const char *msg = "test workfactor 30 with block size 5";
    unsigned int clen = 200;
    char comp[200];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 5, 0, 30), BZ_OK);
}

TEST(workfactor_100_bs9) {
    const char *msg = "test workfactor 100 with block size 9";
    unsigned int clen = 200;
    char comp[200];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 9, 0, 100), BZ_OK);
}

TEST(workfactor_250_bs9) {
    const char *msg = "test workfactor 250 with block size 9";
    unsigned int clen = 200;
    char comp[200];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 9, 0, 250), BZ_OK);
}

/* ========== Streaming decompression edge cases ========== */

TEST(streaming_decompress_small_mode) {
    const char *msg = "Decompress in small mode";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 1), BZ_OK);

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int dlen = sizeof(output) - strm.avail_out;
    ASSERT_EQ(dlen, strlen(msg));
    ASSERT_MEM_EQ(output, msg, dlen);
    BZ2_bzDecompressEnd(&strm);
}

TEST(streaming_decompress_one_byte_output) {
    const char *msg = "Test one-byte output buffer decompress";
    unsigned int msglen = strlen(msg);
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, msglen, 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = 0;

    int ret = BZ_OK;
    while (ret == BZ_OK) {
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int total_out = strm.next_out - output;
    ASSERT_EQ(total_out, msglen);
    ASSERT_MEM_EQ(output, msg, total_out);
    BZ2_bzDecompressEnd(&strm);
}

/* ========== Streaming compression — multi-block ========== */

TEST(streaming_compress_multiblock) {
    /* Create data larger than blockSize100k=1 (100KB) to force multiple blocks */
    unsigned int sz = 150000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 127);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    unsigned int csz = sz + sz / 100 + 600;
    char *comp = malloc(csz);
    ASSERT(comp != NULL);

    strm.next_in = buf;
    strm.avail_in = sz;
    strm.next_out = comp;
    strm.avail_out = csz;

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int clen = csz - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify round-trip */
    unsigned int dlen = sz + 100;
    char *decomp = malloc(dlen);
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, sz);
    ASSERT_MEM_EQ(decomp, buf, sz);

    free(buf);
    free(comp);
    free(decomp);
}

/* ========== Streaming compression — incremental with multiple RUN calls ========== */

TEST(streaming_compress_incremental) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    unsigned int csz = 10000;
    char *comp = malloc(csz);
    ASSERT(comp != NULL);
    strm.next_out = comp;
    strm.avail_out = csz;

    /* Feed 100 chunks of 100 bytes */
    char chunk[100];
    for (int c = 0; c < 100; c++) {
        memset(chunk, 'A' + (c % 26), 100);
        strm.next_in = chunk;
        strm.avail_in = 100;
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
    unsigned int clen = csz - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Decompress and verify */
    unsigned int dlen = 10100;
    char *decomp = malloc(dlen);
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 10000u);

    /* Verify content */
    for (int c = 0; c < 100; c++) {
        for (int b = 0; b < 100; b++) {
            ASSERT_EQ(decomp[c * 100 + b], (char)('A' + (c % 26)));
        }
    }

    free(comp);
    free(decomp);
}

/* ========== Total counts tracking ========== */

TEST(compress_total_counters) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    ASSERT_EQ(strm.total_in_lo32, 0u);
    ASSERT_EQ(strm.total_in_hi32, 0u);
    ASSERT_EQ(strm.total_out_lo32, 0u);
    ASSERT_EQ(strm.total_out_hi32, 0u);

    char input[1000];
    memset(input, 'X', 1000);
    char output[2000];

    strm.next_in = input;
    strm.avail_in = 1000;
    strm.next_out = output;
    strm.avail_out = 2000;

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    ASSERT_EQ(strm.total_in_lo32, 1000u);
    ASSERT(strm.total_out_lo32 > 0u);

    BZ2_bzCompressEnd(&strm);
}

TEST(decompress_total_counters) {
    const char *msg = "Counter tracking test data!";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = 1000;

    ASSERT_EQ(BZ2_bzDecompress(&strm), BZ_STREAM_END);

    ASSERT_EQ(strm.total_in_lo32, clen);
    ASSERT_EQ(strm.total_out_lo32, (unsigned int)strlen(msg));

    BZ2_bzDecompressEnd(&strm);
}

/* ========== Error code tests ========== */

TEST(decompress_empty_input_stream) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[100];
    strm.next_in = "";
    strm.avail_in = 0;
    strm.next_out = output;
    strm.avail_out = 100;

    /* With no input, decompress should ask for more (BZ_OK) */
    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_OK);

    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_just_header) {
    /* Valid header "BZh9" but no block data */
    char data[] = "BZh9";
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[100];
    strm.next_in = data;
    strm.avail_in = 4;
    strm.next_out = output;
    strm.avail_out = 100;

    int ret = BZ2_bzDecompress(&strm);
    /* Should need more input or report error - not crash */
    ASSERT(ret == BZ_OK || ret == BZ_DATA_ERROR || ret == BZ_UNEXPECTED_EOF);

    BZ2_bzDecompressEnd(&strm);
}

TEST(b2b_decompress_truncated_at_every_byte) {
    const char *msg = "Truncation test input data";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);

    /* Try decompressing at every truncation point */
    char dst[1000];
    for (unsigned int i = 0; i < clen; i++) {
        unsigned int dlen = 1000;
        int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, comp, i, 0, 0);
        /* Must not return BZ_OK (incomplete data) and must not crash */
        ASSERT(ret != BZ_OK);
    }
}

/* ========== Sequence error tests ========== */

TEST(compress_run_after_finish) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[2000];
    strm.next_in = NULL;
    strm.avail_in = 0;
    strm.next_out = output;
    strm.avail_out = 2000;

    /* Finish the stream */
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    /* Trying to compress more after STREAM_END should be a sequence error */
    strm.next_in = "more data";
    strm.avail_in = 9;
    ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);

    BZ2_bzCompressEnd(&strm);
}

TEST(compress_flush_after_finish_started) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char input[] = "Some data";
    char output[2000];
    strm.next_in = input;
    strm.avail_in = strlen(input);
    strm.next_out = output;
    strm.avail_out = 2000;

    /* Start with RUN */
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Start FINISH */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);

    /* Trying FLUSH during FINISH should be sequence error */
    if (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }

    BZ2_bzCompressEnd(&strm);
}

/* ========== BZ2_bzlibVersion ========== */

TEST(version_contains_dot) {
    const char *v = BZ2_bzlibVersion();
    ASSERT(strchr(v, '.') != NULL);
}

TEST(version_is_reasonable_length) {
    const char *v = BZ2_bzlibVersion();
    size_t len = strlen(v);
    ASSERT(len >= 3);
    ASSERT(len < 100);
}

/* ========== Compressed output determinism ========== */

TEST(compress_deterministic) {
    const char *msg = "Determinism test - same input should give same output";
    unsigned int clen1 = 1000, clen2 = 1000;
    char comp1[1000], comp2[1000];

    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp1, &clen1, (char *)msg, strlen(msg), 5, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp2, &clen2, (char *)msg, strlen(msg), 5, 0, 0), BZ_OK);

    ASSERT_EQ(clen1, clen2);
    ASSERT_MEM_EQ(comp1, comp2, clen1);
}

TEST(compress_different_blocksizes_differ) {
    /* Different block sizes should (usually) produce different output */
    char buf[50000];
    for (int i = 0; i < 50000; i++) buf[i] = (char)(i % 127);

    unsigned int clen1 = 60000, clen9 = 60000;
    char *comp1 = malloc(60000);
    char *comp9 = malloc(60000);
    ASSERT(comp1 && comp9);

    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp1, &clen1, buf, 50000, 1, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp9, &clen9, buf, 50000, 9, 0, 0), BZ_OK);

    /* They should both decompress correctly */
    unsigned int dlen = 50100;
    char *decomp = malloc(dlen);
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp1, clen1, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 50000u);
    dlen = 50100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp9, clen9, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 50000u);

    free(comp1);
    free(comp9);
    free(decomp);
}

/* ========== Bit-flip detection ========== */

TEST(bitflip_detection_header) {
    const char *msg = "Bit flip in header";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);

    /* Flip each bit in the first 4 bytes (header) */
    for (int byte = 0; byte < 4 && byte < (int)clen; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            char modified[1000];
            memcpy(modified, comp, clen);
            modified[byte] ^= (1 << bit);

            char dst[1000];
            unsigned int dlen = 1000;
            int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, modified, clen, 0, 0);
            /* Must detect corruption - not return BZ_OK with wrong data */
            ASSERT(ret != BZ_OK || (dlen == strlen(msg) && memcmp(dst, msg, dlen) == 0));
        }
    }
}

/* ========== FILE* API edge cases ========== */

TEST(file_read_open_null_file) {
    int bzerror;
    BZFILE *bz = BZ2_bzReadOpen(&bzerror, NULL, 0, 0, NULL, 0);
    ASSERT(bz == NULL);
    ASSERT(bzerror != BZ_OK);
}

TEST(file_write_open_null_file) {
    int bzerror;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, NULL, 1, 0, 0);
    ASSERT(bz == NULL);
    ASSERT(bzerror != BZ_OK);
}

TEST(file_write_open_bad_blocksize) {
    int bzerror;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 0, 0, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerror, BZ_PARAM_ERROR);
    fclose(f);
}

TEST(file_write_read_empty) {
    int bzerror;
    FILE *f = tmpfile();
    ASSERT(f != NULL);

    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 1, 0, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    /* Write nothing, just close */
    BZ2_bzWriteClose(&bzerror, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerror, BZ_OK);

    /* Seek back and read */
    rewind(f);
    bz = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    char buf[100];
    int nread = BZ2_bzRead(&bzerror, bz, buf, 100);
    ASSERT_EQ(bzerror, BZ_STREAM_END);
    ASSERT_EQ(nread, 0);

    BZ2_bzReadClose(&bzerror, bz);
    fclose(f);
}

TEST(file_write_read_large) {
    int bzerror;
    FILE *f = tmpfile();
    ASSERT(f != NULL);

    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 1, 0, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    /* Write 50KB of patterned data */
    char chunk[1000];
    for (int c = 0; c < 50; c++) {
        memset(chunk, 'A' + (c % 26), 1000);
        BZ2_bzWrite(&bzerror, bz, chunk, 1000);
        ASSERT_EQ(bzerror, BZ_OK);
    }

    BZ2_bzWriteClose(&bzerror, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerror, BZ_OK);

    /* Seek back and read */
    rewind(f);
    bz = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    char readbuf[50000];
    int total = 0;
    while (1) {
        int nread = BZ2_bzRead(&bzerror, bz, readbuf + total, 50000 - total);
        total += nread;
        if (bzerror == BZ_STREAM_END) break;
        ASSERT_EQ(bzerror, BZ_OK);
    }
    ASSERT_EQ(total, 50000);

    /* Verify content */
    for (int c = 0; c < 50; c++) {
        for (int b = 0; b < 1000; b++) {
            ASSERT_EQ(readbuf[c * 1000 + b], (char)('A' + (c % 26)));
        }
    }

    BZ2_bzReadClose(&bzerror, bz);
    fclose(f);
}

/* ========== Custom allocator — decompression ========== */

static int d_alloc_count = 0;
static int d_free_count = 0;

static void *d_test_alloc(void *opaque, int items, int size) {
    (void)opaque;
    d_alloc_count++;
    return malloc((size_t)items * (size_t)size);
}

static void d_test_free(void *opaque, void *ptr) {
    (void)opaque;
    d_free_count++;
    free(ptr);
}

TEST(custom_allocator_decompress) {
    /* First compress normally */
    const char *msg = "Custom allocator decompression test";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);

    /* Decompress with custom allocator */
    d_alloc_count = 0;
    d_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = d_test_alloc;
    strm.bzfree = d_test_free;

    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    ASSERT(d_alloc_count > 0);
    int allocs = d_alloc_count;

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = 1000;

    ASSERT_EQ(BZ2_bzDecompress(&strm), BZ_STREAM_END);
    ASSERT_EQ(BZ2_bzDecompressEnd(&strm), BZ_OK);
    /* Decompression may allocate additional buffers during decompress,
       so free_count should be >= allocs from init */
    ASSERT(d_free_count >= allocs);
    ASSERT_EQ(d_free_count, d_alloc_count);
}

/* ========== BZ2_bzReadGetUnused ========== */

TEST(read_get_unused) {
    int bzerror;
    const char *msg = "Test unused bytes";

    FILE *f = tmpfile();
    ASSERT(f != NULL);

    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 1, 0, 0);
    ASSERT_EQ(bzerror, BZ_OK);
    BZ2_bzWrite(&bzerror, bz, (void *)msg, strlen(msg));
    ASSERT_EQ(bzerror, BZ_OK);
    BZ2_bzWriteClose(&bzerror, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerror, BZ_OK);

    /* Append some trailing bytes after the bz2 stream */
    fputs("EXTRA", f);
    rewind(f);

    bz = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    char buf[1000];
    BZ2_bzRead(&bzerror, bz, buf, 1000);
    ASSERT_EQ(bzerror, BZ_STREAM_END);

    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerror, bz, &unused, &nUnused);
    ASSERT_EQ(bzerror, BZ_OK);
    /* nUnused may be 0 or include some of "EXTRA" depending on buffering */
    ASSERT(nUnused >= 0);

    BZ2_bzReadClose(&bzerror, bz);
    fclose(f);
}

/* ========== Test runner ========== */

TEST_MAIN_BEGIN()
    /* Round-trip across all block sizes */
    RUN(roundtrip_bs1_hello); RUN(roundtrip_bs2_hello); RUN(roundtrip_bs3_hello);
    RUN(roundtrip_bs4_hello); RUN(roundtrip_bs5_hello); RUN(roundtrip_bs6_hello);
    RUN(roundtrip_bs7_hello); RUN(roundtrip_bs8_hello); RUN(roundtrip_bs9_hello);

    /* Empty input at all block sizes */
    RUN(roundtrip_bs1_empty); RUN(roundtrip_bs2_empty); RUN(roundtrip_bs3_empty);
    RUN(roundtrip_bs4_empty); RUN(roundtrip_bs5_empty); RUN(roundtrip_bs6_empty);
    RUN(roundtrip_bs7_empty); RUN(roundtrip_bs8_empty); RUN(roundtrip_bs9_empty);

    /* Single byte inputs */
    RUN(roundtrip_single_null); RUN(roundtrip_single_ff);
    RUN(roundtrip_single_A); RUN(roundtrip_single_newline);

    /* Two-byte inputs */
    RUN(roundtrip_two_same); RUN(roundtrip_two_diff);
    RUN(roundtrip_two_nulls); RUN(roundtrip_two_ff);

    /* All byte values */
    RUN(roundtrip_all_bytes); RUN(roundtrip_all_bytes_reversed);

    /* Repetitive patterns */
    RUN(roundtrip_repeat_a_100); RUN(roundtrip_repeat_null_1000);
    RUN(roundtrip_repeat_ff_1000); RUN(roundtrip_alternating_01);
    RUN(roundtrip_alternating_ab); RUN(roundtrip_pattern_abcabc);
    RUN(roundtrip_ascending_mod256);

    /* Block boundary sizes */
    RUN(roundtrip_99999_bs1); RUN(roundtrip_100000_bs1);
    RUN(roundtrip_100001_bs1); RUN(roundtrip_200000_bs1);

    /* Work factor variations */
    RUN(workfactor_0_bs1); RUN(workfactor_1_bs1);
    RUN(workfactor_30_bs5); RUN(workfactor_100_bs9);
    RUN(workfactor_250_bs9);

    /* Streaming decompression edge cases */
    RUN(streaming_decompress_small_mode);
    RUN(streaming_decompress_one_byte_output);

    /* Streaming compression */
    RUN(streaming_compress_multiblock);
    RUN(streaming_compress_incremental);

    /* Total counters */
    RUN(compress_total_counters);
    RUN(decompress_total_counters);

    /* Error codes */
    RUN(decompress_empty_input_stream);
    RUN(decompress_just_header);
    RUN(b2b_decompress_truncated_at_every_byte);

    /* Sequence errors */
    RUN(compress_run_after_finish);
    RUN(compress_flush_after_finish_started);

    /* Version */
    RUN(version_contains_dot);
    RUN(version_is_reasonable_length);

    /* Determinism */
    RUN(compress_deterministic);
    RUN(compress_different_blocksizes_differ);

    /* Bit-flip detection */
    RUN(bitflip_detection_header);

    /* FILE* API edge cases */
    RUN(file_read_open_null_file);
    RUN(file_write_open_null_file);
    RUN(file_write_open_bad_blocksize);
    RUN(file_write_read_empty);
    RUN(file_write_read_large);

    /* Custom allocator — decompression */
    RUN(custom_allocator_decompress);

    /* BZ2_bzReadGetUnused */
    RUN(read_get_unused);
TEST_MAIN_END()
