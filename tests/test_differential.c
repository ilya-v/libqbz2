/* Differential tests: compare libqbz2 output vs reference libbz2
 *
 * Compiles against libqbz2 (statically) and loads libbz2 reference via dlopen.
 * Tests that compressed and decompressed output is bit-for-bit identical.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "bzlib.h"

/* Reference library function pointers */
typedef int (*ref_CompressInit_t)(bz_stream*, int, int, int);
typedef int (*ref_Compress_t)(bz_stream*, int);
typedef int (*ref_CompressEnd_t)(bz_stream*);
typedef int (*ref_DecompressInit_t)(bz_stream*, int, int);
typedef int (*ref_Decompress_t)(bz_stream*);
typedef int (*ref_DecompressEnd_t)(bz_stream*);
typedef int (*ref_B2BCompress_t)(char*, unsigned int*, char*, unsigned int, int, int, int);
typedef int (*ref_B2BDecompress_t)(char*, unsigned int*, char*, unsigned int, int, int);

static ref_CompressInit_t ref_CompressInit;
static ref_Compress_t ref_Compress;
static ref_CompressEnd_t ref_CompressEnd;
static ref_DecompressInit_t ref_DecompressInit;
static ref_Decompress_t ref_Decompress;
static ref_DecompressEnd_t ref_DecompressEnd;
static ref_B2BCompress_t ref_B2BCompress;
static ref_B2BDecompress_t ref_B2BDecompress;

static int total_tests = 0;
static int passed = 0;
static int divergences = 0;

static void *ref_lib = NULL;

static int load_reference(const char *path) {
    ref_lib = dlopen(path, RTLD_NOW);
    if (!ref_lib) {
        fprintf(stderr, "Failed to load reference library: %s\n", dlerror());
        return -1;
    }

    ref_CompressInit = (ref_CompressInit_t)dlsym(ref_lib, "BZ2_bzCompressInit");
    ref_Compress = (ref_Compress_t)dlsym(ref_lib, "BZ2_bzCompress");
    ref_CompressEnd = (ref_CompressEnd_t)dlsym(ref_lib, "BZ2_bzCompressEnd");
    ref_DecompressInit = (ref_DecompressInit_t)dlsym(ref_lib, "BZ2_bzDecompressInit");
    ref_Decompress = (ref_Decompress_t)dlsym(ref_lib, "BZ2_bzDecompress");
    ref_DecompressEnd = (ref_DecompressEnd_t)dlsym(ref_lib, "BZ2_bzDecompressEnd");
    ref_B2BCompress = (ref_B2BCompress_t)dlsym(ref_lib, "BZ2_bzBuffToBuffCompress");
    ref_B2BDecompress = (ref_B2BDecompress_t)dlsym(ref_lib, "BZ2_bzBuffToBuffDecompress");

    if (!ref_CompressInit || !ref_Compress || !ref_CompressEnd ||
        !ref_DecompressInit || !ref_Decompress || !ref_DecompressEnd ||
        !ref_B2BCompress || !ref_B2BDecompress) {
        fprintf(stderr, "Failed to resolve reference symbols\n");
        return -1;
    }
    return 0;
}

/* Compare buffer-to-buffer compression */
static int diff_b2b_compress(const char *desc, const char *data, unsigned int len,
                              int blockSize, int verbosity, int workFactor) {
    total_tests++;
    unsigned int qlen = len + len / 100 + 600;
    unsigned int rlen = qlen;
    char *qbuf = malloc(qlen);
    char *rbuf = malloc(rlen);
    if (!qbuf || !rbuf) { free(qbuf); free(rbuf); return -1; }

    int qret = BZ2_bzBuffToBuffCompress(qbuf, &qlen, (char*)data, len, blockSize, verbosity, workFactor);
    int rret = ref_B2BCompress(rbuf, &rlen, (char*)data, len, blockSize, verbosity, workFactor);

    if (qret != rret) {
        fprintf(stderr, "DIVERGENCE [%s]: return code mismatch: libqbz2=%d ref=%d\n", desc, qret, rret);
        divergences++;
        free(qbuf); free(rbuf);
        return 1;
    }

    if (qret == BZ_OK) {
        if (qlen != rlen) {
            fprintf(stderr, "DIVERGENCE [%s]: output length mismatch: libqbz2=%u ref=%u\n", desc, qlen, rlen);
            divergences++;
            free(qbuf); free(rbuf);
            return 1;
        }
        if (memcmp(qbuf, rbuf, qlen) != 0) {
            fprintf(stderr, "DIVERGENCE [%s]: output data mismatch (len=%u)\n", desc, qlen);
            /* Find first difference */
            for (unsigned int i = 0; i < qlen; i++) {
                if (qbuf[i] != rbuf[i]) {
                    fprintf(stderr, "  First difference at byte %u: libqbz2=0x%02x ref=0x%02x\n",
                            i, (unsigned char)qbuf[i], (unsigned char)rbuf[i]);
                    break;
                }
            }
            divergences++;
            free(qbuf); free(rbuf);
            return 1;
        }
    }

    passed++;
    free(qbuf);
    free(rbuf);
    return 0;
}

/* Compare buffer-to-buffer decompression */
static int diff_b2b_decompress(const char *desc, const char *comp, unsigned int clen,
                                int small, int verbosity) {
    total_tests++;
    unsigned int qlen = clen * 20 + 1000;
    unsigned int rlen = qlen;
    char *qbuf = malloc(qlen);
    char *rbuf = malloc(rlen);
    if (!qbuf || !rbuf) { free(qbuf); free(rbuf); return -1; }

    int qret = BZ2_bzBuffToBuffDecompress(qbuf, &qlen, (char*)comp, clen, small, verbosity);
    int rret = ref_B2BDecompress(rbuf, &rlen, (char*)comp, clen, small, verbosity);

    if (qret != rret) {
        fprintf(stderr, "DIVERGENCE [%s]: decompression return code mismatch: libqbz2=%d ref=%d\n",
                desc, qret, rret);
        divergences++;
        free(qbuf); free(rbuf);
        return 1;
    }

    if (qret == BZ_OK) {
        if (qlen != rlen || memcmp(qbuf, rbuf, qlen) != 0) {
            fprintf(stderr, "DIVERGENCE [%s]: decompressed output mismatch\n", desc);
            divergences++;
            free(qbuf); free(rbuf);
            return 1;
        }
    }

    passed++;
    free(qbuf);
    free(rbuf);
    return 0;
}

/* Compare error behavior on invalid inputs */
static int diff_error(const char *desc, const char *data, unsigned int len) {
    total_tests++;
    char qdst[1000], rdst[1000];
    unsigned int qlen = sizeof(qdst), rlen = sizeof(rdst);

    int qret = BZ2_bzBuffToBuffDecompress(qdst, &qlen, (char*)data, len, 0, 0);
    int rret = ref_B2BDecompress(rdst, &rlen, (char*)data, len, 0, 0);

    if (qret != rret) {
        fprintf(stderr, "DIVERGENCE [%s]: error code mismatch: libqbz2=%d ref=%d\n", desc, qret, rret);
        divergences++;
        return 1;
    }

    passed++;
    return 0;
}

int main(void) {
    /* Find reference library */
    const char *ref_path = NULL;
    const char *candidates[] = {
        "./reference/libbz2_ref.so",
        "../reference/libbz2_ref.so",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (load_reference(candidates[i]) == 0) {
            ref_path = candidates[i];
            break;
        }
    }
    if (!ref_path) {
        fprintf(stderr, "ERROR: Could not load reference libbz2\n");
        return 1;
    }
    printf("Loaded reference library from: %s\n", ref_path);

    /* --- Compression tests across all block sizes and work factors --- */

    /* Test data sets */
    const char *hello = "Hello, world!";
    char zeros[10000];
    memset(zeros, 0, sizeof(zeros));
    char repeated[10000];
    memset(repeated, 'A', sizeof(repeated));
    char binary[256];
    for (int i = 0; i < 256; i++) binary[i] = (char)i;

    /* Random-ish data */
    char random_data[50000];
    unsigned int seed = 42;
    for (int i = 0; i < (int)sizeof(random_data); i++) {
        seed = seed * 1103515245 + 12345;
        random_data[i] = (char)(seed >> 16);
    }

    struct { const char *name; const char *data; unsigned int len; } inputs[] = {
        {"empty",    "",          0},
        {"hello",    hello,       (unsigned int)strlen(hello)},
        {"1byte",    "X",         1},
        {"zeros",    zeros,       sizeof(zeros)},
        {"repeated", repeated,    sizeof(repeated)},
        {"binary",   binary,      sizeof(binary)},
        {"random",   random_data, sizeof(random_data)},
    };
    int ninputs = sizeof(inputs) / sizeof(inputs[0]);

    /* Test all block sizes */
    for (int bs = 1; bs <= 9; bs++) {
        for (int i = 0; i < ninputs; i++) {
            char desc[128];
            snprintf(desc, sizeof(desc), "compress bs=%d %s", bs, inputs[i].name);
            diff_b2b_compress(desc, inputs[i].data, inputs[i].len, bs, 0, 0);
        }
    }

    /* Test work factors 0, 1, 30, 100, 250 */
    int work_factors[] = {0, 1, 30, 100, 250};
    for (int w = 0; w < 5; w++) {
        for (int i = 0; i < ninputs; i++) {
            char desc[128];
            snprintf(desc, sizeof(desc), "compress wf=%d %s", work_factors[w], inputs[i].name);
            diff_b2b_compress(desc, inputs[i].data, inputs[i].len, 5, 0, work_factors[w]);
        }
    }

    /* --- Cross-decompress: compress with libqbz2, decompress with ref (and vice versa) --- */
    for (int bs = 1; bs <= 9; bs += 4) {
        for (int i = 0; i < ninputs; i++) {
            /* Compress with libqbz2 */
            unsigned int clen = inputs[i].len + inputs[i].len / 100 + 600;
            char *comp = malloc(clen);
            if (!comp) continue;

            int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char*)inputs[i].data,
                                                inputs[i].len, bs, 0, 0);
            if (ret == BZ_OK) {
                char desc[128];
                snprintf(desc, sizeof(desc), "cross-decomp(ref) bs=%d %s", bs, inputs[i].name);
                /* Decompress with reference */
                unsigned int dlen = inputs[i].len + 100;
                char *decomp = malloc(dlen);
                if (decomp) {
                    total_tests++;
                    int rret = ref_B2BDecompress(decomp, &dlen, comp, clen, 0, 0);
                    if (rret != BZ_OK) {
                        fprintf(stderr, "DIVERGENCE [%s]: ref failed to decompress libqbz2 output (ret=%d)\n",
                                desc, rret);
                        divergences++;
                    } else if (dlen != inputs[i].len || memcmp(decomp, inputs[i].data, dlen) != 0) {
                        fprintf(stderr, "DIVERGENCE [%s]: ref decompressed different data\n", desc);
                        divergences++;
                    } else {
                        passed++;
                    }
                    free(decomp);
                }
            }
            free(comp);

            /* Compress with reference */
            clen = inputs[i].len + inputs[i].len / 100 + 600;
            comp = malloc(clen);
            if (!comp) continue;

            ret = ref_B2BCompress(comp, &clen, (char*)inputs[i].data, inputs[i].len, bs, 0, 0);
            if (ret == BZ_OK) {
                char desc[128];
                snprintf(desc, sizeof(desc), "cross-decomp(qbz2) bs=%d %s", bs, inputs[i].name);
                /* Decompress with libqbz2 */
                unsigned int dlen = inputs[i].len + 100;
                char *decomp = malloc(dlen);
                if (decomp) {
                    total_tests++;
                    int qret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0);
                    if (qret != BZ_OK) {
                        fprintf(stderr, "DIVERGENCE [%s]: libqbz2 failed to decompress ref output (ret=%d)\n",
                                desc, qret);
                        divergences++;
                    } else if (dlen != inputs[i].len || memcmp(decomp, inputs[i].data, dlen) != 0) {
                        fprintf(stderr, "DIVERGENCE [%s]: libqbz2 decompressed different data\n", desc);
                        divergences++;
                    } else {
                        passed++;
                    }
                    free(decomp);
                }
            }
            free(comp);
        }
    }

    /* --- Error behavior comparison --- */
    diff_error("garbage", "this is not bzip2", 17);
    diff_error("empty", "", 0);
    diff_error("truncated_BZ", "BZ", 2);
    diff_error("truncated_BZh", "BZh", 3);
    diff_error("bad_level", "BZh0", 4);
    diff_error("partial_header", "BZh9\x31\x41\x59\x26", 8);

    /* Truncation at every byte of a valid compressed stream */
    {
        const char *msg = "truncation test data";
        unsigned int clen = 1000;
        char comp[1000];
        if (BZ2_bzBuffToBuffCompress(comp, &clen, (char*)msg, strlen(msg), 1, 0, 0) == BZ_OK) {
            for (unsigned int t = 1; t < clen; t++) {
                char desc[64];
                snprintf(desc, sizeof(desc), "truncate@%u/%u", t, clen);
                diff_error(desc, comp, t);
            }
        }
    }

    /* Small decompress mode comparison */
    for (int i = 0; i < ninputs; i++) {
        unsigned int clen = inputs[i].len + inputs[i].len / 100 + 600;
        char *comp = malloc(clen);
        if (!comp) continue;
        int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char*)inputs[i].data, inputs[i].len, 1, 0, 0);
        if (ret == BZ_OK) {
            char desc[128];
            snprintf(desc, sizeof(desc), "decompress-small %s", inputs[i].name);
            diff_b2b_decompress(desc, comp, clen, 1, 0);
        }
        free(comp);
    }

    /* Print summary */
    printf("\nDifferential test results:\n");
    printf("  Total tests: %d\n", total_tests);
    printf("  Passed:      %d\n", passed);
    printf("  Divergences: %d\n", divergences);

    if (ref_lib) dlclose(ref_lib);

    return divergences > 0 ? 1 : 0;
}
