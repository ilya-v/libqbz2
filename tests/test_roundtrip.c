/* Systematic round-trip tests for libqbz2
 * Every test compresses then decompresses data, verifying bit-for-bit match.
 * Covers all block sizes, work factors, data patterns, and sizes.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: fill buffer with PRNG data */
static void fill_random(char *buf, unsigned int len, unsigned int seed) {
    for (unsigned int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
}

/* Helper: compress, decompress, verify match. Returns 0 on success, -1 on fail */
static int roundtrip_verify(const char *data, unsigned int len,
                            int blockSize, int workFactor, int small) {
    unsigned int clen = len + len / 100 + 600;
    char *comp = malloc(clen);
    if (!comp) return -1;

    int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char *)data, len,
                                       blockSize, 0, workFactor);
    if (ret != BZ_OK) { free(comp); return -1; }

    unsigned int dlen = len;
    char *decomp = malloc(len > 0 ? len : 1);
    if (!decomp) { free(comp); return -1; }

    ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, small, 0);
    if (ret != BZ_OK) { free(comp); free(decomp); return -1; }
    if (dlen != len) { free(comp); free(decomp); return -1; }
    if (len > 0 && memcmp(decomp, data, len) != 0) {
        free(comp); free(decomp); return -1;
    }

    free(comp);
    free(decomp);
    return 0;
}

/* ========== Fixed data patterns x all block sizes ========== */

TEST(rt_zeros_bs1)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 1, 0, 0), 0); }
TEST(rt_zeros_bs2)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 2, 0, 0), 0); }
TEST(rt_zeros_bs3)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 3, 0, 0), 0); }
TEST(rt_zeros_bs4)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 4, 0, 0), 0); }
TEST(rt_zeros_bs5)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 5, 0, 0), 0); }
TEST(rt_zeros_bs6)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 6, 0, 0), 0); }
TEST(rt_zeros_bs7)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 7, 0, 0), 0); }
TEST(rt_zeros_bs8)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 8, 0, 0), 0); }
TEST(rt_zeros_bs9)  { char d[1000]; memset(d, 0, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 9, 0, 0), 0); }

TEST(rt_0xff_bs1)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 1, 0, 0), 0); }
TEST(rt_0xff_bs2)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 2, 0, 0), 0); }
TEST(rt_0xff_bs3)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 3, 0, 0), 0); }
TEST(rt_0xff_bs4)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 4, 0, 0), 0); }
TEST(rt_0xff_bs5)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 5, 0, 0), 0); }
TEST(rt_0xff_bs6)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 6, 0, 0), 0); }
TEST(rt_0xff_bs7)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 7, 0, 0), 0); }
TEST(rt_0xff_bs8)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 8, 0, 0), 0); }
TEST(rt_0xff_bs9)  { char d[1000]; memset(d, 0xff, 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 9, 0, 0), 0); }

TEST(rt_ascending_bs1) { char d[256]; for (int i = 0; i < 256; i++) d[i] = (char)i; ASSERT_EQ(roundtrip_verify(d, 256, 1, 0, 0), 0); }
TEST(rt_ascending_bs5) { char d[256]; for (int i = 0; i < 256; i++) d[i] = (char)i; ASSERT_EQ(roundtrip_verify(d, 256, 5, 0, 0), 0); }
TEST(rt_ascending_bs9) { char d[256]; for (int i = 0; i < 256; i++) d[i] = (char)i; ASSERT_EQ(roundtrip_verify(d, 256, 9, 0, 0), 0); }

TEST(rt_descending_bs1) { char d[256]; for (int i = 0; i < 256; i++) d[i] = (char)(255-i); ASSERT_EQ(roundtrip_verify(d, 256, 1, 0, 0), 0); }
TEST(rt_descending_bs5) { char d[256]; for (int i = 0; i < 256; i++) d[i] = (char)(255-i); ASSERT_EQ(roundtrip_verify(d, 256, 5, 0, 0), 0); }
TEST(rt_descending_bs9) { char d[256]; for (int i = 0; i < 256; i++) d[i] = (char)(255-i); ASSERT_EQ(roundtrip_verify(d, 256, 9, 0, 0), 0); }

/* ========== Single byte values ========== */

TEST(rt_single_byte_0x00) { char d = 0x00; ASSERT_EQ(roundtrip_verify(&d, 1, 1, 0, 0), 0); }
TEST(rt_single_byte_0x01) { char d = 0x01; ASSERT_EQ(roundtrip_verify(&d, 1, 1, 0, 0), 0); }
TEST(rt_single_byte_0x7f) { char d = 0x7f; ASSERT_EQ(roundtrip_verify(&d, 1, 1, 0, 0), 0); }
TEST(rt_single_byte_0x80) { char d = (char)0x80; ASSERT_EQ(roundtrip_verify(&d, 1, 1, 0, 0), 0); }
TEST(rt_single_byte_0xfe) { char d = (char)0xfe; ASSERT_EQ(roundtrip_verify(&d, 1, 1, 0, 0), 0); }
TEST(rt_single_byte_0xff) { char d = (char)0xff; ASSERT_EQ(roundtrip_verify(&d, 1, 1, 0, 0), 0); }

/* ========== Exact sizes that trigger block boundaries at bs=1 (100000 bytes) ========== */

TEST(rt_blockboundary_99999) {
    char *d = malloc(99999); ASSERT(d != NULL);
    fill_random(d, 99999, 1001);
    ASSERT_EQ(roundtrip_verify(d, 99999, 1, 0, 0), 0);
    free(d);
}

TEST(rt_blockboundary_100000) {
    char *d = malloc(100000); ASSERT(d != NULL);
    fill_random(d, 100000, 1002);
    ASSERT_EQ(roundtrip_verify(d, 100000, 1, 0, 0), 0);
    free(d);
}

TEST(rt_blockboundary_100001) {
    char *d = malloc(100001); ASSERT(d != NULL);
    fill_random(d, 100001, 1003);
    ASSERT_EQ(roundtrip_verify(d, 100001, 1, 0, 0), 0);
    free(d);
}

TEST(rt_blockboundary_200000) {
    char *d = malloc(200000); ASSERT(d != NULL);
    fill_random(d, 200000, 1004);
    ASSERT_EQ(roundtrip_verify(d, 200000, 1, 0, 0), 0);
    free(d);
}

/* Block boundary at bs=9 (900000 bytes) */
TEST(rt_bs9_boundary_899999) {
    char *d = malloc(899999); ASSERT(d != NULL);
    fill_random(d, 899999, 2001);
    ASSERT_EQ(roundtrip_verify(d, 899999, 9, 0, 0), 0);
    free(d);
}

TEST(rt_bs9_boundary_900000) {
    char *d = malloc(900000); ASSERT(d != NULL);
    fill_random(d, 900000, 2002);
    ASSERT_EQ(roundtrip_verify(d, 900000, 9, 0, 0), 0);
    free(d);
}

TEST(rt_bs9_boundary_900001) {
    char *d = malloc(900001); ASSERT(d != NULL);
    fill_random(d, 900001, 2003);
    ASSERT_EQ(roundtrip_verify(d, 900001, 9, 0, 0), 0);
    free(d);
}

/* ========== Small mode roundtrip for each block size ========== */

TEST(rt_small_bs1) { char d[5000]; fill_random(d, 5000, 3001); ASSERT_EQ(roundtrip_verify(d, 5000, 1, 0, 1), 0); }
TEST(rt_small_bs2) { char d[5000]; fill_random(d, 5000, 3002); ASSERT_EQ(roundtrip_verify(d, 5000, 2, 0, 1), 0); }
TEST(rt_small_bs3) { char d[5000]; fill_random(d, 5000, 3003); ASSERT_EQ(roundtrip_verify(d, 5000, 3, 0, 1), 0); }
TEST(rt_small_bs4) { char d[5000]; fill_random(d, 5000, 3004); ASSERT_EQ(roundtrip_verify(d, 5000, 4, 0, 1), 0); }
TEST(rt_small_bs5) { char d[5000]; fill_random(d, 5000, 3005); ASSERT_EQ(roundtrip_verify(d, 5000, 5, 0, 1), 0); }
TEST(rt_small_bs6) { char d[5000]; fill_random(d, 5000, 3006); ASSERT_EQ(roundtrip_verify(d, 5000, 6, 0, 1), 0); }
TEST(rt_small_bs7) { char d[5000]; fill_random(d, 5000, 3007); ASSERT_EQ(roundtrip_verify(d, 5000, 7, 0, 1), 0); }
TEST(rt_small_bs8) { char d[5000]; fill_random(d, 5000, 3008); ASSERT_EQ(roundtrip_verify(d, 5000, 8, 0, 1), 0); }
TEST(rt_small_bs9) { char d[5000]; fill_random(d, 5000, 3009); ASSERT_EQ(roundtrip_verify(d, 5000, 9, 0, 1), 0); }

/* ========== Work factor variations per block size ========== */

TEST(rt_wf0_bs1)   { char d[2000]; fill_random(d, 2000, 4001); ASSERT_EQ(roundtrip_verify(d, 2000, 1, 0, 0), 0); }
TEST(rt_wf1_bs1)   { char d[2000]; fill_random(d, 2000, 4002); ASSERT_EQ(roundtrip_verify(d, 2000, 1, 1, 0), 0); }
TEST(rt_wf50_bs1)  { char d[2000]; fill_random(d, 2000, 4003); ASSERT_EQ(roundtrip_verify(d, 2000, 1, 50, 0), 0); }
TEST(rt_wf100_bs1) { char d[2000]; fill_random(d, 2000, 4004); ASSERT_EQ(roundtrip_verify(d, 2000, 1, 100, 0), 0); }
TEST(rt_wf250_bs1) { char d[2000]; fill_random(d, 2000, 4005); ASSERT_EQ(roundtrip_verify(d, 2000, 1, 250, 0), 0); }

TEST(rt_wf0_bs5)   { char d[2000]; fill_random(d, 2000, 4011); ASSERT_EQ(roundtrip_verify(d, 2000, 5, 0, 0), 0); }
TEST(rt_wf1_bs5)   { char d[2000]; fill_random(d, 2000, 4012); ASSERT_EQ(roundtrip_verify(d, 2000, 5, 1, 0), 0); }
TEST(rt_wf50_bs5)  { char d[2000]; fill_random(d, 2000, 4013); ASSERT_EQ(roundtrip_verify(d, 2000, 5, 50, 0), 0); }
TEST(rt_wf100_bs5) { char d[2000]; fill_random(d, 2000, 4014); ASSERT_EQ(roundtrip_verify(d, 2000, 5, 100, 0), 0); }
TEST(rt_wf250_bs5) { char d[2000]; fill_random(d, 2000, 4015); ASSERT_EQ(roundtrip_verify(d, 2000, 5, 250, 0), 0); }

TEST(rt_wf0_bs9)   { char d[2000]; fill_random(d, 2000, 4021); ASSERT_EQ(roundtrip_verify(d, 2000, 9, 0, 0), 0); }
TEST(rt_wf1_bs9)   { char d[2000]; fill_random(d, 2000, 4022); ASSERT_EQ(roundtrip_verify(d, 2000, 9, 1, 0), 0); }
TEST(rt_wf50_bs9)  { char d[2000]; fill_random(d, 2000, 4023); ASSERT_EQ(roundtrip_verify(d, 2000, 9, 50, 0), 0); }
TEST(rt_wf100_bs9) { char d[2000]; fill_random(d, 2000, 4024); ASSERT_EQ(roundtrip_verify(d, 2000, 9, 100, 0), 0); }
TEST(rt_wf250_bs9) { char d[2000]; fill_random(d, 2000, 4025); ASSERT_EQ(roundtrip_verify(d, 2000, 9, 250, 0), 0); }

/* ========== Various data sizes ========== */

TEST(rt_size_1)       { char d; fill_random(&d, 1, 5001); ASSERT_EQ(roundtrip_verify(&d, 1, 1, 0, 0), 0); }
TEST(rt_size_2)       { char d[2]; fill_random(d, 2, 5002); ASSERT_EQ(roundtrip_verify(d, 2, 1, 0, 0), 0); }
TEST(rt_size_3)       { char d[3]; fill_random(d, 3, 5003); ASSERT_EQ(roundtrip_verify(d, 3, 1, 0, 0), 0); }
TEST(rt_size_4)       { char d[4]; fill_random(d, 4, 5004); ASSERT_EQ(roundtrip_verify(d, 4, 1, 0, 0), 0); }
TEST(rt_size_5)       { char d[5]; fill_random(d, 5, 5005); ASSERT_EQ(roundtrip_verify(d, 5, 1, 0, 0), 0); }
TEST(rt_size_10)      { char d[10]; fill_random(d, 10, 5010); ASSERT_EQ(roundtrip_verify(d, 10, 1, 0, 0), 0); }
TEST(rt_size_100)     { char d[100]; fill_random(d, 100, 5100); ASSERT_EQ(roundtrip_verify(d, 100, 1, 0, 0), 0); }
TEST(rt_size_255)     { char d[255]; fill_random(d, 255, 5255); ASSERT_EQ(roundtrip_verify(d, 255, 1, 0, 0), 0); }
TEST(rt_size_256)     { char d[256]; fill_random(d, 256, 5256); ASSERT_EQ(roundtrip_verify(d, 256, 1, 0, 0), 0); }
TEST(rt_size_257)     { char d[257]; fill_random(d, 257, 5257); ASSERT_EQ(roundtrip_verify(d, 257, 1, 0, 0), 0); }
TEST(rt_size_1000)    { char d[1000]; fill_random(d, 1000, 6000); ASSERT_EQ(roundtrip_verify(d, 1000, 1, 0, 0), 0); }
TEST(rt_size_4096)    { char d[4096]; fill_random(d, 4096, 6096); ASSERT_EQ(roundtrip_verify(d, 4096, 1, 0, 0), 0); }
TEST(rt_size_10000)   { char d[10000]; fill_random(d, 10000, 7000); ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0); }
TEST(rt_size_65535)   { char *d = malloc(65535); ASSERT(d); fill_random(d, 65535, 7535); ASSERT_EQ(roundtrip_verify(d, 65535, 1, 0, 0), 0); free(d); }
TEST(rt_size_65536)   { char *d = malloc(65536); ASSERT(d); fill_random(d, 65536, 7536); ASSERT_EQ(roundtrip_verify(d, 65536, 1, 0, 0), 0); free(d); }

/* ========== RLE-triggering patterns (runs of 4+ identical bytes) ========== */

TEST(rt_rle_4bytes)   { char d[4]; memset(d, 'A', 4); ASSERT_EQ(roundtrip_verify(d, 4, 1, 0, 0), 0); }
TEST(rt_rle_5bytes)   { char d[5]; memset(d, 'A', 5); ASSERT_EQ(roundtrip_verify(d, 5, 1, 0, 0), 0); }
TEST(rt_rle_255bytes) { char d[255]; memset(d, 'X', 255); ASSERT_EQ(roundtrip_verify(d, 255, 1, 0, 0), 0); }
TEST(rt_rle_256bytes) { char d[256]; memset(d, 'X', 256); ASSERT_EQ(roundtrip_verify(d, 256, 1, 0, 0), 0); }
TEST(rt_rle_257bytes) { char d[257]; memset(d, 'X', 257); ASSERT_EQ(roundtrip_verify(d, 257, 1, 0, 0), 0); }
TEST(rt_rle_258bytes) { char d[258]; memset(d, 'X', 258); ASSERT_EQ(roundtrip_verify(d, 258, 1, 0, 0), 0); }
TEST(rt_rle_259bytes) { char d[259]; memset(d, 'X', 259); ASSERT_EQ(roundtrip_verify(d, 259, 1, 0, 0), 0); }
TEST(rt_rle_1000bytes) { char d[1000]; memset(d, 'Y', 1000); ASSERT_EQ(roundtrip_verify(d, 1000, 1, 0, 0), 0); }

/* Alternating runs */
TEST(rt_rle_alternating_4) {
    char d[800];
    for (int i = 0; i < 800; i += 8) { memset(d+i, 'A', 4); memset(d+i+4, 'B', 4); }
    ASSERT_EQ(roundtrip_verify(d, 800, 1, 0, 0), 0);
}

TEST(rt_rle_alternating_5) {
    char d[1000];
    for (int i = 0; i < 1000; i += 10) { memset(d+i, 'C', 5); memset(d+i+5, 'D', 5); }
    ASSERT_EQ(roundtrip_verify(d, 1000, 1, 0, 0), 0);
}

/* Increasing run lengths */
TEST(rt_rle_increasing) {
    char d[10000];
    int pos = 0;
    for (int len = 1; len <= 100 && pos + len <= 10000; len++) {
        memset(d + pos, (char)(len & 0xff), len);
        pos += len;
    }
    if (pos < 10000) memset(d + pos, 0, 10000 - pos);
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
}

/* Long run (exercises RLE run-length encoding limit at 255) */
TEST(rt_rle_long_run_254)  { char d[254]; memset(d, 'Z', 254); ASSERT_EQ(roundtrip_verify(d, 254, 1, 0, 0), 0); }
TEST(rt_rle_long_run_258)  { char d[258]; memset(d, 'Z', 258); ASSERT_EQ(roundtrip_verify(d, 258, 1, 0, 0), 0); }
TEST(rt_rle_long_run_259)  { char d[259]; memset(d, 'Z', 259); ASSERT_EQ(roundtrip_verify(d, 259, 1, 0, 0), 0); }
TEST(rt_rle_long_run_260)  { char d[260]; memset(d, 'Z', 260); ASSERT_EQ(roundtrip_verify(d, 260, 1, 0, 0), 0); }
TEST(rt_rle_long_run_512)  { char d[512]; memset(d, 'Z', 512); ASSERT_EQ(roundtrip_verify(d, 512, 1, 0, 0), 0); }

/* ========== Specific byte patterns that stress BWT/MTF/Huffman ========== */

TEST(rt_single_symbol_repeated) {
    /* Only one unique byte value, stresses Huffman with 1-symbol alphabet */
    char d[10000]; memset(d, 42, 10000);
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
}

TEST(rt_two_symbol_alternating) {
    char d[10000];
    for (int i = 0; i < 10000; i++) d[i] = (i & 1) ? 'B' : 'A';
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
}

TEST(rt_all_256_symbols) {
    /* Every byte value appears, good for MTF */
    char d[2560];
    for (int i = 0; i < 2560; i++) d[i] = (char)(i & 0xff);
    ASSERT_EQ(roundtrip_verify(d, 2560, 1, 0, 0), 0);
}

TEST(rt_reversed_256_symbols) {
    char d[2560];
    for (int i = 0; i < 2560; i++) d[i] = (char)(255 - (i & 0xff));
    ASSERT_EQ(roundtrip_verify(d, 2560, 1, 0, 0), 0);
}

TEST(rt_low_entropy_4symbols) {
    /* Only 4 distinct byte values */
    char d[10000];
    for (int i = 0; i < 10000; i++) d[i] = "ACGT"[i % 4];
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
}

TEST(rt_fibonacci_runs) {
    /* Fibonacci-length runs: 1,1,2,3,5,8,13,21,34,55,89,... */
    char d[10000];
    int pos = 0, a = 1, b = 1, ch = 0;
    while (pos < 10000) {
        int len = a;
        if (pos + len > 10000) len = 10000 - pos;
        memset(d + pos, ch & 0xff, len);
        pos += len;
        int next = a + b; a = b; b = next; ch++;
    }
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
}

TEST(rt_sawtooth) {
    char d[10000];
    for (int i = 0; i < 10000; i++) d[i] = (char)(i % 128);
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
}

TEST(rt_triangle_wave) {
    char d[10000];
    for (int i = 0; i < 10000; i++) {
        int v = i % 512;
        d[i] = (char)(v < 256 ? v : 511 - v);
    }
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
}

/* ========== Text-like patterns ========== */

TEST(rt_english_text) {
    const char *text = "The quick brown fox jumps over the lazy dog. "
                       "Pack my box with five dozen liquor jugs. "
                       "How vexingly quick daft zebras jump! ";
    unsigned int len = strlen(text);
    /* Repeat it to fill 10KB */
    char *d = malloc(10000);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < 10000; i++) d[i] = text[i % len];
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
    free(d);
}

TEST(rt_html_like) {
    const char *html = "<html><body><h1>Title</h1><p>Content with <b>bold</b> and <i>italic</i>.</p></body></html>";
    unsigned int len = strlen(html);
    char *d = malloc(10000);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < 10000; i++) d[i] = html[i % len];
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
    free(d);
}

TEST(rt_json_like) {
    const char *json = "{\"key\":\"value\",\"num\":42,\"arr\":[1,2,3],\"nested\":{\"a\":true}}";
    unsigned int len = strlen(json);
    char *d = malloc(10000);
    ASSERT(d != NULL);
    for (unsigned int i = 0; i < 10000; i++) d[i] = json[i % len];
    ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0);
    free(d);
}

TEST(rt_csv_like) {
    char d[10000];
    int pos = 0;
    for (int row = 0; row < 200 && pos < 9990; row++) {
        int n = snprintf(d + pos, 10000 - pos, "%d,field%d,%.2f\n", row, row*3, row * 1.5);
        pos += n;
    }
    ASSERT_EQ(roundtrip_verify(d, pos, 1, 0, 0), 0);
}

/* ========== Random data at various sizes with different seeds ========== */

TEST(rt_random_100_seed1)  { char d[100]; fill_random(d, 100, 8001); ASSERT_EQ(roundtrip_verify(d, 100, 1, 0, 0), 0); }
TEST(rt_random_100_seed2)  { char d[100]; fill_random(d, 100, 8002); ASSERT_EQ(roundtrip_verify(d, 100, 5, 0, 0), 0); }
TEST(rt_random_100_seed3)  { char d[100]; fill_random(d, 100, 8003); ASSERT_EQ(roundtrip_verify(d, 100, 9, 0, 0), 0); }
TEST(rt_random_1k_seed1)   { char d[1000]; fill_random(d, 1000, 8011); ASSERT_EQ(roundtrip_verify(d, 1000, 1, 0, 0), 0); }
TEST(rt_random_1k_seed2)   { char d[1000]; fill_random(d, 1000, 8012); ASSERT_EQ(roundtrip_verify(d, 1000, 5, 0, 0), 0); }
TEST(rt_random_1k_seed3)   { char d[1000]; fill_random(d, 1000, 8013); ASSERT_EQ(roundtrip_verify(d, 1000, 9, 0, 0), 0); }
TEST(rt_random_10k_seed1)  { char d[10000]; fill_random(d, 10000, 8021); ASSERT_EQ(roundtrip_verify(d, 10000, 1, 0, 0), 0); }
TEST(rt_random_10k_seed2)  { char d[10000]; fill_random(d, 10000, 8022); ASSERT_EQ(roundtrip_verify(d, 10000, 5, 0, 0), 0); }
TEST(rt_random_10k_seed3)  { char d[10000]; fill_random(d, 10000, 8023); ASSERT_EQ(roundtrip_verify(d, 10000, 9, 0, 0), 0); }
TEST(rt_random_50k_seed1)  { char *d = malloc(50000); ASSERT(d); fill_random(d, 50000, 8031); ASSERT_EQ(roundtrip_verify(d, 50000, 1, 0, 0), 0); free(d); }
TEST(rt_random_50k_seed2)  { char *d = malloc(50000); ASSERT(d); fill_random(d, 50000, 8032); ASSERT_EQ(roundtrip_verify(d, 50000, 5, 0, 0), 0); free(d); }
TEST(rt_random_50k_seed3)  { char *d = malloc(50000); ASSERT(d); fill_random(d, 50000, 8033); ASSERT_EQ(roundtrip_verify(d, 50000, 9, 0, 0), 0); free(d); }

/* ========== Random + small decompress ========== */

TEST(rt_random_small_100)  { char d[100]; fill_random(d, 100, 9001); ASSERT_EQ(roundtrip_verify(d, 100, 1, 0, 1), 0); }
TEST(rt_random_small_1k)   { char d[1000]; fill_random(d, 1000, 9002); ASSERT_EQ(roundtrip_verify(d, 1000, 5, 0, 1), 0); }
TEST(rt_random_small_10k)  { char d[10000]; fill_random(d, 10000, 9003); ASSERT_EQ(roundtrip_verify(d, 10000, 9, 0, 1), 0); }

/* ========== Multi-block roundtrip with various block sizes ========== */

TEST(rt_multiblock_bs1_150k) {
    unsigned int sz = 150000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10001);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 0, 0), 0);
    free(d);
}

TEST(rt_multiblock_bs2_250k) {
    unsigned int sz = 250000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10002);
    ASSERT_EQ(roundtrip_verify(d, sz, 2, 0, 0), 0);
    free(d);
}

TEST(rt_multiblock_bs3_400k) {
    unsigned int sz = 400000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10003);
    ASSERT_EQ(roundtrip_verify(d, sz, 3, 0, 0), 0);
    free(d);
}

TEST(rt_multiblock_bs4_500k) {
    unsigned int sz = 500000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10004);
    ASSERT_EQ(roundtrip_verify(d, sz, 4, 0, 0), 0);
    free(d);
}

TEST(rt_multiblock_bs5_600k) {
    unsigned int sz = 600000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10005);
    ASSERT_EQ(roundtrip_verify(d, sz, 5, 0, 0), 0);
    free(d);
}

TEST(rt_multiblock_bs6_700k) {
    unsigned int sz = 700000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10006);
    ASSERT_EQ(roundtrip_verify(d, sz, 6, 0, 0), 0);
    free(d);
}

TEST(rt_multiblock_bs7_800k) {
    unsigned int sz = 800000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10007);
    ASSERT_EQ(roundtrip_verify(d, sz, 7, 0, 0), 0);
    free(d);
}

TEST(rt_multiblock_bs8_900k) {
    unsigned int sz = 900000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10008);
    ASSERT_EQ(roundtrip_verify(d, sz, 8, 0, 0), 0);
    free(d);
}

TEST(rt_multiblock_bs9_1M) {
    unsigned int sz = 1000000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 10009);
    ASSERT_EQ(roundtrip_verify(d, sz, 9, 0, 0), 0);
    free(d);
}

/* ========== Multi-block with small mode decompress ========== */

TEST(rt_multiblock_small_bs1) {
    unsigned int sz = 200000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 11001);
    ASSERT_EQ(roundtrip_verify(d, sz, 1, 0, 1), 0);
    free(d);
}

TEST(rt_multiblock_small_bs5) {
    unsigned int sz = 600000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 11005);
    ASSERT_EQ(roundtrip_verify(d, sz, 5, 0, 1), 0);
    free(d);
}

TEST(rt_multiblock_small_bs9) {
    unsigned int sz = 1000000;
    char *d = malloc(sz); ASSERT(d);
    fill_random(d, sz, 11009);
    ASSERT_EQ(roundtrip_verify(d, sz, 9, 0, 1), 0);
    free(d);
}

/* ========== Edge: highly compressible data ========== */

TEST(rt_highly_compressible_50k) {
    /* All zeros — compresses to very small size */
    char *d = calloc(50000, 1);
    ASSERT(d != NULL);
    ASSERT_EQ(roundtrip_verify(d, 50000, 1, 0, 0), 0);
    free(d);
}

TEST(rt_highly_compressible_100k) {
    char *d = calloc(100000, 1);
    ASSERT(d != NULL);
    ASSERT_EQ(roundtrip_verify(d, 100000, 1, 0, 0), 0);
    free(d);
}

/* ========== Incompressible data ========== */

TEST(rt_incompressible_10k) {
    /* Random data that won't compress well */
    char d[10000];
    fill_random(d, 10000, 12001);
    unsigned int clen = 10000 + 10000/100 + 600;
    char *comp = malloc(clen);
    ASSERT(comp != NULL);
    int ret = BZ2_bzBuffToBuffCompress(comp, &clen, d, 10000, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    /* Random data may expand slightly */
    ASSERT(clen > 0);
    unsigned int dlen = 10000;
    char decomp[10000];
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 10000u);
    ASSERT_MEM_EQ(decomp, d, 10000);
    free(comp);
}

/* ========== BZ2_bzBuffToBuffDecompress output buffer exactly right ========== */

TEST(rt_exact_output_buffer) {
    char data[] = "Hello, world!";
    unsigned int clen = 600;
    char comp[600];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, strlen(data), 1, 0, 0), BZ_OK);

    /* Decompress with exactly the right size */
    unsigned int dlen = strlen(data);
    char decomp[20];
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)strlen(data));
    ASSERT_MEM_EQ(decomp, data, strlen(data));
}

/* Output buffer too small */
TEST(rt_output_buffer_too_small) {
    char data[] = "Hello, world!";
    unsigned int clen = 600;
    char comp[600];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, strlen(data), 1, 0, 0), BZ_OK);

    unsigned int dlen = 5; /* Too small */
    char decomp[5];
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OUTBUFF_FULL);
}

/* ========== Compress buffer too small ========== */

TEST(rt_compress_buffer_too_small) {
    char data[10000];
    fill_random(data, 10000, 13001);
    unsigned int clen = 10; /* Way too small */
    char comp[10];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 10000, 1, 0, 0), BZ_OUTBUFF_FULL);
}

/* ========== Test runner ========== */

TEST_MAIN_BEGIN()
    /* Zeros x all block sizes */
    RUN(rt_zeros_bs1); RUN(rt_zeros_bs2); RUN(rt_zeros_bs3);
    RUN(rt_zeros_bs4); RUN(rt_zeros_bs5); RUN(rt_zeros_bs6);
    RUN(rt_zeros_bs7); RUN(rt_zeros_bs8); RUN(rt_zeros_bs9);

    /* 0xFF x all block sizes */
    RUN(rt_0xff_bs1); RUN(rt_0xff_bs2); RUN(rt_0xff_bs3);
    RUN(rt_0xff_bs4); RUN(rt_0xff_bs5); RUN(rt_0xff_bs6);
    RUN(rt_0xff_bs7); RUN(rt_0xff_bs8); RUN(rt_0xff_bs9);

    /* Ascending/descending */
    RUN(rt_ascending_bs1); RUN(rt_ascending_bs5); RUN(rt_ascending_bs9);
    RUN(rt_descending_bs1); RUN(rt_descending_bs5); RUN(rt_descending_bs9);

    /* Single byte values */
    RUN(rt_single_byte_0x00); RUN(rt_single_byte_0x01);
    RUN(rt_single_byte_0x7f); RUN(rt_single_byte_0x80);
    RUN(rt_single_byte_0xfe); RUN(rt_single_byte_0xff);

    /* Block boundaries */
    RUN(rt_blockboundary_99999); RUN(rt_blockboundary_100000);
    RUN(rt_blockboundary_100001); RUN(rt_blockboundary_200000);
    RUN(rt_bs9_boundary_899999); RUN(rt_bs9_boundary_900000);
    RUN(rt_bs9_boundary_900001);

    /* Small mode */
    RUN(rt_small_bs1); RUN(rt_small_bs2); RUN(rt_small_bs3);
    RUN(rt_small_bs4); RUN(rt_small_bs5); RUN(rt_small_bs6);
    RUN(rt_small_bs7); RUN(rt_small_bs8); RUN(rt_small_bs9);

    /* Work factors */
    RUN(rt_wf0_bs1); RUN(rt_wf1_bs1); RUN(rt_wf50_bs1);
    RUN(rt_wf100_bs1); RUN(rt_wf250_bs1);
    RUN(rt_wf0_bs5); RUN(rt_wf1_bs5); RUN(rt_wf50_bs5);
    RUN(rt_wf100_bs5); RUN(rt_wf250_bs5);
    RUN(rt_wf0_bs9); RUN(rt_wf1_bs9); RUN(rt_wf50_bs9);
    RUN(rt_wf100_bs9); RUN(rt_wf250_bs9);

    /* Various data sizes */
    RUN(rt_size_1); RUN(rt_size_2); RUN(rt_size_3);
    RUN(rt_size_4); RUN(rt_size_5); RUN(rt_size_10);
    RUN(rt_size_100); RUN(rt_size_255); RUN(rt_size_256);
    RUN(rt_size_257); RUN(rt_size_1000); RUN(rt_size_4096);
    RUN(rt_size_10000); RUN(rt_size_65535); RUN(rt_size_65536);

    /* RLE patterns */
    RUN(rt_rle_4bytes); RUN(rt_rle_5bytes);
    RUN(rt_rle_255bytes); RUN(rt_rle_256bytes);
    RUN(rt_rle_257bytes); RUN(rt_rle_258bytes);
    RUN(rt_rle_259bytes); RUN(rt_rle_1000bytes);
    RUN(rt_rle_alternating_4); RUN(rt_rle_alternating_5);
    RUN(rt_rle_increasing);
    RUN(rt_rle_long_run_254); RUN(rt_rle_long_run_258);
    RUN(rt_rle_long_run_259); RUN(rt_rle_long_run_260);
    RUN(rt_rle_long_run_512);

    /* Symbol patterns */
    RUN(rt_single_symbol_repeated); RUN(rt_two_symbol_alternating);
    RUN(rt_all_256_symbols); RUN(rt_reversed_256_symbols);
    RUN(rt_low_entropy_4symbols); RUN(rt_fibonacci_runs);
    RUN(rt_sawtooth); RUN(rt_triangle_wave);

    /* Text-like */
    RUN(rt_english_text); RUN(rt_html_like);
    RUN(rt_json_like); RUN(rt_csv_like);

    /* Random data */
    RUN(rt_random_100_seed1); RUN(rt_random_100_seed2); RUN(rt_random_100_seed3);
    RUN(rt_random_1k_seed1); RUN(rt_random_1k_seed2); RUN(rt_random_1k_seed3);
    RUN(rt_random_10k_seed1); RUN(rt_random_10k_seed2); RUN(rt_random_10k_seed3);
    RUN(rt_random_50k_seed1); RUN(rt_random_50k_seed2); RUN(rt_random_50k_seed3);

    /* Random + small */
    RUN(rt_random_small_100); RUN(rt_random_small_1k); RUN(rt_random_small_10k);

    /* Multi-block */
    RUN(rt_multiblock_bs1_150k); RUN(rt_multiblock_bs2_250k);
    RUN(rt_multiblock_bs3_400k); RUN(rt_multiblock_bs4_500k);
    RUN(rt_multiblock_bs5_600k); RUN(rt_multiblock_bs6_700k);
    RUN(rt_multiblock_bs7_800k); RUN(rt_multiblock_bs8_900k);
    RUN(rt_multiblock_bs9_1M);

    /* Multi-block + small */
    RUN(rt_multiblock_small_bs1); RUN(rt_multiblock_small_bs5);
    RUN(rt_multiblock_small_bs9);

    /* Highly compressible */
    RUN(rt_highly_compressible_50k); RUN(rt_highly_compressible_100k);

    /* Incompressible */
    RUN(rt_incompressible_10k);

    /* Buffer size edge cases */
    RUN(rt_exact_output_buffer); RUN(rt_output_buffer_too_small);
    RUN(rt_compress_buffer_too_small);
TEST_MAIN_END()
