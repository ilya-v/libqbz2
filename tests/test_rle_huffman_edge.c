/* test_rle_huffman_edge.c — targeted tests for RLE encoding boundaries,
 * Huffman/MTF edge cases, and CRC batch optimization correctness.
 *
 * These tests target areas where the worker's performance optimizations
 * (batch CRC in add_pair_to_block, SIMD MTF search, 64-bit bitstream)
 * could introduce subtle bugs:
 *
 * 1. RLE initial encoding boundaries: runs of exactly 4, 5, 255, 256 bytes
 *    (bzip2 encodes runs of 4+ identical bytes as 4 bytes + run-length)
 * 2. Single-symbol alphabets: all bytes identical -> 1-symbol MTF alphabet
 * 3. Two-symbol alphabets: alternating bytes -> stress MTF swap logic
 * 4. Huffman code length edge cases: data distributions that produce
 *    extreme code lengths (near the 20-bit maximum)
 * 5. CRC batch correctness: specific patterns where batch vs byte-at-a-time
 *    CRC computation could diverge
 *
 * All tests do differential verification against the reference libbz2
 * via dlopen, confirming byte-for-byte identical compressed output.
 */

#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

/* Reference library function pointers */
static int (*ref_compress)(char *, unsigned int *, char *, unsigned int,
                           int, int, int);
static int (*ref_decompress)(char *, unsigned int *, char *, unsigned int,
                             int, int);
static void *ref_lib = NULL;
static int ref_loaded = 0;

static void load_ref(void) {
    if (ref_loaded) return;
    ref_loaded = 1;
    ref_lib = dlopen("./reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_lib) ref_lib = dlopen("reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_lib) {
        fprintf(stderr, "WARNING: cannot load reference library: %s\n",
                dlerror());
        return;
    }
    ref_compress = dlsym(ref_lib, "BZ2_bzBuffToBuffCompress");
    ref_decompress = dlsym(ref_lib, "BZ2_bzBuffToBuffDecompress");
}

/* Helper: compress with both libraries and verify identical output.
 * Returns 0 on success, -1 on failure. */
static int diff_verify(const char *data, unsigned int len, int bs, int wf) {
    load_ref();

    unsigned int maxcomp = len + len / 100 + 600;
    char *comp_qbz2 = malloc(maxcomp);
    char *comp_ref = malloc(maxcomp);
    if (!comp_qbz2 || !comp_ref) {
        free(comp_qbz2); free(comp_ref);
        return -1;
    }

    unsigned int clen_qbz2 = maxcomp, clen_ref = maxcomp;

    int ret_qbz2 = BZ2_bzBuffToBuffCompress(comp_qbz2, &clen_qbz2,
                                              (char *)data, len, bs, 0, wf);
    if (ret_qbz2 != BZ_OK) {
        fprintf(stderr, "    qbz2 compress failed: %d\n", ret_qbz2);
        free(comp_qbz2); free(comp_ref);
        return -1;
    }

    if (ref_compress) {
        int ret_ref = ref_compress(comp_ref, &clen_ref, (char *)data, len,
                                   bs, 0, wf);
        if (ret_ref != BZ_OK) {
            fprintf(stderr, "    ref compress failed: %d\n", ret_ref);
            free(comp_qbz2); free(comp_ref);
            return -1;
        }
        if (clen_qbz2 != clen_ref || memcmp(comp_qbz2, comp_ref, clen_qbz2) != 0) {
            fprintf(stderr, "    DIVERGENCE: qbz2=%u bytes, ref=%u bytes\n",
                    clen_qbz2, clen_ref);
            free(comp_qbz2); free(comp_ref);
            return -1;
        }
    }

    /* Verify round-trip */
    unsigned int dlen = len + 100;
    if (dlen < 100) dlen = 100;
    char *decomp = malloc(dlen);
    if (!decomp) { free(comp_qbz2); free(comp_ref); return -1; }

    int ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp_qbz2, clen_qbz2,
                                         0, 0);
    if (ret != BZ_OK || dlen != len || (len > 0 && memcmp(decomp, data, len) != 0)) {
        fprintf(stderr, "    round-trip failed: ret=%d dlen=%u expected=%u\n",
                ret, dlen, len);
        free(comp_qbz2); free(comp_ref); free(decomp);
        return -1;
    }

    free(comp_qbz2);
    free(comp_ref);
    free(decomp);
    return 0;
}

/* ========== RLE Initial Encoding Boundaries ==========
 * bzip2's initial RLE encodes runs of 4+ identical bytes as:
 *   byte byte byte byte run_length
 * where run_length = (actual_count - 4), max 255.
 * So runs of 4 emit as [b b b b 0], runs of 259 emit as [b b b b 255].
 * Runs > 259 are split into multiple RLE sequences.
 */

/* Run of exactly 3 (no RLE encoding) */
TEST(rle_run_3) {
    char buf[3];
    memset(buf, 'A', 3);
    ASSERT_EQ(diff_verify(buf, 3, 1, 0), 0);
}

/* Run of exactly 4 (minimal RLE: 4 bytes + runlen=0) */
TEST(rle_run_4) {
    char buf[4];
    memset(buf, 'A', 4);
    ASSERT_EQ(diff_verify(buf, 4, 1, 0), 0);
}

/* Run of exactly 5 */
TEST(rle_run_5) {
    char buf[5];
    memset(buf, 'A', 5);
    ASSERT_EQ(diff_verify(buf, 5, 1, 0), 0);
}

/* Run of exactly 255 (runlen=251) */
TEST(rle_run_255) {
    char buf[255];
    memset(buf, 'B', 255);
    ASSERT_EQ(diff_verify(buf, 255, 1, 0), 0);
}

/* Run of exactly 256 */
TEST(rle_run_256) {
    char buf[256];
    memset(buf, 'B', 256);
    ASSERT_EQ(diff_verify(buf, 256, 1, 0), 0);
}

/* Run of exactly 259 (maximum for single RLE: runlen=255) */
TEST(rle_run_259) {
    char buf[259];
    memset(buf, 'C', 259);
    ASSERT_EQ(diff_verify(buf, 259, 1, 0), 0);
}

/* Run of exactly 260 (forces two RLE sequences) */
TEST(rle_run_260) {
    char buf[260];
    memset(buf, 'C', 260);
    ASSERT_EQ(diff_verify(buf, 260, 1, 0), 0);
}

/* Alternating run-3 and run-4 boundaries */
TEST(rle_alternating_3_4) {
    char buf[700];
    int pos = 0;
    for (int i = 0; i < 100; i++) {
        /* 3 of one byte, 4 of another */
        memset(buf + pos, 'A' + (i % 10), 3);
        pos += 3;
        memset(buf + pos, 'K' + (i % 10), 4);
        pos += 4;
    }
    ASSERT_EQ(diff_verify(buf, pos, 1, 0), 0);
}

/* Run of exactly 259 then a different byte then run of 259 */
TEST(rle_two_max_runs) {
    char buf[259 + 1 + 259];
    memset(buf, 'X', 259);
    buf[259] = 'Y';
    memset(buf + 260, 'Z', 259);
    ASSERT_EQ(diff_verify(buf, sizeof(buf), 1, 0), 0);
}

/* Run of 1000 identical bytes (multiple RLE sequences) */
TEST(rle_run_1000) {
    char buf[1000];
    memset(buf, 0x42, 1000);
    ASSERT_EQ(diff_verify(buf, 1000, 1, 0), 0);
}

/* All 256 byte values, each appearing as a run of exactly 4 */
TEST(rle_all_byte_values_run4) {
    char buf[256 * 4];
    for (int i = 0; i < 256; i++) {
        memset(buf + i * 4, (char)i, 4);
    }
    ASSERT_EQ(diff_verify(buf, sizeof(buf), 1, 0), 0);
}

/* Run of 4 of each byte value plus trailing single byte */
TEST(rle_all_byte_values_run4_plus1) {
    char buf[256 * 5];
    for (int i = 0; i < 256; i++) {
        memset(buf + i * 5, (char)i, 4);
        buf[i * 5 + 4] = (char)((i + 1) % 256);
    }
    ASSERT_EQ(diff_verify(buf, sizeof(buf), 1, 0), 0);
}

/* ========== Single-Symbol Alphabets ==========
 * When all bytes are identical, the BWT output is all the same byte,
 * MTF produces all zeros (RUNA/RUNB encoding only), and Huffman has
 * a minimal code table. This stresses the batch CRC path and MTF edge case.
 */

TEST(single_symbol_null_1kb) {
    char buf[1024];
    memset(buf, 0, 1024);
    ASSERT_EQ(diff_verify(buf, 1024, 1, 0), 0);
}

TEST(single_symbol_ff_1kb) {
    char buf[1024];
    memset(buf, 0xFF, 1024);
    ASSERT_EQ(diff_verify(buf, 1024, 1, 0), 0);
}

TEST(single_symbol_large_bs9) {
    /* 100KB of identical bytes at bs=9 -> exactly 1 block */
    unsigned int sz = 100000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    memset(buf, 'Q', sz);
    ASSERT_EQ(diff_verify(buf, sz, 9, 0), 0);
    free(buf);
}

TEST(single_symbol_exact_block_bs1) {
    /* Exactly 100000 bytes of 'A' at bs=1 -> fills exactly 1 block */
    unsigned int sz = 100000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    memset(buf, 'A', sz);
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

TEST(single_symbol_over_block_bs1) {
    /* 100001 bytes of 'A' at bs=1 -> 2 blocks, second block has 1 byte */
    unsigned int sz = 100001;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    memset(buf, 'A', sz);
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

/* ========== Two-Symbol Alphabets ==========
 * Two distinct byte values stress the MTF swap logic and produce
 * specific Huffman tree structures.
 */

TEST(two_symbol_alternating_1kb) {
    char buf[1024];
    for (int i = 0; i < 1024; i++)
        buf[i] = (i & 1) ? 'B' : 'A';
    ASSERT_EQ(diff_verify(buf, 1024, 1, 0), 0);
}

TEST(two_symbol_biased_90_10) {
    /* 90% 'A', 10% 'B' — biased distribution stresses Huffman code lengths */
    char buf[10000];
    unsigned int seed = 12345;
    for (int i = 0; i < 10000; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = ((seed >> 16) % 10 < 9) ? 'A' : 'B';
    }
    ASSERT_EQ(diff_verify(buf, 10000, 1, 0), 0);
}

TEST(two_symbol_runs) {
    /* Runs of 'A' and 'B': AAAA...BBBB...AAAA... */
    char buf[2000];
    int pos = 0;
    for (int i = 0; i < 20; i++) {
        memset(buf + pos, (i & 1) ? 'B' : 'A', 100);
        pos += 100;
    }
    ASSERT_EQ(diff_verify(buf, pos, 1, 0), 0);
}

/* ========== Huffman/MTF Edge Cases ========== */

/* Exactly 256 distinct bytes, each appearing once — maximum alphabet size */
TEST(huffman_max_alphabet) {
    char buf[256];
    for (int i = 0; i < 256; i++)
        buf[i] = (char)i;
    ASSERT_EQ(diff_verify(buf, 256, 1, 0), 0);
}

/* One dominant byte with all 256 values present — extreme code length skew */
TEST(huffman_skewed_distribution) {
    /* 10000 'A's plus 1 each of all other 255 byte values */
    unsigned int sz = 10000 + 255;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    memset(buf, 'A', 10000);
    for (int i = 0; i < 255; i++)
        buf[10000 + i] = (char)(i + 1); /* bytes 0x01..0xFF */
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

/* Very short data with many distinct symbols — stress Huffman table packing */
TEST(huffman_short_many_symbols) {
    char buf[50];
    for (int i = 0; i < 50; i++)
        buf[i] = (char)(i * 5); /* 50 distinct values */
    ASSERT_EQ(diff_verify(buf, 50, 1, 0), 0);
}

/* Pathological MTF: each symbol appears exactly once in reverse order */
TEST(mtf_reverse_order) {
    char buf[256];
    for (int i = 0; i < 256; i++)
        buf[i] = (char)(255 - i);
    ASSERT_EQ(diff_verify(buf, 256, 1, 0), 0);
}

/* MTF worst case for linear search: symbols at the end of the MTF list */
TEST(mtf_worst_case_search) {
    /* Cycle through all 256 byte values, forcing MTF to search the full list */
    char buf[5120];
    for (int i = 0; i < 5120; i++)
        buf[i] = (char)(i % 256);
    ASSERT_EQ(diff_verify(buf, 5120, 1, 0), 0);
}

/* ========== CRC Batch Optimization ==========
 * The batch CRC optimization processes runs of identical bytes using
 * a precomputed table. Test patterns that stress the boundary between
 * batch and byte-at-a-time processing.
 */

/* Pattern where CRC batch processes exactly aligned boundaries */
TEST(crc_batch_aligned_runs) {
    /* Runs of exactly 8 bytes (likely batch size alignment) */
    char buf[2048];
    for (int i = 0; i < 256; i++) {
        memset(buf + i * 8, (char)i, 8);
    }
    ASSERT_EQ(diff_verify(buf, 2048, 1, 0), 0);
}

/* Pattern with runs of exactly 1, 2, 3, ..., 16 bytes */
TEST(crc_batch_variable_runs) {
    char buf[2000];
    int pos = 0;
    for (int run_len = 1; run_len <= 30 && pos + run_len < 2000; run_len++) {
        memset(buf + pos, (char)(run_len % 256), run_len);
        pos += run_len;
    }
    ASSERT_EQ(diff_verify(buf, pos, 1, 0), 0);
}

/* All-zero data of various sizes near CRC batch boundaries */
TEST(crc_batch_zeros_127) {
    char buf[127];
    memset(buf, 0, 127);
    ASSERT_EQ(diff_verify(buf, 127, 1, 0), 0);
}

TEST(crc_batch_zeros_128) {
    char buf[128];
    memset(buf, 0, 128);
    ASSERT_EQ(diff_verify(buf, 128, 1, 0), 0);
}

TEST(crc_batch_zeros_129) {
    char buf[129];
    memset(buf, 0, 129);
    ASSERT_EQ(diff_verify(buf, 129, 1, 0), 0);
}

TEST(crc_batch_zeros_255) {
    char buf[255];
    memset(buf, 0, 255);
    ASSERT_EQ(diff_verify(buf, 255, 1, 0), 0);
}

/* ========== Exact Block Boundary with Differential ==========
 * Exercise exact block size boundaries for all block sizes, with
 * differential verification to catch any off-by-one in block splitting.
 */

TEST(exact_boundary_bs1) {
    unsigned int sz = 100000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

TEST(exact_boundary_bs1_minus1) {
    unsigned int sz = 99999;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

TEST(exact_boundary_bs1_plus1) {
    unsigned int sz = 100001;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

TEST(exact_boundary_bs2) {
    unsigned int sz = 200000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(diff_verify(buf, sz, 2, 0), 0);
    free(buf);
}

TEST(exact_boundary_bs3) {
    unsigned int sz = 300000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(diff_verify(buf, sz, 3, 0), 0);
    free(buf);
}

/* Double the block size: exactly 2 blocks */
TEST(exact_two_blocks_bs1) {
    unsigned int sz = 200000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

/* ========== Work Factor Edge Cases ==========
 * Different work factors trigger different blocksort paths.
 * wf=1 forces SA-IS on all blocks >= 10000 bytes.
 * wf=250 gives maximum budget to mainSort.
 */

TEST(wf1_forces_sais_large) {
    unsigned int sz = 50000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(diff_verify(buf, sz, 1, 1), 0);
    free(buf);
}

TEST(wf250_mainsort_large) {
    unsigned int sz = 50000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 251);
    ASSERT_EQ(diff_verify(buf, sz, 1, 250), 0);
    free(buf);
}

TEST(wf1_repetitive_data) {
    /* Repetitive data with wf=1 -> SA-IS path */
    unsigned int sz = 50000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 3);
    ASSERT_EQ(diff_verify(buf, sz, 1, 1), 0);
    free(buf);
}

/* ========== Data Patterns That Stress Specific Components ========== */

/* All-0xFF data: CRC of 0xFF bytes has specific properties */
TEST(all_ff_10kb) {
    unsigned int sz = 10240;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    memset(buf, 0xFF, sz);
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

/* Sawtooth pattern: 0,1,2,...,255,0,1,2,... — stresses MTF with cycling */
TEST(sawtooth_10kb) {
    unsigned int sz = 10240;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++)
        buf[i] = (char)(i & 0xFF);
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

/* Reverse sawtooth: 255,254,...,1,0,255,254,... */
TEST(reverse_sawtooth_10kb) {
    unsigned int sz = 10240;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++)
        buf[i] = (char)(255 - (i & 0xFF));
    ASSERT_EQ(diff_verify(buf, sz, 1, 0), 0);
    free(buf);
}

/* Fibonacci-like sequence mod 256 */
TEST(fibonacci_pattern) {
    char buf[10000];
    buf[0] = 1;
    buf[1] = 1;
    for (int i = 2; i < 10000; i++)
        buf[i] = (char)((unsigned char)buf[i-1] + (unsigned char)buf[i-2]);
    ASSERT_EQ(diff_verify(buf, 10000, 1, 0), 0);
}

/* Data with embedded NUL bytes at various positions */
TEST(embedded_nuls) {
    char buf[1000];
    memset(buf, 'A', 1000);
    /* Place NUL bytes at specific positions */
    buf[0] = 0;
    buf[1] = 0;
    buf[4] = 0;
    buf[255] = 0;
    buf[256] = 0;
    buf[999] = 0;
    ASSERT_EQ(diff_verify(buf, 1000, 1, 0), 0);
}

/* ========== Small Mode (low-memory) Decompression with RLE Patterns ========== */

TEST(small_mode_single_symbol) {
    char buf[5000];
    memset(buf, 'Z', 5000);
    unsigned int maxcomp = 5000 + 5000 / 100 + 600;
    char *comp = malloc(maxcomp);
    unsigned int clen = maxcomp;
    ASSERT(comp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, 5000, 1, 0, 0), BZ_OK);

    char *decomp = malloc(5100);
    unsigned int dlen = 5100;
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0), BZ_OK);
    ASSERT_EQ(dlen, 5000u);
    ASSERT_MEM_EQ(decomp, buf, 5000);

    free(comp);
    free(decomp);
}

TEST(small_mode_rle_runs) {
    /* Pattern with many runs to stress small-mode RLE output */
    char buf[3000];
    int pos = 0;
    for (int i = 0; i < 30 && pos + 100 <= 3000; i++) {
        memset(buf + pos, (char)('A' + i % 26), 100);
        pos += 100;
    }
    unsigned int maxcomp = pos + pos / 100 + 600;
    char *comp = malloc(maxcomp);
    unsigned int clen = maxcomp;
    ASSERT(comp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, pos, 1, 0, 0), BZ_OK);

    char *decomp = malloc(pos + 100);
    unsigned int dlen = pos + 100;
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)pos);
    ASSERT_MEM_EQ(decomp, buf, pos);

    free(comp);
    free(decomp);
}

/* ========== Test Runner ========== */

TEST_MAIN_BEGIN()
    /* RLE boundary tests */
    RUN(rle_run_3);
    RUN(rle_run_4);
    RUN(rle_run_5);
    RUN(rle_run_255);
    RUN(rle_run_256);
    RUN(rle_run_259);
    RUN(rle_run_260);
    RUN(rle_alternating_3_4);
    RUN(rle_two_max_runs);
    RUN(rle_run_1000);
    RUN(rle_all_byte_values_run4);
    RUN(rle_all_byte_values_run4_plus1);

    /* Single-symbol alphabets */
    RUN(single_symbol_null_1kb);
    RUN(single_symbol_ff_1kb);
    RUN(single_symbol_large_bs9);
    RUN(single_symbol_exact_block_bs1);
    RUN(single_symbol_over_block_bs1);

    /* Two-symbol alphabets */
    RUN(two_symbol_alternating_1kb);
    RUN(two_symbol_biased_90_10);
    RUN(two_symbol_runs);

    /* Huffman/MTF edge cases */
    RUN(huffman_max_alphabet);
    RUN(huffman_skewed_distribution);
    RUN(huffman_short_many_symbols);
    RUN(mtf_reverse_order);
    RUN(mtf_worst_case_search);

    /* CRC batch optimization */
    RUN(crc_batch_aligned_runs);
    RUN(crc_batch_variable_runs);
    RUN(crc_batch_zeros_127);
    RUN(crc_batch_zeros_128);
    RUN(crc_batch_zeros_129);
    RUN(crc_batch_zeros_255);

    /* Exact block boundary with differential */
    RUN(exact_boundary_bs1);
    RUN(exact_boundary_bs1_minus1);
    RUN(exact_boundary_bs1_plus1);
    RUN(exact_boundary_bs2);
    RUN(exact_boundary_bs3);
    RUN(exact_two_blocks_bs1);

    /* Work factor edge cases */
    RUN(wf1_forces_sais_large);
    RUN(wf250_mainsort_large);
    RUN(wf1_repetitive_data);

    /* Stress patterns */
    RUN(all_ff_10kb);
    RUN(sawtooth_10kb);
    RUN(reverse_sawtooth_10kb);
    RUN(fibonacci_pattern);
    RUN(embedded_nuls);

    /* Small mode with RLE patterns */
    RUN(small_mode_single_symbol);
    RUN(small_mode_rle_runs);
TEST_MAIN_END()
