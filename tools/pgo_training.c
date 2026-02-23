/*
 * PGO training workload for libqbz2
 *
 * Exercises compression and decompression across representative data types
 * and block sizes to collect profile data for profile-guided optimization.
 *
 * Data types: text, binary (pseudo-random), repetitive, zero-filled
 * Block sizes: 1, 5, 9
 * Operations: compress then decompress each combination
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

#define TRAINING_SIZE (256 * 1024)  /* 256 KB per data type */

static void gen_text(char *buf, unsigned int len)
{
    const char *words[] = {
        "the ", "quick ", "brown ", "fox ", "jumps ", "over ",
        "lazy ", "dog ", "and ", "cat ", "sat ", "on ", "mat ",
        "in ", "a ", "big ", "red ", "box ", "\n", ". "
    };
    unsigned int pos = 0, seed = 42;
    while (pos < len) {
        seed = seed * 1103515245 + 12345;
        const char *w = words[(seed >> 16) % 20];
        unsigned int wl = strlen(w);
        if (pos + wl > len) break;
        memcpy(buf + pos, w, wl);
        pos += wl;
    }
    if (pos < len) memset(buf + pos, ' ', len - pos);
}

static void gen_binary(char *buf, unsigned int len)
{
    unsigned int seed = 7919;
    unsigned int i;
    for (i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
}

static void gen_repetitive(char *buf, unsigned int len)
{
    const char pattern[] = "ABCABCABCABC";
    unsigned int plen = sizeof(pattern) - 1;
    unsigned int i;
    for (i = 0; i < len; i++)
        buf[i] = pattern[i % plen];
}

static void train_one(const char *data, unsigned int data_len, int blockSize)
{
    unsigned int comp_len = data_len + data_len / 100 + 600;
    char *comp = malloc(comp_len);
    char *decomp = malloc(data_len + 1);
    unsigned int decomp_len;
    int ret;

    if (!comp || !decomp) {
        fprintf(stderr, "PGO training: malloc failed\n");
        free(comp);
        free(decomp);
        return;
    }

    /* Compress */
    ret = BZ2_bzBuffToBuffCompress(comp, &comp_len,
                                   (char *)data, data_len,
                                   blockSize, 0, 30);
    if (ret != BZ_OK) {
        fprintf(stderr, "PGO training: compress failed (%d) bs=%d\n",
                ret, blockSize);
        free(comp);
        free(decomp);
        return;
    }

    /* Decompress */
    decomp_len = data_len + 1;
    ret = BZ2_bzBuffToBuffDecompress(decomp, &decomp_len,
                                     comp, comp_len, 0, 0);
    if (ret != BZ_OK) {
        fprintf(stderr, "PGO training: decompress failed (%d) bs=%d\n",
                ret, blockSize);
        free(comp);
        free(decomp);
        return;
    }

    free(comp);
    free(decomp);
}

int main(void)
{
    char *text_buf = malloc(TRAINING_SIZE);
    char *binary_buf = malloc(TRAINING_SIZE);
    char *rep_buf = malloc(TRAINING_SIZE);
    char *zero_buf = calloc(TRAINING_SIZE, 1);
    int block_sizes[] = {1, 5, 9};
    int i;

    if (!text_buf || !binary_buf || !rep_buf || !zero_buf) {
        fprintf(stderr, "PGO training: allocation failed\n");
        return 1;
    }

    gen_text(text_buf, TRAINING_SIZE);
    gen_binary(binary_buf, TRAINING_SIZE);
    gen_repetitive(rep_buf, TRAINING_SIZE);

    printf("PGO training: exercising compress+decompress...\n");

    for (i = 0; i < 3; i++) {
        int bs = block_sizes[i];
        printf("  block size %d: text...", bs);
        fflush(stdout);
        train_one(text_buf, TRAINING_SIZE, bs);

        printf(" binary...");
        fflush(stdout);
        train_one(binary_buf, TRAINING_SIZE, bs);

        printf(" repetitive...");
        fflush(stdout);
        train_one(rep_buf, TRAINING_SIZE, bs);

        printf(" zeros...");
        fflush(stdout);
        train_one(zero_buf, TRAINING_SIZE, bs);

        printf(" done.\n");
    }

    printf("PGO training complete.\n");

    free(text_buf);
    free(binary_buf);
    free(rep_buf);
    free(zero_buf);
    return 0;
}
