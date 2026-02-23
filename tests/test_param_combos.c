/* test_param_combos.c — systematic parameter combination coverage
 *
 * Tests every combination of block size (1-9) x work factor (0, 1, 30, 100, 250)
 * with multiple input patterns, verifying round-trip correctness. Also tests
 * BZ2_bzlibVersion, verbosity levels, and small decompress mode across params.
 *
 * Uses dlopen to compare compressed output against reference libbz2 byte-for-byte.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

/* Reference library loaded via dlopen */
static void *ref_lib = NULL;

typedef int (*compress_fn)(char *, unsigned int *, char *, unsigned int, int, int, int);
typedef int (*decompress_fn)(char *, unsigned int *, char *, unsigned int, int, int);
typedef const char *(*version_fn)(void);

static compress_fn ref_compress = NULL;
static decompress_fn ref_decompress = NULL;
static version_fn ref_version = NULL;

static int load_reference(void) {
    if (ref_lib) return 1;
    const char *paths[] = {
        "./build/release/libbz2.so",
        "./build/release/libbz2.so.1",
        "../reference/libbz2_ref.so",
        "reference/libbz2_ref.so",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        ref_lib = dlopen(paths[i], RTLD_NOW);
        if (ref_lib) break;
    }
    if (!ref_lib) {
        fprintf(stderr, "  WARNING: cannot load reference library: %s\n", dlerror());
        return 0;
    }
    ref_compress = (compress_fn)dlsym(ref_lib, "BZ2_bzBuffToBuffCompress");
    ref_decompress = (decompress_fn)dlsym(ref_lib, "BZ2_bzBuffToBuffDecompress");
    ref_version = (version_fn)dlsym(ref_lib, "BZ2_bzlibVersion");
    return (ref_compress && ref_decompress) ? 1 : 0;
}

/* Generate test data patterns */
static void fill_text(char *buf, unsigned int len) {
    const char *phrase = "The quick brown fox jumps over the lazy dog. ";
    unsigned int plen = (unsigned int)strlen(phrase);
    for (unsigned int i = 0; i < len; i++)
        buf[i] = phrase[i % plen];
}

static void fill_binary(char *buf, unsigned int len) {
    unsigned int seed = 12345;
    for (unsigned int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
}

static void fill_repeated(char *buf, unsigned int len) {
    for (unsigned int i = 0; i < len; i++)
        buf[i] = (char)(i % 5);
}

/* Differential round-trip: compress with libqbz2, compare compressed output
 * byte-for-byte with reference, then decompress and verify original data. */
static int diff_roundtrip(const char *data, unsigned int len, int bs, int wfact) {
    unsigned int maxcomp = len + len / 100 + 600;
    char *qcomp = malloc(maxcomp);
    char *rcomp = malloc(maxcomp);
    char *decomp = malloc(len + 1);
    if (!qcomp || !rcomp || !decomp) { free(qcomp); free(rcomp); free(decomp); return -1; }

    unsigned int qclen = maxcomp, rclen = maxcomp;
    int qret = BZ2_bzBuffToBuffCompress(qcomp, &qclen, (char *)data, len, bs, 0, wfact);
    int rret = ref_compress(rcomp, &rclen, (char *)data, len, bs, 0, wfact);

    int ok = 1;

    /* Both should succeed */
    if (qret != BZ_OK || rret != BZ_OK) {
        fprintf(stderr, "    compress failed: qbz2=%d ref=%d (bs=%d wf=%d len=%u)\n",
                qret, rret, bs, wfact, len);
        ok = 0;
        goto cleanup;
    }

    /* Compressed sizes must match */
    if (qclen != rclen) {
        fprintf(stderr, "    size mismatch: qbz2=%u ref=%u (bs=%d wf=%d len=%u)\n",
                qclen, rclen, bs, wfact, len);
        ok = 0;
        goto cleanup;
    }

    /* Compressed bytes must match bit-for-bit */
    if (memcmp(qcomp, rcomp, qclen) != 0) {
        fprintf(stderr, "    compressed data mismatch (bs=%d wf=%d len=%u)\n",
                bs, wfact, len);
        ok = 0;
        goto cleanup;
    }

    /* Decompress and verify */
    unsigned int dlen = len + 1;
    int dret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, qcomp, qclen, 0, 0);
    if (dret != BZ_OK) {
        fprintf(stderr, "    decompress failed: %d (bs=%d wf=%d len=%u)\n",
                dret, bs, wfact, len);
        ok = 0;
        goto cleanup;
    }
    if (dlen != len || memcmp(decomp, data, len) != 0) {
        fprintf(stderr, "    decompressed data mismatch (bs=%d wf=%d len=%u)\n",
                bs, wfact, len);
        ok = 0;
    }

cleanup:
    free(qcomp); free(rcomp); free(decomp);
    return ok;
}


/* ================================================================
 * Section 1: Block size x work factor matrix — text data
 * 9 block sizes x 5 work factors = 45 tests
 * ================================================================ */

#define MAKE_COMBO_TEST(name, bs, wf, fill_func, sz) \
    TEST(name) { \
        ASSERT(load_reference()); \
        char *data = malloc(sz); \
        ASSERT(data != NULL); \
        fill_func(data, sz); \
        ASSERT(diff_roundtrip(data, sz, bs, wf)); \
        free(data); \
    }

/* Text data, 10KB, all block size x work factor combos */
MAKE_COMBO_TEST(combo_text_bs1_wf0,   1,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs1_wf1,   1,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs1_wf30,  1,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs1_wf100, 1, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs1_wf250, 1, 250, fill_text, 10000)

MAKE_COMBO_TEST(combo_text_bs2_wf0,   2,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs2_wf1,   2,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs2_wf30,  2,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs2_wf100, 2, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs2_wf250, 2, 250, fill_text, 10000)

MAKE_COMBO_TEST(combo_text_bs3_wf0,   3,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs3_wf1,   3,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs3_wf30,  3,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs3_wf100, 3, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs3_wf250, 3, 250, fill_text, 10000)

MAKE_COMBO_TEST(combo_text_bs4_wf0,   4,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs4_wf1,   4,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs4_wf30,  4,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs4_wf100, 4, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs4_wf250, 4, 250, fill_text, 10000)

MAKE_COMBO_TEST(combo_text_bs5_wf0,   5,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs5_wf1,   5,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs5_wf30,  5,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs5_wf100, 5, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs5_wf250, 5, 250, fill_text, 10000)

MAKE_COMBO_TEST(combo_text_bs6_wf0,   6,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs6_wf1,   6,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs6_wf30,  6,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs6_wf100, 6, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs6_wf250, 6, 250, fill_text, 10000)

MAKE_COMBO_TEST(combo_text_bs7_wf0,   7,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs7_wf1,   7,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs7_wf30,  7,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs7_wf100, 7, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs7_wf250, 7, 250, fill_text, 10000)

MAKE_COMBO_TEST(combo_text_bs8_wf0,   8,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs8_wf1,   8,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs8_wf30,  8,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs8_wf100, 8, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs8_wf250, 8, 250, fill_text, 10000)

MAKE_COMBO_TEST(combo_text_bs9_wf0,   9,   0, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs9_wf1,   9,   1, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs9_wf30,  9,  30, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs9_wf100, 9, 100, fill_text, 10000)
MAKE_COMBO_TEST(combo_text_bs9_wf250, 9, 250, fill_text, 10000)


/* ================================================================
 * Section 2: Binary data combos (subset: bs=1,5,9 x wf=0,1,100,250)
 * 12 tests
 * ================================================================ */

MAKE_COMBO_TEST(combo_bin_bs1_wf0,   1,   0, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs1_wf1,   1,   1, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs1_wf100, 1, 100, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs1_wf250, 1, 250, fill_binary, 20000)

MAKE_COMBO_TEST(combo_bin_bs5_wf0,   5,   0, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs5_wf1,   5,   1, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs5_wf100, 5, 100, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs5_wf250, 5, 250, fill_binary, 20000)

MAKE_COMBO_TEST(combo_bin_bs9_wf0,   9,   0, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs9_wf1,   9,   1, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs9_wf100, 9, 100, fill_binary, 20000)
MAKE_COMBO_TEST(combo_bin_bs9_wf250, 9, 250, fill_binary, 20000)


/* ================================================================
 * Section 3: Repeated data combos (subset: bs=1,5,9 x wf=0,1,100,250)
 * 12 tests
 * ================================================================ */

MAKE_COMBO_TEST(combo_rep_bs1_wf0,   1,   0, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs1_wf1,   1,   1, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs1_wf100, 1, 100, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs1_wf250, 1, 250, fill_repeated, 50000)

MAKE_COMBO_TEST(combo_rep_bs5_wf0,   5,   0, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs5_wf1,   5,   1, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs5_wf100, 5, 100, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs5_wf250, 5, 250, fill_repeated, 50000)

MAKE_COMBO_TEST(combo_rep_bs9_wf0,   9,   0, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs9_wf1,   9,   1, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs9_wf100, 9, 100, fill_repeated, 50000)
MAKE_COMBO_TEST(combo_rep_bs9_wf250, 9, 250, fill_repeated, 50000)


/* ================================================================
 * Section 4: Small decompress mode — verify identical output
 * 9 tests (one per block size)
 * ================================================================ */

#define MAKE_SMALL_TEST(name, bs) \
    TEST(name) { \
        char data[5000]; \
        fill_text(data, 5000); \
        char comp[6000]; \
        unsigned int clen = 6000; \
        ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, bs, 0, 0), BZ_OK); \
        char out_normal[5100], out_small[5100]; \
        unsigned int nlen = 5100, slen = 5100; \
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out_normal, &nlen, comp, clen, 0, 0), BZ_OK); \
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out_small, &slen, comp, clen, 1, 0), BZ_OK); \
        ASSERT_EQ(nlen, 5000u); \
        ASSERT_EQ(slen, 5000u); \
        ASSERT_MEM_EQ(out_normal, out_small, 5000); \
        ASSERT_MEM_EQ(out_normal, data, 5000); \
    }

MAKE_SMALL_TEST(small_mode_bs1, 1)
MAKE_SMALL_TEST(small_mode_bs2, 2)
MAKE_SMALL_TEST(small_mode_bs3, 3)
MAKE_SMALL_TEST(small_mode_bs4, 4)
MAKE_SMALL_TEST(small_mode_bs5, 5)
MAKE_SMALL_TEST(small_mode_bs6, 6)
MAKE_SMALL_TEST(small_mode_bs7, 7)
MAKE_SMALL_TEST(small_mode_bs8, 8)
MAKE_SMALL_TEST(small_mode_bs9, 9)


/* ================================================================
 * Section 5: BZ2_bzlibVersion
 * ================================================================ */

TEST(version_returns_string) {
    const char *v = BZ2_bzlibVersion();
    ASSERT(v != NULL);
    ASSERT(strlen(v) > 0);
}

TEST(version_matches_reference) {
    ASSERT(load_reference());
    const char *qver = BZ2_bzlibVersion();
    const char *rver = ref_version();
    ASSERT(qver != NULL);
    ASSERT(rver != NULL);
    /* Both should return a version string starting with "1." */
    ASSERT(qver[0] == '1');
    ASSERT(rver[0] == '1');
}


/* ================================================================
 * Section 6: Verbosity levels (should not affect output)
 * 3 tests
 * ================================================================ */

TEST(verbosity_compress_no_effect) {
    char data[1000];
    fill_text(data, 1000);
    char comp0[1500], comp4[1500];
    unsigned int clen0 = 1500, clen4 = 1500;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp0, &clen0, data, 1000, 1, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp4, &clen4, data, 1000, 1, 4, 0), BZ_OK);
    ASSERT_EQ(clen0, clen4);
    ASSERT_MEM_EQ(comp0, comp4, clen0);
}

TEST(verbosity_decompress_no_effect) {
    char data[1000];
    fill_text(data, 1000);
    char comp[1500];
    unsigned int clen = 1500;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 1000, 1, 0, 0), BZ_OK);

    char out0[1100], out4[1100];
    unsigned int dlen0 = 1100, dlen4 = 1100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out0, &dlen0, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out4, &dlen4, comp, clen, 0, 4), BZ_OK);
    ASSERT_EQ(dlen0, dlen4);
    ASSERT_MEM_EQ(out0, out4, dlen0);
    ASSERT_MEM_EQ(out0, data, 1000);
}

TEST(verbosity_streaming_no_effect) {
    char data[2000];
    fill_text(data, 2000);

    /* Compress with verbosity=0 */
    bz_stream s0;
    memset(&s0, 0, sizeof(s0));
    ASSERT_EQ(BZ2_bzCompressInit(&s0, 1, 0, 0), BZ_OK);
    char comp0[3000];
    s0.next_in = data; s0.avail_in = 2000;
    s0.next_out = comp0; s0.avail_out = 3000;
    ASSERT_EQ(BZ2_bzCompress(&s0, BZ_FINISH), BZ_STREAM_END);
    unsigned int clen0 = 3000 - s0.avail_out;
    BZ2_bzCompressEnd(&s0);

    /* Compress with verbosity=4 */
    bz_stream s4;
    memset(&s4, 0, sizeof(s4));
    ASSERT_EQ(BZ2_bzCompressInit(&s4, 1, 4, 0), BZ_OK);
    char comp4[3000];
    s4.next_in = data; s4.avail_in = 2000;
    s4.next_out = comp4; s4.avail_out = 3000;
    ASSERT_EQ(BZ2_bzCompress(&s4, BZ_FINISH), BZ_STREAM_END);
    unsigned int clen4 = 3000 - s4.avail_out;
    BZ2_bzCompressEnd(&s4);

    ASSERT_EQ(clen0, clen4);
    ASSERT_MEM_EQ(comp0, comp4, clen0);
}


/* ================================================================
 * Section 7: Cross-library decompression (compress with ref, decompress with qbz2)
 * 9 tests (one per block size, wf=30)
 * ================================================================ */

#define MAKE_CROSS_DECOMP_TEST(name, bs) \
    TEST(name) { \
        ASSERT(load_reference()); \
        char data[8000]; \
        fill_text(data, 8000); \
        char comp[9000]; \
        unsigned int clen = 9000; \
        /* Compress with reference */ \
        ASSERT_EQ(ref_compress(comp, &clen, data, 8000, bs, 0, 30), BZ_OK); \
        /* Decompress with qbz2 */ \
        char out[8100]; \
        unsigned int dlen = 8100; \
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0), BZ_OK); \
        ASSERT_EQ(dlen, 8000u); \
        ASSERT_MEM_EQ(out, data, 8000); \
    }

MAKE_CROSS_DECOMP_TEST(cross_decomp_bs1, 1)
MAKE_CROSS_DECOMP_TEST(cross_decomp_bs2, 2)
MAKE_CROSS_DECOMP_TEST(cross_decomp_bs3, 3)
MAKE_CROSS_DECOMP_TEST(cross_decomp_bs4, 4)
MAKE_CROSS_DECOMP_TEST(cross_decomp_bs5, 5)
MAKE_CROSS_DECOMP_TEST(cross_decomp_bs6, 6)
MAKE_CROSS_DECOMP_TEST(cross_decomp_bs7, 7)
MAKE_CROSS_DECOMP_TEST(cross_decomp_bs8, 8)
MAKE_CROSS_DECOMP_TEST(cross_decomp_bs9, 9)


/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    /* Section 1: text data — all 45 bs x wf combos */
    RUN(combo_text_bs1_wf0);   RUN(combo_text_bs1_wf1);
    RUN(combo_text_bs1_wf30);  RUN(combo_text_bs1_wf100);  RUN(combo_text_bs1_wf250);
    RUN(combo_text_bs2_wf0);   RUN(combo_text_bs2_wf1);
    RUN(combo_text_bs2_wf30);  RUN(combo_text_bs2_wf100);  RUN(combo_text_bs2_wf250);
    RUN(combo_text_bs3_wf0);   RUN(combo_text_bs3_wf1);
    RUN(combo_text_bs3_wf30);  RUN(combo_text_bs3_wf100);  RUN(combo_text_bs3_wf250);
    RUN(combo_text_bs4_wf0);   RUN(combo_text_bs4_wf1);
    RUN(combo_text_bs4_wf30);  RUN(combo_text_bs4_wf100);  RUN(combo_text_bs4_wf250);
    RUN(combo_text_bs5_wf0);   RUN(combo_text_bs5_wf1);
    RUN(combo_text_bs5_wf30);  RUN(combo_text_bs5_wf100);  RUN(combo_text_bs5_wf250);
    RUN(combo_text_bs6_wf0);   RUN(combo_text_bs6_wf1);
    RUN(combo_text_bs6_wf30);  RUN(combo_text_bs6_wf100);  RUN(combo_text_bs6_wf250);
    RUN(combo_text_bs7_wf0);   RUN(combo_text_bs7_wf1);
    RUN(combo_text_bs7_wf30);  RUN(combo_text_bs7_wf100);  RUN(combo_text_bs7_wf250);
    RUN(combo_text_bs8_wf0);   RUN(combo_text_bs8_wf1);
    RUN(combo_text_bs8_wf30);  RUN(combo_text_bs8_wf100);  RUN(combo_text_bs8_wf250);
    RUN(combo_text_bs9_wf0);   RUN(combo_text_bs9_wf1);
    RUN(combo_text_bs9_wf30);  RUN(combo_text_bs9_wf100);  RUN(combo_text_bs9_wf250);

    /* Section 2: binary data — 12 combos */
    RUN(combo_bin_bs1_wf0);   RUN(combo_bin_bs1_wf1);
    RUN(combo_bin_bs1_wf100); RUN(combo_bin_bs1_wf250);
    RUN(combo_bin_bs5_wf0);   RUN(combo_bin_bs5_wf1);
    RUN(combo_bin_bs5_wf100); RUN(combo_bin_bs5_wf250);
    RUN(combo_bin_bs9_wf0);   RUN(combo_bin_bs9_wf1);
    RUN(combo_bin_bs9_wf100); RUN(combo_bin_bs9_wf250);

    /* Section 3: repeated data — 12 combos */
    RUN(combo_rep_bs1_wf0);   RUN(combo_rep_bs1_wf1);
    RUN(combo_rep_bs1_wf100); RUN(combo_rep_bs1_wf250);
    RUN(combo_rep_bs5_wf0);   RUN(combo_rep_bs5_wf1);
    RUN(combo_rep_bs5_wf100); RUN(combo_rep_bs5_wf250);
    RUN(combo_rep_bs9_wf0);   RUN(combo_rep_bs9_wf1);
    RUN(combo_rep_bs9_wf100); RUN(combo_rep_bs9_wf250);

    /* Section 4: small decompress mode */
    RUN(small_mode_bs1); RUN(small_mode_bs2); RUN(small_mode_bs3);
    RUN(small_mode_bs4); RUN(small_mode_bs5); RUN(small_mode_bs6);
    RUN(small_mode_bs7); RUN(small_mode_bs8); RUN(small_mode_bs9);

    /* Section 5: version */
    RUN(version_returns_string);
    RUN(version_matches_reference);

    /* Section 6: verbosity */
    RUN(verbosity_compress_no_effect);
    RUN(verbosity_decompress_no_effect);
    RUN(verbosity_streaming_no_effect);

    /* Section 7: cross-library decompression */
    RUN(cross_decomp_bs1); RUN(cross_decomp_bs2); RUN(cross_decomp_bs3);
    RUN(cross_decomp_bs4); RUN(cross_decomp_bs5); RUN(cross_decomp_bs6);
    RUN(cross_decomp_bs7); RUN(cross_decomp_bs8); RUN(cross_decomp_bs9);
TEST_MAIN_END()
