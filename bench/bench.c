/* Benchmark: compare libqbz2 vs reference libbz2 throughput */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include "bzlib.h"

typedef int (*ref_B2BCompress_t)(char*, unsigned int*, char*, unsigned int, int, int, int);
typedef int (*ref_B2BDecompress_t)(char*, unsigned int*, char*, unsigned int, int, int);

static ref_B2BCompress_t ref_B2BCompress;
static ref_B2BDecompress_t ref_B2BDecompress;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Generate test workloads */
static char *gen_text(unsigned int *len) {
    *len = 100000;
    char *buf = malloc(*len);
    const char *words[] = {"the ", "quick ", "brown ", "fox ", "jumps ", "over ", "lazy ", "dog ", "\n"};
    unsigned int pos = 0;
    unsigned int seed = 1234;
    while (pos < *len) {
        seed = seed * 1103515245 + 12345;
        const char *w = words[(seed >> 16) % 9];
        unsigned int wl = strlen(w);
        if (pos + wl > *len) break;
        memcpy(buf + pos, w, wl);
        pos += wl;
    }
    *len = pos;
    return buf;
}

static char *gen_binary(unsigned int *len) {
    *len = 100000;
    char *buf = malloc(*len);
    unsigned int seed = 5678;
    for (unsigned int i = 0; i < *len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
    return buf;
}

static char *gen_repeated(unsigned int *len) {
    *len = 100000;
    char *buf = malloc(*len);
    for (unsigned int i = 0; i < *len; i++) buf[i] = (char)(i % 4 + 'A');
    return buf;
}

static char *gen_zeros(unsigned int *len) {
    *len = 100000;
    char *buf = calloc(*len, 1);
    return buf;
}

typedef struct {
    const char *name;
    double qbz2_comp_mbps;
    double ref_comp_mbps;
    double qbz2_decomp_mbps;
    double ref_decomp_mbps;
    double comp_speedup;
    double decomp_speedup;
} bench_result_t;

static bench_result_t bench_workload(const char *name, const char *data, unsigned int len,
                                      int blockSize, int reps) {
    bench_result_t r;
    r.name = name;

    unsigned int clen = len + len / 100 + 600;
    char *qcomp = malloc(clen);
    char *rcomp = malloc(clen);
    char *decomp = malloc(len + 100);

    /* --- Compression benchmark --- */
    /* libqbz2 */
    double start = get_time_sec();
    for (int i = 0; i < reps; i++) {
        unsigned int cl = clen;
        BZ2_bzBuffToBuffCompress(qcomp, &cl, (char*)data, len, blockSize, 0, 0);
    }
    double qcomp_time = get_time_sec() - start;

    /* reference */
    start = get_time_sec();
    for (int i = 0; i < reps; i++) {
        unsigned int cl = clen;
        ref_B2BCompress(rcomp, &cl, (char*)data, len, blockSize, 0, 0);
    }
    double rcomp_time = get_time_sec() - start;

    /* Get compressed data for decompression benchmark */
    unsigned int qclen = clen;
    BZ2_bzBuffToBuffCompress(qcomp, &qclen, (char*)data, len, blockSize, 0, 0);
    unsigned int rclen = clen;
    ref_B2BCompress(rcomp, &rclen, (char*)data, len, blockSize, 0, 0);

    /* --- Decompression benchmark --- */
    /* libqbz2 */
    start = get_time_sec();
    for (int i = 0; i < reps; i++) {
        unsigned int dl = len + 100;
        BZ2_bzBuffToBuffDecompress(decomp, &dl, qcomp, qclen, 0, 0);
    }
    double qdecomp_time = get_time_sec() - start;

    /* reference */
    start = get_time_sec();
    for (int i = 0; i < reps; i++) {
        unsigned int dl = len + 100;
        ref_B2BDecompress(decomp, &dl, rcomp, rclen, 0, 0);
    }
    double rdecomp_time = get_time_sec() - start;

    double total_bytes = (double)len * reps;
    r.qbz2_comp_mbps = total_bytes / qcomp_time / 1e6;
    r.ref_comp_mbps = total_bytes / rcomp_time / 1e6;
    r.qbz2_decomp_mbps = total_bytes / qdecomp_time / 1e6;
    r.ref_decomp_mbps = total_bytes / rdecomp_time / 1e6;
    r.comp_speedup = r.qbz2_comp_mbps / r.ref_comp_mbps;
    r.decomp_speedup = r.qbz2_decomp_mbps / r.ref_decomp_mbps;

    free(qcomp);
    free(rcomp);
    free(decomp);
    return r;
}

int main(void) {
    /* Load reference */
    const char *ref_paths[] = {
        "./reference/libbz2_ref.so",
        "../reference/libbz2_ref.so",
        NULL
    };
    void *ref_lib = NULL;
    for (int i = 0; ref_paths[i]; i++) {
        ref_lib = dlopen(ref_paths[i], RTLD_NOW);
        if (ref_lib) break;
    }
    if (!ref_lib) {
        fprintf(stderr, "ERROR: Could not load reference libbz2\n");
        return 1;
    }
    ref_B2BCompress = (ref_B2BCompress_t)dlsym(ref_lib, "BZ2_bzBuffToBuffCompress");
    ref_B2BDecompress = (ref_B2BDecompress_t)dlsym(ref_lib, "BZ2_bzBuffToBuffDecompress");
    if (!ref_B2BCompress || !ref_B2BDecompress) {
        fprintf(stderr, "ERROR: Could not resolve reference symbols\n");
        return 1;
    }

    /* Generate workloads */
    unsigned int text_len, binary_len, rep_len, zero_len;
    char *text = gen_text(&text_len);
    char *binary = gen_binary(&binary_len);
    char *rep = gen_repeated(&rep_len);
    char *zeros = gen_zeros(&zero_len);

    struct { const char *name; char *data; unsigned int len; } workloads[] = {
        {"text-100k",     text,   text_len},
        {"binary-100k",   binary, binary_len},
        {"repeated-100k", rep,    rep_len},
        {"zeros-100k",    zeros,  zero_len},
    };
    int nworkloads = sizeof(workloads) / sizeof(workloads[0]);
    int block_sizes[] = {1, 5, 9};
    int nbs = 3;

    printf("%-20s %4s %10s %10s %8s %10s %10s %8s\n",
           "Workload", "BS", "qbz2 C", "ref C", "C speed", "qbz2 D", "ref D", "D speed");
    printf("%-20s %4s %10s %10s %8s %10s %10s %8s\n",
           "--------", "--", "------", "-----", "-------", "------", "-----", "-------");

    for (int w = 0; w < nworkloads; w++) {
        for (int b = 0; b < nbs; b++) {
            char name[64];
            snprintf(name, sizeof(name), "%s", workloads[w].name);
            bench_result_t r = bench_workload(name, workloads[w].data, workloads[w].len,
                                               block_sizes[b], 3);
            printf("%-20s %4d %8.2f MB %8.2f MB %6.2fx %8.2f MB %8.2f MB %6.2fx\n",
                   r.name, block_sizes[b],
                   r.qbz2_comp_mbps, r.ref_comp_mbps, r.comp_speedup,
                   r.qbz2_decomp_mbps, r.ref_decomp_mbps, r.decomp_speedup);
        }
    }

    free(text);
    free(binary);
    free(rep);
    free(zeros);
    dlclose(ref_lib);
    return 0;
}
