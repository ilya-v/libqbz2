/* Multi-block differential tests: compare libqbz2 vs reference libbz2
 *
 * The existing test_differential.c uses inputs that fit in a single block.
 * This file specifically targets multi-block scenarios (inputs larger than
 * blockSize * 100000 bytes) which exercise block-boundary code paths including
 * cross-block CRC computation, block header writing, and BWT resets.
 *
 * Also tests streaming API differential behavior (BZ2_bzCompress/BZ2_bzDecompress)
 * and error code comparison on invalid inputs.
 *
 * Motivated by the CRC bug found at commit dffe019 which only manifested on
 * multi-block inputs.
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

/* ---- Helper: compare b2b compression ---- */
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

/* ---- Helper: compare b2b decompression ---- */
static int diff_b2b_decompress(const char *desc, const char *comp, unsigned int clen,
                                unsigned int expected_len, int small) {
    total_tests++;
    unsigned int qlen = expected_len + 1000;
    unsigned int rlen = qlen;
    char *qbuf = malloc(qlen);
    char *rbuf = malloc(rlen);
    if (!qbuf || !rbuf) { free(qbuf); free(rbuf); return -1; }

    int qret = BZ2_bzBuffToBuffDecompress(qbuf, &qlen, (char*)comp, clen, small, 0);
    int rret = ref_B2BDecompress(rbuf, &rlen, (char*)comp, clen, small, 0);

    if (qret != rret) {
        fprintf(stderr, "DIVERGENCE [%s]: decompression return code mismatch: libqbz2=%d ref=%d\n",
                desc, qret, rret);
        divergences++;
        free(qbuf); free(rbuf);
        return 1;
    }

    if (qret == BZ_OK) {
        if (qlen != rlen || memcmp(qbuf, rbuf, qlen) != 0) {
            fprintf(stderr, "DIVERGENCE [%s]: decompressed output mismatch (qlen=%u rlen=%u)\n",
                    desc, qlen, rlen);
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

/* ---- Helper: compare streaming compression ---- */
static int diff_streaming_compress(const char *desc, const char *data, unsigned int len,
                                    int blockSize, int workFactor, int chunk_size) {
    total_tests++;

    /* Allocate output buffers */
    unsigned int outsize = len + len / 100 + 600 + 10000;
    char *qout = malloc(outsize);
    char *rout = malloc(outsize);
    if (!qout || !rout) { free(qout); free(rout); return -1; }

    /* libqbz2 streaming compress */
    bz_stream qs;
    memset(&qs, 0, sizeof(qs));
    int qret = BZ2_bzCompressInit(&qs, blockSize, 0, workFactor);
    if (qret != BZ_OK) {
        fprintf(stderr, "DIVERGENCE [%s]: libqbz2 CompressInit failed: %d\n", desc, qret);
        divergences++;
        free(qout); free(rout);
        return 1;
    }

    unsigned int q_total_out = 0;
    unsigned int in_pos = 0;
    int q_done = 0;
    while (!q_done) {
        unsigned int avail = (in_pos < len) ? (unsigned int)chunk_size : 0;
        if (in_pos + avail > len) avail = len - in_pos;

        qs.next_in = (char*)data + in_pos;
        qs.avail_in = avail;
        qs.next_out = qout + q_total_out;
        qs.avail_out = outsize - q_total_out;

        int action = (in_pos + avail >= len) ? BZ_FINISH : BZ_RUN;
        qret = BZ2_bzCompress(&qs, action);

        in_pos += avail - qs.avail_in;
        q_total_out = outsize - qs.avail_out;

        if (qret == BZ_STREAM_END) {
            q_done = 1;
        } else if (action == BZ_RUN && qret != BZ_RUN_OK) {
            fprintf(stderr, "DIVERGENCE [%s]: libqbz2 BZ_RUN returned %d\n", desc, qret);
            BZ2_bzCompressEnd(&qs);
            divergences++;
            free(qout); free(rout);
            return 1;
        } else if (action == BZ_FINISH && qret != BZ_FINISH_OK && qret != BZ_STREAM_END) {
            fprintf(stderr, "DIVERGENCE [%s]: libqbz2 BZ_FINISH returned %d\n", desc, qret);
            BZ2_bzCompressEnd(&qs);
            divergences++;
            free(qout); free(rout);
            return 1;
        }
    }
    BZ2_bzCompressEnd(&qs);

    /* Reference streaming compress with same chunk sizes */
    bz_stream rs;
    memset(&rs, 0, sizeof(rs));
    int rret = ref_CompressInit(&rs, blockSize, 0, workFactor);
    if (rret != BZ_OK) {
        fprintf(stderr, "ERROR [%s]: ref CompressInit failed: %d\n", desc, rret);
        free(qout); free(rout);
        return -1;
    }

    unsigned int r_total_out = 0;
    in_pos = 0;
    int r_done = 0;
    while (!r_done) {
        unsigned int avail = (in_pos < len) ? (unsigned int)chunk_size : 0;
        if (in_pos + avail > len) avail = len - in_pos;

        rs.next_in = (char*)data + in_pos;
        rs.avail_in = avail;
        rs.next_out = rout + r_total_out;
        rs.avail_out = outsize - r_total_out;

        int action = (in_pos + avail >= len) ? BZ_FINISH : BZ_RUN;
        rret = ref_Compress(&rs, action);

        in_pos += avail - rs.avail_in;
        r_total_out = outsize - rs.avail_out;

        if (rret == BZ_STREAM_END) {
            r_done = 1;
        } else if (action == BZ_RUN && rret != BZ_RUN_OK) {
            ref_CompressEnd(&rs);
            break;
        } else if (action == BZ_FINISH && rret != BZ_FINISH_OK && rret != BZ_STREAM_END) {
            ref_CompressEnd(&rs);
            break;
        }
    }
    ref_CompressEnd(&rs);

    /* Compare outputs */
    if (q_total_out != r_total_out) {
        fprintf(stderr, "DIVERGENCE [%s]: streaming output length mismatch: libqbz2=%u ref=%u\n",
                desc, q_total_out, r_total_out);
        divergences++;
        free(qout); free(rout);
        return 1;
    }
    if (memcmp(qout, rout, q_total_out) != 0) {
        fprintf(stderr, "DIVERGENCE [%s]: streaming output data mismatch (len=%u)\n", desc, q_total_out);
        for (unsigned int i = 0; i < q_total_out; i++) {
            if (qout[i] != rout[i]) {
                fprintf(stderr, "  First difference at byte %u: libqbz2=0x%02x ref=0x%02x\n",
                        i, (unsigned char)qout[i], (unsigned char)rout[i]);
                break;
            }
        }
        divergences++;
        free(qout); free(rout);
        return 1;
    }

    passed++;
    free(qout);
    free(rout);
    return 0;
}

/* ---- Helper: compare streaming decompression ---- */
static int diff_streaming_decompress(const char *desc, const char *comp, unsigned int clen,
                                      unsigned int expected_len, int small, int chunk_size) {
    total_tests++;

    unsigned int outsize = expected_len + 1000;
    char *qout = malloc(outsize);
    char *rout = malloc(outsize);
    if (!qout || !rout) { free(qout); free(rout); return -1; }

    /* libqbz2 streaming decompress */
    bz_stream qs;
    memset(&qs, 0, sizeof(qs));
    int qret = BZ2_bzDecompressInit(&qs, 0, small);
    if (qret != BZ_OK) {
        fprintf(stderr, "DIVERGENCE [%s]: libqbz2 DecompressInit failed: %d\n", desc, qret);
        divergences++;
        free(qout); free(rout);
        return 1;
    }

    unsigned int q_total_out = 0;
    unsigned int q_in_pos = 0;
    int q_done = 0;
    while (!q_done) {
        unsigned int avail = (unsigned int)chunk_size;
        if (q_in_pos + avail > clen) avail = clen - q_in_pos;

        qs.next_in = (char*)comp + q_in_pos;
        qs.avail_in = avail;
        qs.next_out = qout + q_total_out;
        qs.avail_out = outsize - q_total_out;

        qret = BZ2_bzDecompress(&qs);
        q_in_pos += avail - qs.avail_in;
        q_total_out = outsize - qs.avail_out;

        if (qret == BZ_STREAM_END) {
            q_done = 1;
        } else if (qret != BZ_OK) {
            fprintf(stderr, "DIVERGENCE [%s]: libqbz2 Decompress returned %d at in_pos=%u\n",
                    desc, qret, q_in_pos);
            BZ2_bzDecompressEnd(&qs);
            divergences++;
            free(qout); free(rout);
            return 1;
        }
    }
    BZ2_bzDecompressEnd(&qs);

    /* Reference streaming decompress */
    bz_stream rs;
    memset(&rs, 0, sizeof(rs));
    int rret = ref_DecompressInit(&rs, 0, small);
    if (rret != BZ_OK) {
        fprintf(stderr, "ERROR [%s]: ref DecompressInit failed: %d\n", desc, rret);
        free(qout); free(rout);
        return -1;
    }

    unsigned int r_total_out = 0;
    unsigned int r_in_pos = 0;
    int r_done = 0;
    while (!r_done) {
        unsigned int avail = (unsigned int)chunk_size;
        if (r_in_pos + avail > clen) avail = clen - r_in_pos;

        rs.next_in = (char*)comp + r_in_pos;
        rs.avail_in = avail;
        rs.next_out = rout + r_total_out;
        rs.avail_out = outsize - r_total_out;

        rret = ref_Decompress(&rs);
        r_in_pos += avail - rs.avail_in;
        r_total_out = outsize - rs.avail_out;

        if (rret == BZ_STREAM_END) {
            r_done = 1;
        } else if (rret != BZ_OK) {
            ref_DecompressEnd(&rs);
            break;
        }
    }
    ref_DecompressEnd(&rs);

    /* Compare outputs */
    if (q_total_out != r_total_out) {
        fprintf(stderr, "DIVERGENCE [%s]: streaming decomp length mismatch: libqbz2=%u ref=%u\n",
                desc, q_total_out, r_total_out);
        divergences++;
        free(qout); free(rout);
        return 1;
    }
    if (memcmp(qout, rout, q_total_out) != 0) {
        fprintf(stderr, "DIVERGENCE [%s]: streaming decomp data mismatch\n", desc);
        divergences++;
        free(qout); free(rout);
        return 1;
    }

    passed++;
    free(qout);
    free(rout);
    return 0;
}

/* ---- Data generation helpers ---- */

/* Generate pseudo-random data with given seed */
static void gen_random(char *buf, unsigned int len, unsigned int seed) {
    for (unsigned int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
}

/* Generate text-like data (ASCII printable with newlines) */
static void gen_text(char *buf, unsigned int len, unsigned int seed) {
    for (unsigned int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        int v = (seed >> 16) % 100;
        if (v < 5) buf[i] = '\n';
        else if (v < 10) buf[i] = ' ';
        else buf[i] = 'a' + (v % 26);
    }
}

/* Generate data with long runs (stresses RLE and block boundaries) */
static void gen_runs(char *buf, unsigned int len, unsigned int seed) {
    unsigned int pos = 0;
    while (pos < len) {
        seed = seed * 1103515245 + 12345;
        char ch = (char)(seed >> 16);
        seed = seed * 1103515245 + 12345;
        unsigned int runlen = ((seed >> 16) % 500) + 1;
        if (pos + runlen > len) runlen = len - pos;
        memset(buf + pos, ch, runlen);
        pos += runlen;
    }
}

/* Generate mixed data (alternating patterns and random) */
static void gen_mixed(char *buf, unsigned int len, unsigned int seed) {
    unsigned int pos = 0;
    while (pos < len) {
        seed = seed * 1103515245 + 12345;
        int kind = (seed >> 16) % 4;
        seed = seed * 1103515245 + 12345;
        unsigned int chunk = ((seed >> 16) % 20000) + 100;
        if (pos + chunk > len) chunk = len - pos;

        switch (kind) {
        case 0: /* zeros */
            memset(buf + pos, 0, chunk);
            break;
        case 1: /* repeated byte */
            memset(buf + pos, (char)(seed >> 16), chunk);
            break;
        case 2: /* random */
            gen_random(buf + pos, chunk, seed + pos);
            break;
        case 3: /* ascending */
            for (unsigned int i = 0; i < chunk; i++)
                buf[pos + i] = (char)(i & 0xFF);
            break;
        }
        pos += chunk;
    }
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

    printf("\n=== Multi-block buffer-to-buffer compression ===\n");

    /* Block size 1 = 100KB blocks. To get multi-block, need > 100KB.
     * Block size 9 = 900KB blocks. To get multi-block, need > 900KB.
     *
     * Test matrix:
     * - Block sizes 1, 2, 5, 9
     * - Input sizes that force 2-5 blocks per block size
     * - Data patterns: zeros, random, text, runs, mixed
     */

    struct {
        int blockSize;
        unsigned int input_size;  /* bytes */
        const char *size_desc;
    } multiblock_configs[] = {
        /* Block size 1 (100KB blocks) */
        {1, 150000,  "150KB/bs1"},   /* ~1.5 blocks */
        {1, 250000,  "250KB/bs1"},   /* ~2.5 blocks */
        {1, 500000,  "500KB/bs1"},   /* ~5 blocks */

        /* Block size 2 (200KB blocks) */
        {2, 250000,  "250KB/bs2"},   /* ~1.25 blocks */
        {2, 500000,  "500KB/bs2"},   /* ~2.5 blocks */

        /* Block size 5 (500KB blocks) */
        {5, 600000,  "600KB/bs5"},   /* ~1.2 blocks */
        {5, 1200000, "1.2MB/bs5"},   /* ~2.4 blocks */

        /* Block size 9 (900KB blocks) */
        {9, 1000000, "1MB/bs9"},     /* ~1.1 blocks */
        {9, 2000000, "2MB/bs9"},     /* ~2.2 blocks */
    };
    int nconfigs = sizeof(multiblock_configs) / sizeof(multiblock_configs[0]);

    struct {
        const char *name;
        void (*gen)(char*, unsigned int, unsigned int);
        unsigned int seed;
    } patterns[] = {
        {"random",  gen_random, 42},
        {"text",    gen_text,   123},
        {"runs",    gen_runs,   456},
        {"mixed",   gen_mixed,  789},
    };
    int npatterns = sizeof(patterns) / sizeof(patterns[0]);

    /* Also test zeros (no generator needed) */
    for (int c = 0; c < nconfigs; c++) {
        unsigned int sz = multiblock_configs[c].input_size;
        char *buf = calloc(sz, 1);
        if (!buf) continue;

        char desc[128];
        snprintf(desc, sizeof(desc), "multiblock zeros %s", multiblock_configs[c].size_desc);
        diff_b2b_compress(desc, buf, sz, multiblock_configs[c].blockSize, 0, 0);
        free(buf);
    }

    /* Pattern-based multi-block tests */
    for (int c = 0; c < nconfigs; c++) {
        unsigned int sz = multiblock_configs[c].input_size;
        char *buf = malloc(sz);
        if (!buf) continue;

        for (int p = 0; p < npatterns; p++) {
            patterns[p].gen(buf, sz, patterns[p].seed);

            char desc[128];
            snprintf(desc, sizeof(desc), "multiblock %s %s",
                     patterns[p].name, multiblock_configs[c].size_desc);
            diff_b2b_compress(desc, buf, sz, multiblock_configs[c].blockSize, 0, 0);
        }
        free(buf);
    }

    /* Multi-block with various work factors */
    printf("\n=== Multi-block with work factors ===\n");
    {
        unsigned int sz = 250000;  /* 2.5 blocks at bs=1 */
        char *buf = malloc(sz);
        if (buf) {
            gen_text(buf, sz, 999);
            int wfs[] = {0, 1, 30, 100, 250};
            for (int w = 0; w < 5; w++) {
                char desc[128];
                snprintf(desc, sizeof(desc), "multiblock text 250KB/bs1 wf=%d", wfs[w]);
                diff_b2b_compress(desc, buf, sz, 1, 0, wfs[w]);
            }
            free(buf);
        }
    }

    /* Cross-decompression of multi-block streams */
    printf("\n=== Multi-block cross-decompression ===\n");
    for (int c = 0; c < nconfigs; c++) {
        unsigned int sz = multiblock_configs[c].input_size;
        char *buf = malloc(sz);
        if (!buf) continue;
        gen_mixed(buf, sz, 1234 + c);

        /* Compress with libqbz2 */
        unsigned int clen = sz + sz / 100 + 600;
        char *comp = malloc(clen);
        if (comp) {
            int ret = BZ2_bzBuffToBuffCompress(comp, &clen, buf, sz,
                                                multiblock_configs[c].blockSize, 0, 0);
            if (ret == BZ_OK) {
                char desc[128];
                snprintf(desc, sizeof(desc), "cross-decomp(ref) multiblock %s",
                         multiblock_configs[c].size_desc);
                diff_b2b_decompress(desc, comp, clen, sz, 0);

                /* Also test small mode */
                snprintf(desc, sizeof(desc), "cross-decomp(ref) multiblock small %s",
                         multiblock_configs[c].size_desc);
                diff_b2b_decompress(desc, comp, clen, sz, 1);
            }
            free(comp);
        }
        free(buf);
    }

    /* Streaming API multi-block comparison */
    printf("\n=== Streaming API multi-block compression ===\n");
    {
        int chunk_sizes[] = {4096, 32768, 65536};  /* different feed chunk sizes */
        int nchunks = sizeof(chunk_sizes) / sizeof(chunk_sizes[0]);

        for (int c = 0; c < 4; c++) {  /* first 4 configs for speed */
            unsigned int sz = multiblock_configs[c].input_size;
            char *buf = malloc(sz);
            if (!buf) continue;
            gen_text(buf, sz, 5678 + c);

            for (int ch = 0; ch < nchunks; ch++) {
                char desc[128];
                snprintf(desc, sizeof(desc), "streaming compress %s chunk=%d",
                         multiblock_configs[c].size_desc, chunk_sizes[ch]);
                diff_streaming_compress(desc, buf, sz, multiblock_configs[c].blockSize,
                                        0, chunk_sizes[ch]);
            }
            free(buf);
        }
    }

    /* Streaming decompression of multi-block data */
    printf("\n=== Streaming API multi-block decompression ===\n");
    {
        int chunk_sizes[] = {1024, 8192, 65536};
        int nchunks = sizeof(chunk_sizes) / sizeof(chunk_sizes[0]);

        for (int c = 0; c < 4; c++) {
            unsigned int sz = multiblock_configs[c].input_size;
            char *buf = malloc(sz);
            if (!buf) continue;
            gen_random(buf, sz, 9999 + c);

            /* Compress first */
            unsigned int clen = sz + sz / 100 + 600;
            char *comp = malloc(clen);
            if (!comp) { free(buf); continue; }

            int ret = BZ2_bzBuffToBuffCompress(comp, &clen, buf, sz,
                                                multiblock_configs[c].blockSize, 0, 0);
            if (ret == BZ_OK) {
                for (int ch = 0; ch < nchunks; ch++) {
                    char desc[128];

                    /* Normal mode */
                    snprintf(desc, sizeof(desc), "streaming decomp %s chunk=%d",
                             multiblock_configs[c].size_desc, chunk_sizes[ch]);
                    diff_streaming_decompress(desc, comp, clen, sz, 0, chunk_sizes[ch]);

                    /* Small mode */
                    snprintf(desc, sizeof(desc), "streaming decomp small %s chunk=%d",
                             multiblock_configs[c].size_desc, chunk_sizes[ch]);
                    diff_streaming_decompress(desc, comp, clen, sz, 1, chunk_sizes[ch]);
                }
            }
            free(comp);
            free(buf);
        }
    }

    /* Boundary-exact inputs: exactly at block boundary */
    printf("\n=== Exact block-boundary inputs ===\n");
    {
        unsigned int exact_sizes[] = {100000, 200000, 300000, 500000, 900000};
        for (int i = 0; i < 5; i++) {
            unsigned int sz = exact_sizes[i];
            char *buf = malloc(sz);
            if (!buf) continue;
            gen_text(buf, sz, 111 + i);

            char desc[128];
            snprintf(desc, sizeof(desc), "exact-boundary %uB/bs1", sz);
            diff_b2b_compress(desc, buf, sz, 1, 0, 0);

            /* Also at native block size */
            int bs = (sz <= 100000) ? 1 : (sz <= 200000) ? 2 : (sz <= 500000) ? 5 : 9;
            snprintf(desc, sizeof(desc), "exact-boundary %uB/bs%d", sz, bs);
            diff_b2b_compress(desc, buf, sz, bs, 0, 0);

            free(buf);
        }
    }

    /* Off-by-one around block boundaries */
    printf("\n=== Off-by-one block boundary inputs ===\n");
    {
        /* block size 1 = 100000 bytes per block */
        unsigned int offsets[] = {99999, 100000, 100001, 199999, 200000, 200001};
        for (int i = 0; i < 6; i++) {
            unsigned int sz = offsets[i];
            char *buf = malloc(sz);
            if (!buf) continue;
            gen_mixed(buf, sz, 222 + i);

            char desc[128];
            snprintf(desc, sizeof(desc), "boundary-off1 %uB/bs1", sz);
            diff_b2b_compress(desc, buf, sz, 1, 0, 0);
            free(buf);
        }
    }

    /* Error code comparison for multi-block truncated streams */
    printf("\n=== Multi-block truncation error comparison ===\n");
    {
        unsigned int sz = 250000;
        char *buf = malloc(sz);
        if (buf) {
            gen_random(buf, sz, 333);

            unsigned int clen = sz + sz / 100 + 600;
            char *comp = malloc(clen);
            if (comp) {
                int ret = BZ2_bzBuffToBuffCompress(comp, &clen, buf, sz, 1, 0, 0);
                if (ret == BZ_OK) {
                    /* Truncate at various points through the multi-block stream */
                    unsigned int trunc_points[] = {4, 10, 50, 100, 500, 1000};
                    for (int i = 0; i < 6; i++) {
                        if (trunc_points[i] >= clen) continue;
                        total_tests++;

                        char qdst[1000], rdst[1000];
                        unsigned int qlen2 = sizeof(qdst), rlen2 = sizeof(rdst);

                        int qr = BZ2_bzBuffToBuffDecompress(qdst, &qlen2, comp, trunc_points[i], 0, 0);
                        int rr = ref_B2BDecompress(rdst, &rlen2, comp, trunc_points[i], 0, 0);

                        if (qr != rr) {
                            fprintf(stderr, "DIVERGENCE [trunc@%u/%u]: error code mismatch: "
                                    "libqbz2=%d ref=%d\n", trunc_points[i], clen, qr, rr);
                            divergences++;
                        } else {
                            passed++;
                        }
                    }

                    /* Also truncate mid-stream at ~1/4, 1/2, 3/4 */
                    for (int frac = 1; frac <= 3; frac++) {
                        unsigned int tp = (clen * frac) / 4;
                        total_tests++;

                        char *qdst = malloc(sz + 100);
                        char *rdst2 = malloc(sz + 100);
                        if (qdst && rdst2) {
                            unsigned int qlen2 = sz + 100, rlen2 = sz + 100;
                            int qr = BZ2_bzBuffToBuffDecompress(qdst, &qlen2, comp, tp, 0, 0);
                            int rr = ref_B2BDecompress(rdst2, &rlen2, comp, tp, 0, 0);

                            if (qr != rr) {
                                fprintf(stderr, "DIVERGENCE [trunc@%u/%u (%d/4)]: error code mismatch: "
                                        "libqbz2=%d ref=%d\n", tp, clen, frac, qr, rr);
                                divergences++;
                            } else {
                                passed++;
                            }
                        }
                        free(qdst);
                        free(rdst2);
                    }
                }
                free(comp);
            }
            free(buf);
        }
    }

    /* Print summary */
    printf("\nMulti-block differential test results:\n");
    printf("  Total tests: %d\n", total_tests);
    printf("  Passed:      %d\n", passed);
    printf("  Divergences: %d\n", divergences);

    if (ref_lib) dlclose(ref_lib);

    return divergences > 0 ? 1 : 0;
}
