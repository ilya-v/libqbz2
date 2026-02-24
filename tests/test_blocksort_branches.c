/*
 * test_blocksort_branches.c — Tests targeting blocksort.c branch coverage gaps.
 *
 * Since 76996fe, blocksort.c has two paths:
 *   - nblock < 10000: fallbackSort (exponential radix sort)
 *   - nblock >= 10000: sais_bwt (libsais SA-IS)
 *
 * The remaining uncovered branches are primarily in fallbackSort internals:
 *   - fallbackSimpleSort: hi-lo <= 3 vs > 3 threshold
 *   - fallbackQSort3: partitioning branches (ltLo/gtHi, all-equal, etc.)
 *   - fallbackSort bit-vector scanning: WORD_BH fast-skip paths
 *   - BZ2_blockSort: nblock < 10000 threshold boundary
 *
 * Strategy: use small inputs (< 10000 bytes at blockSize=1, which means
 * < ~100 bytes compressed per block) with various data patterns to exercise
 * different sorting branches.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: compress, decompress, verify round-trip correctness */
static void roundtrip(const char *src, unsigned int len, int bs, int wf) {
    unsigned int compLen = len + len / 100 + 600 + 10000;
    char *comp = malloc(compLen);
    ASSERT(comp != NULL);
    int ret = BZ2_bzBuffToBuffCompress(comp, &compLen, (char *)src, len,
                                       bs, 0, wf);
    ASSERT_EQ(ret, BZ_OK);

    char *dec = malloc(len + 100);
    unsigned int decLen = len + 100;
    ret = BZ2_bzBuffToBuffDecompress(dec, &decLen, comp, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(decLen, len);
    ASSERT_MEM_EQ(dec, src, len);
    free(comp);
    free(dec);
}

/* --- Threshold boundary: nblock exactly at the cutoff --- */

TEST(blocksort_threshold_just_below) {
    /* nblock = 9999 -> fallbackSort path.
     * Note: actual nblock depends on initial RLE encoding, so we use
     * high-entropy data where each byte is unique to minimize RLE. */
    unsigned int srcLen = 9999;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++)
        src[i] = (char)((i * 97 + 31) % 256);
    roundtrip(src, srcLen, 1, 0);
    free(src);
}

TEST(blocksort_threshold_just_above) {
    /* nblock = 10000 -> sais_bwt path */
    unsigned int srcLen = 10000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++)
        src[i] = (char)((i * 97 + 31) % 256);
    roundtrip(src, srcLen, 1, 0);
    free(src);
}

/* --- Small blocks exercising fallbackSort deeply --- */

TEST(fallback_very_small_1byte) {
    /* nblock = 1: trivial case */
    char src = 'A';
    roundtrip(&src, 1, 1, 0);
}

TEST(fallback_very_small_2bytes) {
    /* nblock = 2: simplest non-trivial sort */
    char src[2] = {'B', 'A'};
    roundtrip(src, 2, 1, 0);
}

TEST(fallback_very_small_3bytes) {
    /* nblock = 3: edge of fallbackSimpleSort threshold */
    char src[3] = {'C', 'A', 'B'};
    roundtrip(src, 3, 1, 0);
}

TEST(fallback_very_small_4bytes) {
    /* nblock = 4: just at the hi-lo > 3 boundary in fallbackSimpleSort */
    char src[4] = {'D', 'B', 'A', 'C'};
    roundtrip(src, 4, 1, 0);
}

TEST(fallback_very_small_5bytes) {
    /* nblock = 5: exercises the 4-stride insertion sort in fallbackSimpleSort */
    char src[5] = {'E', 'C', 'A', 'D', 'B'};
    roundtrip(src, 5, 1, 0);
}

TEST(fallback_small_10bytes) {
    /* nblock ~10: at FALLBACK_QSORT_SMALL_THRESH boundary */
    char src[10] = {9, 7, 5, 3, 1, 8, 6, 4, 2, 0};
    roundtrip(src, 10, 1, 0);
}

TEST(fallback_small_20bytes) {
    /* nblock ~20: above SMALL_THRESH, exercises fallbackQSort3 */
    char src[20];
    for (int i = 0; i < 20; i++) src[i] = (char)(19 - i);
    roundtrip(src, 20, 1, 0);
}

/* --- Data patterns that stress different fallbackQSort3 branches --- */

TEST(fallback_all_identical) {
    /* All identical bytes: trivial sort, tests the gtHi < ltLo continue */
    char src[100];
    memset(src, 'Z', sizeof(src));
    roundtrip(src, sizeof(src), 1, 0);
}

TEST(fallback_two_values_alternating) {
    /* Two alternating values: creates many equal-class partitions */
    char src[200];
    for (int i = 0; i < 200; i++) src[i] = (i % 2) ? 'A' : 'B';
    roundtrip(src, sizeof(src), 1, 0);
}

TEST(fallback_descending) {
    /* Descending byte values: worst-case for simple insertion sort */
    char src[100];
    for (int i = 0; i < 100; i++) src[i] = (char)(255 - i);
    roundtrip(src, sizeof(src), 1, 0);
}

TEST(fallback_ascending) {
    /* Ascending byte values */
    char src[100];
    for (int i = 0; i < 100; i++) src[i] = (char)i;
    roundtrip(src, sizeof(src), 1, 0);
}

TEST(fallback_repeated_pattern_short) {
    /* Short repeated pattern: exercises deep radix sort levels */
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = "ABCDE"[i % 5];
    roundtrip(src, sizeof(src), 1, 0);
}

TEST(fallback_repeated_pattern_long) {
    /* Longer repeated pattern */
    char src[1000];
    for (int i = 0; i < 1000; i++) src[i] = "HELLO WORLD "[i % 12];
    roundtrip(src, sizeof(src), 1, 0);
}

TEST(fallback_all_256_values) {
    /* All 256 byte values in a small block: maximizes bucket diversity */
    char src[256];
    for (int i = 0; i < 256; i++) src[i] = (char)i;
    roundtrip(src, sizeof(src), 1, 0);
}

TEST(fallback_high_entropy_small) {
    /* High-entropy small block: exercises diverse sorting */
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = (char)((i * 173 + 37) % 256);
    roundtrip(src, sizeof(src), 1, 0);
}

/* --- Blocks near the 10000 threshold --- */

TEST(fallback_near_threshold_9000) {
    /* 9000 bytes: well within fallbackSort range, large block */
    unsigned int srcLen = 9000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++)
        src[i] = (char)((i * 59 + 17) % 256);
    roundtrip(src, srcLen, 1, 0);
    free(src);
}

TEST(fallback_near_threshold_repetitive_9000) {
    /* 9000 bytes of repetitive data: deep fallbackSort with many equal classes */
    unsigned int srcLen = 9000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++)
        src[i] = (char)(i % 10);
    roundtrip(src, srcLen, 1, 0);
    free(src);
}

/* --- Verbose mode with fallback sort --- */

TEST(fallback_verbose_4) {
    /* Verbosity=4 with a small block exercising fallbackSort.
     * Exercises the VPrintf paths for bucket sorting, depth reporting,
     * and block reconstruction. */
    char src[100];
    for (int i = 0; i < 100; i++) src[i] = (char)((i * 7 + 3) % 50);
    unsigned int compLen = 1000;
    char comp[1000];
    int ret = BZ2_bzBuffToBuffCompress(comp, &compLen, src, sizeof(src),
                                       1, 4, 0);
    ASSERT_EQ(ret, BZ_OK);

    char dec[200];
    unsigned int decLen = sizeof(dec);
    ret = BZ2_bzBuffToBuffDecompress(dec, &decLen, comp, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(decLen, sizeof(src));
    ASSERT_MEM_EQ(dec, src, sizeof(src));
}

/* --- sais_bwt path variants --- */

TEST(sais_medium_block) {
    /* 20000 bytes: exercises sais_bwt with a medium block */
    unsigned int srcLen = 20000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++)
        src[i] = (char)((i * 131 + 41) % 256);
    roundtrip(src, srcLen, 1, 0);
    free(src);
}

TEST(sais_repetitive_block) {
    /* 15000 bytes of highly repetitive data: exercises libsais on
     * repetitive input. The doubled-string technique in sais_bwt
     * handles this correctly. */
    unsigned int srcLen = 15000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    for (unsigned int i = 0; i < srcLen; i++)
        src[i] = (char)(i % 3);
    roundtrip(src, srcLen, 1, 0);
    free(src);
}

TEST(sais_single_value_large) {
    /* 12000 bytes of single value: edge case for SA-IS */
    unsigned int srcLen = 12000;
    char *src = malloc(srcLen);
    ASSERT(src != NULL);
    memset(src, 'Q', srcLen);
    roundtrip(src, srcLen, 1, 0);
    free(src);
}

/* --- Small-mode decompression of fallbackSort-produced blocks --- */

TEST(fallback_small_mode_decompress) {
    /* Compress with fallbackSort (small block), decompress with small=1.
     * Exercises the SMALL unRLE path on fallbackSort-produced data. */
    char src[500];
    for (int i = 0; i < 500; i++) src[i] = (char)((i * 43 + 11) % 256);
    unsigned int compLen = 2000;
    char comp[2000];
    int ret = BZ2_bzBuffToBuffCompress(comp, &compLen, src, sizeof(src),
                                       1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);

    char dec[600];
    unsigned int decLen = sizeof(dec);
    ret = BZ2_bzBuffToBuffDecompress(dec, &decLen, comp, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(decLen, sizeof(src));
    ASSERT_MEM_EQ(dec, src, sizeof(src));
}

TEST_MAIN_BEGIN()
    /* Threshold boundary */
    RUN(blocksort_threshold_just_below);
    RUN(blocksort_threshold_just_above);

    /* Very small blocks */
    RUN(fallback_very_small_1byte);
    RUN(fallback_very_small_2bytes);
    RUN(fallback_very_small_3bytes);
    RUN(fallback_very_small_4bytes);
    RUN(fallback_very_small_5bytes);
    RUN(fallback_small_10bytes);
    RUN(fallback_small_20bytes);

    /* Data pattern diversity */
    RUN(fallback_all_identical);
    RUN(fallback_two_values_alternating);
    RUN(fallback_descending);
    RUN(fallback_ascending);
    RUN(fallback_repeated_pattern_short);
    RUN(fallback_repeated_pattern_long);
    RUN(fallback_all_256_values);
    RUN(fallback_high_entropy_small);

    /* Near threshold */
    RUN(fallback_near_threshold_9000);
    RUN(fallback_near_threshold_repetitive_9000);

    /* Verbose */
    RUN(fallback_verbose_4);

    /* sais_bwt variants */
    RUN(sais_medium_block);
    RUN(sais_repetitive_block);
    RUN(sais_single_value_large);

    /* Small-mode decompress */
    RUN(fallback_small_mode_decompress);
TEST_MAIN_END()
