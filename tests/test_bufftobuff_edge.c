/*
 * test_bufftobuff_edge.c — Edge case tests for BZ2_bzBuffToBuffCompress
 * and BZ2_bzBuffToBuffDecompress, plus small-mode decompression paths.
 *
 * Targets coverage gaps in bzlib.c:
 *   - Output overflow (BZ_OUTBUFF_FULL) for both compress and decompress
 *   - Unexpected EOF (BZ_UNEXPECTED_EOF) for decompress
 *   - Small-mode decompression via BuffToBuffDecompress(small=1)
 *   - Parameter validation for both functions
 *   - Byte count correctness on success
 *   - Edge case input sizes (0, 1, maximum)
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: compress data into a heap buffer, return compressed size.
 * Caller must free *out_buf. Returns 0 on failure. */
static unsigned int helper_compress(const char *src, unsigned int srcLen,
                                    char **out_buf, int blockSize100k) {
    unsigned int destLen = srcLen + srcLen / 100 + 600 + 10000;
    *out_buf = malloc(destLen);
    if (!*out_buf) return 0;
    int ret = BZ2_bzBuffToBuffCompress(*out_buf, &destLen,
                                      (char*)src, srcLen,
                                      blockSize100k, 0, 0);
    if (ret != BZ_OK) { free(*out_buf); *out_buf = NULL; return 0; }
    return destLen;
}

/* --- Parameter validation tests --- */

TEST(compress_null_dest) {
    char src[] = "hello";
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffCompress(NULL, &destLen, src, 5, 1, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_null_destlen) {
    char src[] = "hello";
    char dest[100];
    int ret = BZ2_bzBuffToBuffCompress(dest, NULL, src, 5, 1, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_null_source) {
    char dest[100];
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, NULL, 5, 1, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_blocksize_too_low) {
    char src[] = "hello";
    char dest[1000];
    unsigned int destLen = 1000;
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, 5, 0, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_blocksize_too_high) {
    char src[] = "hello";
    char dest[1000];
    unsigned int destLen = 1000;
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, 5, 10, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_verbosity_too_high) {
    char src[] = "hello";
    char dest[1000];
    unsigned int destLen = 1000;
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, 5, 1, 5, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_workfactor_too_high) {
    char src[] = "hello";
    char dest[1000];
    unsigned int destLen = 1000;
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, 5, 1, 0, 251);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_negative_verbosity) {
    char src[] = "hello";
    char dest[1000];
    unsigned int destLen = 1000;
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, 5, 1, -1, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_negative_workfactor) {
    char src[] = "hello";
    char dest[1000];
    unsigned int destLen = 1000;
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, 5, 1, 0, -1);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_null_dest) {
    char src[] = "BZ";
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(NULL, &destLen, src, 2, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_null_destlen) {
    char src[] = "BZ";
    char dest[100];
    int ret = BZ2_bzBuffToBuffDecompress(dest, NULL, src, 2, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_null_source) {
    char dest[100];
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, NULL, 2, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_invalid_small) {
    char src[] = "BZ";
    char dest[100];
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, src, 2, 2, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_verbosity_too_high) {
    char src[] = "BZ";
    char dest[100];
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, src, 2, 0, 5);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_negative_verbosity) {
    char src[] = "BZ";
    char dest[100];
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, src, 2, 0, -1);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

/* --- Output overflow tests --- */

TEST(compress_output_overflow) {
    /* Compress with a destination buffer that is too small */
    char src[500];
    memset(src, 'A', sizeof(src));
    char dest[10]; /* way too small */
    unsigned int destLen = 10;
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, sizeof(src), 1, 0, 0);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
}

TEST(decompress_output_overflow) {
    /* Compress some data, then try to decompress into a too-small buffer */
    char src[1000];
    memset(src, 'X', sizeof(src));
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);
    ASSERT(compressed != NULL);

    /* Try to decompress into a buffer of 1 byte (too small for 1000 bytes output) */
    char dest[1];
    unsigned int destLen = 1;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
    free(compressed);
}

TEST(decompress_output_overflow_small) {
    /* Same test but with small=1 */
    char src[1000];
    memset(src, 'Y', sizeof(src));
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    char dest[1];
    unsigned int destLen = 1;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
    free(compressed);
}

/* --- Unexpected EOF tests --- */

TEST(decompress_unexpected_eof) {
    /* Truncated bzip2 stream — valid header but incomplete data */
    char *compressed = NULL;
    char src[500];
    memset(src, 'Z', sizeof(src));
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 10);

    /* Truncate to just the header + a few bytes */
    unsigned int truncLen = 10;
    char dest[1000];
    unsigned int destLen = 1000;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, truncLen, 0, 0);
    /* Should be BZ_UNEXPECTED_EOF or BZ_DATA_ERROR (depends on where truncation falls) */
    ASSERT(ret == BZ_UNEXPECTED_EOF || ret == BZ_DATA_ERROR);
    free(compressed);
}

TEST(decompress_unexpected_eof_small) {
    /* Same truncation test with small mode */
    char *compressed = NULL;
    char src[500];
    memset(src, 'W', sizeof(src));
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 10);

    unsigned int truncLen = 10;
    char dest[1000];
    unsigned int destLen = 1000;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, truncLen, 1, 0);
    ASSERT(ret == BZ_UNEXPECTED_EOF || ret == BZ_DATA_ERROR);
    free(compressed);
}

/* --- Bad magic tests --- */

TEST(decompress_bad_magic) {
    char src[] = "Not a bzip2 stream at all";
    char dest[100];
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, src, (unsigned)strlen(src), 0, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

TEST(decompress_bad_magic_small) {
    char src[] = "Not a bzip2 stream at all";
    char dest[100];
    unsigned int destLen = 100;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, src, (unsigned)strlen(src), 1, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR_MAGIC);
}

/* --- Small-mode decompression correctness tests --- */

TEST(small_mode_roundtrip_short) {
    /* Compress and decompress with small=1, verify correctness */
    const char *src = "Hello, small mode decompression test!";
    unsigned int srcLen = (unsigned)strlen(src);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, srcLen, &compressed, 1);
    ASSERT(compLen > 0);

    char dest[200];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen, srcLen);
    ASSERT_MEM_EQ(dest, src, srcLen);
    free(compressed);
}

TEST(small_mode_roundtrip_1k) {
    /* 1KB data, small mode */
    char src[1024];
    for (int i = 0; i < 1024; i++) src[i] = (char)(i % 256);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    char dest[2048];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen, sizeof(src));
    ASSERT_MEM_EQ(dest, src, sizeof(src));
    free(compressed);
}

TEST(small_mode_roundtrip_10k) {
    /* 10KB data, small mode — exercises more of the ll16/ll4 code paths */
    unsigned int srcLen = 10240;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++) src[i] = (char)((i * 7 + 13) % 256);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, srcLen, &compressed, 1);
    ASSERT(compLen > 0);

    char *dest = malloc(srcLen + 100);
    ASSERT(dest != NULL);
    unsigned int destLen = srcLen + 100;
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen, srcLen);
    ASSERT_MEM_EQ(dest, src, srcLen);
    free(compressed);
    free(dest);
    free(src);
}

TEST(small_mode_roundtrip_repetitive) {
    /* Repetitive data exercises RLE in small mode */
    char src[2000];
    memset(src, 'R', sizeof(src));
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    char dest[2100];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen, sizeof(src));
    ASSERT_MEM_EQ(dest, src, sizeof(src));
    free(compressed);
}

TEST(small_mode_roundtrip_all_blocksize) {
    /* Test small mode with all block sizes 1-9 */
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = (char)(i & 0xFF);
    unsigned int srcLen = sizeof(src);

    for (int bs = 1; bs <= 9; bs++) {
        char *compressed = NULL;
        unsigned int compLen = helper_compress(src, srcLen, &compressed, bs);
        ASSERT(compLen > 0);

        char dest[600];
        unsigned int destLen = sizeof(dest);
        int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
        ASSERT_EQ(ret, BZ_OK);
        ASSERT_EQ(destLen, srcLen);
        ASSERT_MEM_EQ(dest, src, srcLen);
        free(compressed);
    }
}

/* --- Normal mode (small=0) roundtrip for completeness --- */

TEST(normal_mode_roundtrip_correctness) {
    /* Verify exact byte counts from BuffToBuffCompress/Decompress */
    const char *src = "The quick brown fox jumps over the lazy dog";
    unsigned int srcLen = (unsigned)strlen(src);
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, (char*)src, srcLen, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT(destLen > 0);
    ASSERT(destLen < 1000);

    char out[200];
    unsigned int outLen = sizeof(out);
    ret = BZ2_bzBuffToBuffDecompress(out, &outLen, dest, destLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(outLen, srcLen);
    ASSERT_MEM_EQ(out, src, srcLen);
}

/* --- Exact-fit output buffer tests --- */

TEST(compress_exact_fit) {
    /* Compress, then re-compress with exact destLen to verify it works */
    char src[200];
    for (int i = 0; i < 200; i++) src[i] = (char)(i % 50);
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, sizeof(src), 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    unsigned int exactLen = destLen;

    /* Now compress again with exactly the right size */
    char dest2[1000];
    unsigned int destLen2 = exactLen;
    ret = BZ2_bzBuffToBuffCompress(dest2, &destLen2, src, sizeof(src), 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen2, exactLen);
    ASSERT_MEM_EQ(dest, dest2, exactLen);
}

TEST(decompress_exact_fit) {
    /* Decompress with exactly the right output size */
    char src[200];
    for (int i = 0; i < 200; i++) src[i] = (char)(i % 100);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    char dest[200];
    unsigned int destLen = 200; /* exact output size */
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen, 200);
    ASSERT_MEM_EQ(dest, src, 200);
    free(compressed);
}

/* --- Zero-length input --- */

TEST(compress_zero_length_input) {
    /* Compressing zero bytes should succeed (empty bzip2 stream) */
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, (char*)"", 0, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT(destLen > 0); /* Empty stream still has header + EOS marker */

    /* Decompress it back */
    char out[100];
    unsigned int outLen = sizeof(out);
    ret = BZ2_bzBuffToBuffDecompress(out, &outLen, dest, destLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(outLen, 0);
}

TEST(compress_zero_length_small_decompress) {
    /* Same but decompress in small mode */
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, (char*)"", 0, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    char out[100];
    unsigned int outLen = sizeof(out);
    ret = BZ2_bzBuffToBuffDecompress(out, &outLen, dest, destLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(outLen, 0);
}

/* --- Single byte input --- */

TEST(compress_single_byte) {
    char src = 'A';
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, &src, 1, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT(destLen > 0);

    char out[10];
    unsigned int outLen = sizeof(out);
    ret = BZ2_bzBuffToBuffDecompress(out, &outLen, dest, destLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(outLen, 1);
    ASSERT_EQ(out[0], 'A');
}

TEST(decompress_single_byte_small) {
    char src = 'B';
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, &src, 1, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    char out[10];
    unsigned int outLen = sizeof(out);
    ret = BZ2_bzBuffToBuffDecompress(out, &outLen, dest, destLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(outLen, 1);
    ASSERT_EQ(out[0], 'B');
}

/* --- workFactor=0 (default=30) boundary test --- */

TEST(compress_workfactor_zero_uses_default) {
    /* workFactor=0 should be treated as 30 (the default) */
    char src[200];
    memset(src, 'A', sizeof(src));
    char dest1[1000], dest2[1000];
    unsigned int destLen1 = sizeof(dest1), destLen2 = sizeof(dest2);

    int ret1 = BZ2_bzBuffToBuffCompress(dest1, &destLen1, src, sizeof(src), 1, 0, 0);
    int ret2 = BZ2_bzBuffToBuffCompress(dest2, &destLen2, src, sizeof(src), 1, 0, 30);
    ASSERT_EQ(ret1, BZ_OK);
    ASSERT_EQ(ret2, BZ_OK);
    ASSERT_EQ(destLen1, destLen2);
    ASSERT_MEM_EQ(dest1, dest2, destLen1);
}

/* --- workFactor boundary values --- */

TEST(compress_workfactor_1) {
    char src[200];
    for (int i = 0; i < 200; i++) src[i] = (char)(i % 50);
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, sizeof(src), 1, 0, 1);
    ASSERT_EQ(ret, BZ_OK);
}

TEST(compress_workfactor_250) {
    char src[200];
    for (int i = 0; i < 200; i++) src[i] = (char)(i % 50);
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, sizeof(src), 1, 0, 250);
    ASSERT_EQ(ret, BZ_OK);
}

/* --- Small-mode streaming decompression with BZ2_bzDecompress --- */

TEST(small_mode_streaming_1byte_output) {
    /* Decompress in small mode with 1-byte output buffer at a time */
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = (char)((i * 3 + 17) % 256);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 1); /* small=1 */
    ASSERT_EQ(ret, BZ_OK);

    strm.next_in = compressed;
    strm.avail_in = compLen;

    char out[500];
    unsigned int total = 0;
    while (1) {
        strm.next_out = out + total;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END) {
            total += (1 - strm.avail_out);
            break;
        }
        ASSERT_EQ(ret, BZ_OK);
        total += (1 - strm.avail_out);
        if (total >= sizeof(src)) break;
    }
    ASSERT_EQ(total, sizeof(src));
    ASSERT_MEM_EQ(out, src, sizeof(src));
    BZ2_bzDecompressEnd(&strm);
    free(compressed);
}

TEST(small_mode_streaming_1byte_input) {
    /* Feed compressed data 1 byte at a time, decompress in small mode */
    char src[300];
    for (int i = 0; i < 300; i++) src[i] = (char)((i * 11 + 5) % 256);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 1); /* small=1 */
    ASSERT_EQ(ret, BZ_OK);

    char out[400];
    unsigned int inPos = 0;
    strm.next_out = out;
    strm.avail_out = sizeof(out);

    while (inPos < compLen) {
        strm.next_in = compressed + inPos;
        strm.avail_in = 1;
        inPos++;
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int outLen = sizeof(out) - strm.avail_out;
    ASSERT_EQ(outLen, sizeof(src));
    ASSERT_MEM_EQ(out, src, sizeof(src));
    BZ2_bzDecompressEnd(&strm);
    free(compressed);
}

/* --- Decompress with data that has runs (RLE exercise for small mode) --- */

TEST(small_mode_runs_of_4) {
    /* Data with runs of exactly 4 identical bytes — exercises RLE state_out_len=4 path */
    char src[200];
    int pos = 0;
    for (int i = 0; i < 50 && pos < 196; i++) {
        char c = (char)('A' + (i % 26));
        src[pos++] = c;
        src[pos++] = c;
        src[pos++] = c;
        src[pos++] = c;
    }
    unsigned int srcLen = (unsigned)pos;
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, srcLen, &compressed, 1);
    ASSERT(compLen > 0);

    char dest[300];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen, srcLen);
    ASSERT_MEM_EQ(dest, src, srcLen);
    free(compressed);
}

TEST(small_mode_long_runs) {
    /* Data with long runs (>4) — exercises the RLE count byte path in small mode */
    char src[1000];
    int pos = 0;
    for (int i = 0; i < 10 && pos + 100 <= 1000; i++) {
        char c = (char)('A' + i);
        for (int j = 0; j < 100; j++) src[pos++] = c;
    }
    unsigned int srcLen = (unsigned)pos;
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, srcLen, &compressed, 1);
    ASSERT(compLen > 0);

    char dest[1100];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen, srcLen);
    ASSERT_MEM_EQ(dest, src, srcLen);
    free(compressed);
}

/* --- Differential: compare small=0 and small=1 output --- */

TEST(small_vs_normal_identical_output) {
    /* Both decompression modes must produce identical output */
    char src[2000];
    for (int i = 0; i < 2000; i++) src[i] = (char)((i * 37 + 91) % 256);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    char out_normal[2100];
    unsigned int normLen = sizeof(out_normal);
    int ret = BZ2_bzBuffToBuffDecompress(out_normal, &normLen, compressed, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    char out_small[2100];
    unsigned int smallLen = sizeof(out_small);
    ret = BZ2_bzBuffToBuffDecompress(out_small, &smallLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);

    ASSERT_EQ(normLen, smallLen);
    ASSERT_MEM_EQ(out_normal, out_small, normLen);
    ASSERT_EQ(normLen, sizeof(src));
    ASSERT_MEM_EQ(out_normal, src, sizeof(src));
    free(compressed);
}

/* --- Empty source to decompress --- */

TEST(decompress_empty_source) {
    char dest[100];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, (char*)"", 0, 0, 0);
    /* Empty source -> DATA_ERROR_MAGIC (no valid header) or UNEXPECTED_EOF */
    ASSERT(ret == BZ_DATA_ERROR_MAGIC || ret == BZ_UNEXPECTED_EOF);
}

/* --- Compress and decompress with all block sizes (1-9) --- */

TEST(all_blocksizes_roundtrip) {
    char src[800];
    for (int i = 0; i < 800; i++) src[i] = (char)((i * 13 + 7) % 256);
    for (int bs = 1; bs <= 9; bs++) {
        char dest[2000];
        unsigned int destLen = sizeof(dest);
        int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, sizeof(src), bs, 0, 0);
        ASSERT_EQ(ret, BZ_OK);

        char out[900];
        unsigned int outLen = sizeof(out);
        ret = BZ2_bzBuffToBuffDecompress(out, &outLen, dest, destLen, 0, 0);
        ASSERT_EQ(ret, BZ_OK);
        ASSERT_EQ(outLen, sizeof(src));
        ASSERT_MEM_EQ(out, src, sizeof(src));
    }
}

TEST(all_blocksizes_roundtrip_small) {
    char src[800];
    for (int i = 0; i < 800; i++) src[i] = (char)((i * 13 + 7) % 256);
    for (int bs = 1; bs <= 9; bs++) {
        char dest[2000];
        unsigned int destLen = sizeof(dest);
        int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, sizeof(src), bs, 0, 0);
        ASSERT_EQ(ret, BZ_OK);

        char out[900];
        unsigned int outLen = sizeof(out);
        ret = BZ2_bzBuffToBuffDecompress(out, &outLen, dest, destLen, 1, 0);
        ASSERT_EQ(ret, BZ_OK);
        ASSERT_EQ(outLen, sizeof(src));
        ASSERT_MEM_EQ(out, src, sizeof(src));
    }
}

TEST_MAIN_BEGIN()
    /* Parameter validation */
    RUN(compress_null_dest);
    RUN(compress_null_destlen);
    RUN(compress_null_source);
    RUN(compress_blocksize_too_low);
    RUN(compress_blocksize_too_high);
    RUN(compress_verbosity_too_high);
    RUN(compress_workfactor_too_high);
    RUN(compress_negative_verbosity);
    RUN(compress_negative_workfactor);
    RUN(decompress_null_dest);
    RUN(decompress_null_destlen);
    RUN(decompress_null_source);
    RUN(decompress_invalid_small);
    RUN(decompress_verbosity_too_high);
    RUN(decompress_negative_verbosity);

    /* Output overflow */
    RUN(compress_output_overflow);
    RUN(decompress_output_overflow);
    RUN(decompress_output_overflow_small);

    /* Unexpected EOF / bad data */
    RUN(decompress_unexpected_eof);
    RUN(decompress_unexpected_eof_small);
    RUN(decompress_bad_magic);
    RUN(decompress_bad_magic_small);
    RUN(decompress_empty_source);

    /* Small-mode correctness */
    RUN(small_mode_roundtrip_short);
    RUN(small_mode_roundtrip_1k);
    RUN(small_mode_roundtrip_10k);
    RUN(small_mode_roundtrip_repetitive);
    RUN(small_mode_roundtrip_all_blocksize);
    RUN(small_mode_streaming_1byte_output);
    RUN(small_mode_streaming_1byte_input);
    RUN(small_mode_runs_of_4);
    RUN(small_mode_long_runs);
    RUN(small_vs_normal_identical_output);

    /* Normal mode correctness */
    RUN(normal_mode_roundtrip_correctness);
    RUN(compress_exact_fit);
    RUN(decompress_exact_fit);
    RUN(compress_zero_length_input);
    RUN(compress_zero_length_small_decompress);
    RUN(compress_single_byte);
    RUN(decompress_single_byte_small);
    RUN(compress_workfactor_zero_uses_default);
    RUN(compress_workfactor_1);
    RUN(compress_workfactor_250);

    /* All block sizes */
    RUN(all_blocksizes_roundtrip);
    RUN(all_blocksizes_roundtrip_small);
TEST_MAIN_END()
