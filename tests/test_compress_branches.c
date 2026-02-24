/*
 * test_compress_branches.c — Tests targeting compress.c branch coverage gaps.
 *
 * Key targets:
 *   - nGroups selection thresholds (2/3/4/5/6 groups based on nMTF count)
 *   - General-case (non-unrolled) code paths when nGroups != 6
 *   - Partial final group (group size < 50)
 *   - Huffman tooLong retry path (skewed frequency distribution)
 *   - bsFinishWrite flush path (various bit alignment endings)
 *   - Verbose mode with different group counts
 *   - Multi-block compression hitting all group count thresholds
 *
 * The compress.c nGroups thresholds are:
 *   nMTF < 200   => 2 groups
 *   nMTF < 600   => 3 groups
 *   nMTF < 1200  => 4 groups
 *   nMTF < 2400  => 5 groups
 *   nMTF >= 2400 => 6 groups
 *
 * nMTF is approximately the number of MTF-encoded symbols, which depends
 * on data entropy and size. We use block size 1 (100KB max) and control
 * input size to hit each threshold.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: compress-decompress round-trip, verify correctness */
static void roundtrip_verify(const char *src, unsigned int srcLen,
                             int blockSize, int verbosity, int workFactor) {
    unsigned int compLen = srcLen + srcLen / 100 + 600 + 10000;
    char *comp = malloc(compLen);
    ASSERT(comp != NULL);
    int ret = BZ2_bzBuffToBuffCompress(comp, &compLen, (char *)src, srcLen,
                                       blockSize, verbosity, workFactor);
    ASSERT_EQ(ret, BZ_OK);

    char *decomp = malloc(srcLen + 100);
    unsigned int decompLen = srcLen + 100;
    ret = BZ2_bzBuffToBuffDecompress(decomp, &decompLen, comp, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(decompLen, srcLen);
    ASSERT_MEM_EQ(decomp, src, srcLen);
    free(comp);
    free(decomp);
}

/* Helper: generate data with controlled entropy.
 * nDistinct controls number of distinct byte values used (1..256).
 * This affects nInUse and thus alphaSize and nMTF. */
static void fill_data(char *buf, unsigned int len, int nDistinct) {
    for (unsigned int i = 0; i < len; i++) {
        buf[i] = (char)(i % nDistinct);
    }
}

/* --- nGroups = 2 (nMTF < 200): very small input --- */

TEST(compress_2_groups_tiny) {
    /* Very small input with few distinct values.
     * ~10 bytes with 2 distinct values -> nMTF ~= 10 (well under 200) */
    char src[10];
    for (int i = 0; i < 10; i++) src[i] = (char)(i % 2);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

TEST(compress_2_groups_small) {
    /* ~50 bytes, low entropy -> nMTF ~50 (under 200).
     * Exercises the non-unrolled general case since nGroups=2. */
    char src[50];
    fill_data(src, sizeof(src), 3);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

TEST(compress_2_groups_edge) {
    /* ~100 bytes, moderate entropy -> nMTF approaching but under 200.
     * Exercises 2-group cost computation, selector, and code emission
     * via the general (non-6-group) code paths. */
    char src[120];
    fill_data(src, sizeof(src), 5);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

/* --- nGroups = 3 (200 <= nMTF < 600) --- */

TEST(compress_3_groups) {
    /* ~300 bytes, moderate entropy -> nMTF ~300 (3 groups).
     * General-case cost/freq/emit code paths (non-unrolled). */
    char src[300];
    fill_data(src, sizeof(src), 20);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

TEST(compress_3_groups_high_entropy) {
    /* Higher entropy pushes nMTF up. */
    char src[400];
    for (int i = 0; i < 400; i++) src[i] = (char)((i * 37 + 13) % 200);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

/* --- nGroups = 4 (600 <= nMTF < 1200) --- */

TEST(compress_4_groups) {
    /* ~800 bytes with high entropy -> nMTF ~800 (4 groups). */
    char src[800];
    for (int i = 0; i < 800; i++) src[i] = (char)((i * 41 + 7) % 256);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

TEST(compress_4_groups_moderate) {
    /* ~700 bytes, medium entropy */
    char src[700];
    fill_data(src, sizeof(src), 100);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

/* --- nGroups = 5 (1200 <= nMTF < 2400) --- */

TEST(compress_5_groups) {
    /* ~1500 bytes with high entropy -> nMTF ~1500 (5 groups). */
    char src[1500];
    for (int i = 0; i < 1500; i++) src[i] = (char)((i * 53 + 11) % 256);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

TEST(compress_5_groups_high) {
    /* ~2000 bytes, high entropy -> nMTF ~2000 (5 groups). */
    char src[2000];
    for (int i = 0; i < 2000; i++) src[i] = (char)((i * 71 + 3) % 256);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

/* --- nGroups = 6 (nMTF >= 2400) --- */

TEST(compress_6_groups) {
    /* ~5000 bytes with high entropy -> nMTF >= 2400 (6 groups).
     * This is the common case; exercises the unrolled fast paths. */
    char src[5000];
    for (int i = 0; i < 5000; i++) src[i] = (char)((i * 97 + 31) % 256);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

TEST(compress_6_groups_partial_final) {
    /* Input sized so the last MTF group has fewer than 50 symbols.
     * This exercises the partial-group fallback in all three loops
     * (cost, freq, emit). nMTF not divisible by 50. */
    char src[3333];
    for (int i = 0; i < 3333; i++) src[i] = (char)((i * 83 + 19) % 256);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

/* --- Verbose mode with non-6 groups --- */

TEST(compress_verbose_2_groups) {
    /* Verbose mode with 2 groups — exercises VPrintf paths in sendMTFValues
     * with nGroups=2, and the initial group partition logging. */
    char src[50];
    fill_data(src, sizeof(src), 3);
    roundtrip_verify(src, sizeof(src), 1, 3, 0);
}

TEST(compress_verbose_4_groups) {
    /* Verbose mode with 4 groups */
    char src[800];
    for (int i = 0; i < 800; i++) src[i] = (char)((i * 41 + 7) % 256);
    roundtrip_verify(src, sizeof(src), 1, 3, 0);
}

/* --- Bitstream alignment --- */

TEST(bsfinish_various_alignments) {
    /* Compress multiple inputs of different sizes to exercise bsFinishWrite
     * with various bit alignment states in the output buffer. */
    for (int size = 1; size <= 20; size++) {
        char src[20];
        for (int i = 0; i < size; i++) src[i] = (char)(i * 3 + size);
        unsigned int compLen = 1000;
        char comp[1000];
        int ret = BZ2_bzBuffToBuffCompress(comp, &compLen, src, size, 1, 0, 0);
        ASSERT_EQ(ret, BZ_OK);

        char dec[20];
        unsigned int decLen = sizeof(dec);
        ret = BZ2_bzBuffToBuffDecompress(dec, &decLen, comp, compLen, 0, 0);
        ASSERT_EQ(ret, BZ_OK);
        ASSERT_EQ(decLen, (unsigned int)size);
        ASSERT_MEM_EQ(dec, src, size);
    }
}

/* --- Work factor boundary values --- */

TEST(compress_workfactor_1) {
    /* workFactor=1 — minimum effort for blocksort fallback.
     * Low workFactor triggers earlier fallback in blocksort. */
    char src[2000];
    for (int i = 0; i < 2000; i++) src[i] = (char)((i * 7) % 256);
    roundtrip_verify(src, sizeof(src), 1, 0, 1);
}

TEST(compress_workfactor_250) {
    /* workFactor=250 — maximum effort. */
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = (char)((i * 13) % 256);
    roundtrip_verify(src, sizeof(src), 1, 0, 250);
}

/* --- Single-byte alphabet (all RUNA/RUNB in MTF) --- */

TEST(compress_single_byte_alphabet) {
    /* All same bytes — MTF produces only RUNA/RUNB symbols.
     * Tests the run-length encoding paths and the 2-symbol alphabet
     * edge case in sendMTFValues. */
    char src[2000];
    memset(src, 'X', sizeof(src));
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

/* --- Two-byte alphabet --- */

TEST(compress_two_byte_alphabet) {
    /* Alternating two bytes — MTF produces mix of 0-runs and symbol 1.
     * alphaSize = 4 (2 bytes + RUNA + RUNB + EOB = 2+2 = 4) */
    char src[1000];
    for (int i = 0; i < 1000; i++) src[i] = (i % 2) ? 'B' : 'A';
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

/* --- All 256 byte values present --- */

TEST(compress_full_alphabet) {
    /* All 256 byte values used — maximizes alphaSize=258 (256+RUNA+EOB).
     * Exercises the inUse16 mapping table with all 16 groups active. */
    char src[1024];
    for (int i = 0; i < 1024; i++) src[i] = (char)(i % 256);
    roundtrip_verify(src, sizeof(src), 1, 0, 0);
}

/* --- Multi-block with different block sizes --- */

TEST(compress_multiblock_bs1) {
    /* Multi-block at blockSize=1 (100KB blocks).
     * Exercises the blockNo > 1 => numZ = 0 path (line 589).
     * Also exercises combinedCRC computation across blocks. */
    unsigned int srcLen = 200000; /* >100KB = 2 blocks at bs=1 */
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++)
        src[i] = (char)((i * 97 + 31) % 256);
    roundtrip_verify(src, srcLen, 1, 0, 0);
    free(src);
}

/* --- Huffman tooLong retry path --- */

TEST(compress_huffman_toolong_retry) {
    /* Create a frequency distribution so skewed that initial Huffman
     * construction exceeds maxLen (17 bits). This triggers the weight
     * halving retry loop in BZ2_hbMakeCodeLengths.
     *
     * Approach: create data where one byte has overwhelming frequency
     * and many other bytes appear exactly once. The resulting Huffman
     * tree will be very deep for the rare symbols. With 258 symbols
     * (256 byte values + RUNA + EOB), the minimum possible max depth
     * is ceil(log2(258)) = 9 bits, but skewed distributions can push
     * rare symbols to 17+ bits.
     *
     * We use a large block (99000 bytes) where:
     *   - 1 byte ('A') appears 98744 times
     *   - 256 other byte values each appear exactly once
     *     (to force all 256 into the alphabet)
     */
    unsigned int srcLen = 99000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);

    /* Fill with dominant byte */
    memset(src, 'A', srcLen);

    /* Insert one of each other byte value at the start */
    for (int i = 0; i < 256; i++) {
        if (i != 'A') {
            src[i] = (char)i;
        }
    }

    roundtrip_verify(src, srcLen, 1, 0, 0);
    free(src);
}

/* --- Selector MTF with deep positions --- */

TEST(compress_selector_mtf_deep) {
    /* Data designed to produce varied Huffman table selections across
     * groups, exercising the selector MTF search loop (lines 448-453)
     * with positions > 0. We want groups that alternate between
     * different coding tables. High entropy data with periodic
     * structure should achieve this. */
    unsigned int srcLen = 10000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);

    /* Create data with 3 distinct regions of different byte distributions */
    for (unsigned int i = 0; i < srcLen; i++) {
        unsigned int region = (i / 100) % 3;
        if (region == 0)
            src[i] = (char)(i % 10);       /* low bytes */
        else if (region == 1)
            src[i] = (char)(120 + (i % 10)); /* mid bytes */
        else
            src[i] = (char)(240 + (i % 10)); /* high bytes */
    }

    roundtrip_verify(src, srcLen, 1, 0, 0);
    free(src);
}

/* --- Streaming compress with various input feed patterns --- */

TEST(compress_streaming_1byte_feed) {
    /* Feed 1 byte at a time to the compressor. This exercises the
     * copy_input_until_stop path with very small avail_in values. */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    char src[200];
    for (int i = 0; i < 200; i++) src[i] = (char)((i * 7) % 50);

    char dest[2000];
    strm.next_out = dest;
    strm.avail_out = sizeof(dest);

    /* Feed 1 byte at a time with BZ_RUN */
    for (int i = 0; i < 199; i++) {
        strm.next_in = &src[i];
        strm.avail_in = 1;
        ret = BZ2_bzCompress(&strm, BZ_RUN);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* Feed last byte with BZ_FINISH */
    strm.next_in = &src[199];
    strm.avail_in = 1;
    while (1) {
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_FINISH_OK);
    }

    unsigned int compLen = (unsigned int)(strm.next_out - dest);
    BZ2_bzCompressEnd(&strm);

    /* Verify decompression */
    char dec[300];
    unsigned int decLen = sizeof(dec);
    ret = BZ2_bzBuffToBuffDecompress(dec, &decLen, dest, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(decLen, 200);
    ASSERT_MEM_EQ(dec, src, 200);
}

/* --- Compress with all block sizes 1-9 --- */

TEST(compress_all_block_sizes) {
    /* Compress with each block size 1-9.
     * Exercises the block size header byte (BZ_HDR_0 + blockSize100k).
     * Also verifies correct combinedCRC across all block sizes. */
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = (char)((i * 23 + 7) % 256);

    for (int bs = 1; bs <= 9; bs++) {
        unsigned int compLen = 2000;
        char comp[2000];
        int ret = BZ2_bzBuffToBuffCompress(comp, &compLen, src, sizeof(src),
                                           bs, 0, 0);
        ASSERT_EQ(ret, BZ_OK);
        ASSERT(compLen > 0);

        /* Verify header byte */
        ASSERT_EQ((unsigned char)comp[3], (unsigned char)('0' + bs));

        /* Round-trip verify */
        char dec[600];
        unsigned int decLen = sizeof(dec);
        ret = BZ2_bzBuffToBuffDecompress(dec, &decLen, comp, compLen, 0, 0);
        ASSERT_EQ(ret, BZ_OK);
        ASSERT_EQ(decLen, sizeof(src));
        ASSERT_MEM_EQ(dec, src, sizeof(src));
    }
}

/* --- Compress exactly at block boundary --- */

TEST(compress_exact_block_boundary) {
    /* Input exactly 100000 bytes at blockSize=1.
     * The block boundary is at 100000 bytes (1 * 100000).
     * This tests whether data exactly fitting one block works
     * correctly vs spilling into a second block. */
    unsigned int srcLen = 100000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++)
        src[i] = (char)((i * 59 + 17) % 256);
    roundtrip_verify(src, srcLen, 1, 0, 0);
    free(src);
}

/* --- Empty block (0 bytes input) --- */

TEST(compress_empty_input) {
    /* Empty input — exercises the is_last_block path with nblock=0.
     * The compressor should emit just the stream header + trailer. */
    char comp[1000];
    unsigned int compLen = sizeof(comp);
    int ret = BZ2_bzBuffToBuffCompress(comp, &compLen, (char *)"", 0,
                                       1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT(compLen > 0);

    char dec[10];
    unsigned int decLen = sizeof(dec);
    ret = BZ2_bzBuffToBuffDecompress(dec, &decLen, comp, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(decLen, 0);
}

TEST_MAIN_BEGIN()
    /* nGroups = 2 tests */
    RUN(compress_2_groups_tiny);
    RUN(compress_2_groups_small);
    RUN(compress_2_groups_edge);

    /* nGroups = 3 tests */
    RUN(compress_3_groups);
    RUN(compress_3_groups_high_entropy);

    /* nGroups = 4 tests */
    RUN(compress_4_groups);
    RUN(compress_4_groups_moderate);

    /* nGroups = 5 tests */
    RUN(compress_5_groups);
    RUN(compress_5_groups_high);

    /* nGroups = 6 tests */
    RUN(compress_6_groups);
    RUN(compress_6_groups_partial_final);

    /* Verbose with non-6 groups */
    RUN(compress_verbose_2_groups);
    RUN(compress_verbose_4_groups);

    /* Bitstream */
    RUN(bsfinish_various_alignments);

    /* Work factor */
    RUN(compress_workfactor_1);
    RUN(compress_workfactor_250);

    /* Alphabet sizes */
    RUN(compress_single_byte_alphabet);
    RUN(compress_two_byte_alphabet);
    RUN(compress_full_alphabet);

    /* Multi-block */
    RUN(compress_multiblock_bs1);

    /* Huffman tooLong retry */
    RUN(compress_huffman_toolong_retry);

    /* Selector MTF */
    RUN(compress_selector_mtf_deep);

    /* Streaming */
    RUN(compress_streaming_1byte_feed);

    /* Block sizes and boundaries */
    RUN(compress_all_block_sizes);
    RUN(compress_exact_block_boundary);
    RUN(compress_empty_input);
TEST_MAIN_END()
