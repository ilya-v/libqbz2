/* coverage_driver.c — Run fuzz corpus through library APIs for coverage measurement.
 * Reads files from command line args and exercises compress, decompress,
 * streaming, and buffer-to-buffer APIs.
 * Build with gcov: gcc --coverage -O0 -g -Iinclude coverage_driver.c libqbz2_cov.a -o coverage_driver */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0 || len > 4 * 1024 * 1024) { fclose(f); *size = 0; return NULL; }
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    *size = fread(buf, 1, (size_t)len, f);
    fclose(f);
    return buf;
}

static void exercise_compress(const unsigned char *data, size_t size) {
    for (int bs = 1; bs <= 9; bs += 4) {  /* block sizes 1, 5, 9 */
        for (int wf = 0; wf <= 250; wf += 250) {  /* work factors 0, 250 */
            unsigned int dest_len = (unsigned int)(size + size / 100 + 700);
            if (dest_len < 700) dest_len = 700;
            char *dest = malloc(dest_len);
            if (!dest) continue;
            BZ2_bzBuffToBuffCompress(dest, &dest_len, (char *)data,
                                     (unsigned int)size, bs, 0, wf);
            free(dest);
        }
    }
}

static void exercise_decompress(const unsigned char *data, size_t size) {
    for (int small = 0; small <= 1; small++) {
        unsigned int dest_len = 4 * 1024 * 1024;
        char *dest = malloc(dest_len);
        if (!dest) continue;
        BZ2_bzBuffToBuffDecompress(dest, &dest_len, (char *)data,
                                   (unsigned int)size, small, 0);
        free(dest);
    }
}

static void exercise_streaming_compress(const unsigned char *data, size_t size) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzCompressInit(&strm, 5, 0, 0) != BZ_OK) return;

    size_t out_cap = size + size / 100 + 700;
    if (out_cap < 700) out_cap = 700;
    char *out = malloc(out_cap);
    if (!out) { BZ2_bzCompressEnd(&strm); return; }

    strm.next_in = (char *)data;
    strm.avail_in = (unsigned int)size;
    strm.next_out = out;
    strm.avail_out = (unsigned int)out_cap;

    int action = (size == 0) ? BZ_FINISH : BZ_RUN;
    int ret;
    do {
        if (strm.avail_in == 0 && action == BZ_RUN) action = BZ_FINISH;
        ret = BZ2_bzCompress(&strm, action);
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_RUN_OK && ret != BZ_FINISH_OK) break;
    } while (1);

    BZ2_bzCompressEnd(&strm);
    free(out);
}

static void exercise_streaming_decompress(const unsigned char *data, size_t size) {
    for (int small = 0; small <= 1; small++) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        if (BZ2_bzDecompressInit(&strm, 0, small) != BZ_OK) continue;

        unsigned int out_cap = 4 * 1024 * 1024;
        char *out = malloc(out_cap);
        if (!out) { BZ2_bzDecompressEnd(&strm); continue; }

        strm.next_in = (char *)data;
        strm.avail_in = (unsigned int)size;
        strm.next_out = out;
        strm.avail_out = out_cap;

        int ret;
        do {
            ret = BZ2_bzDecompress(&strm);
            if (ret == BZ_STREAM_END || ret != BZ_OK) break;
        } while (strm.avail_in > 0 || strm.avail_out == 0);

        BZ2_bzDecompressEnd(&strm);
        free(out);
    }
}

static void exercise_param_errors(void) {
    /* Parameter validation paths */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    BZ2_bzCompressInit(NULL, 5, 0, 0);
    BZ2_bzCompressInit(&strm, 0, 0, 0);   /* invalid blockSize */
    BZ2_bzCompressInit(&strm, 10, 0, 0);  /* invalid blockSize */
    BZ2_bzCompressInit(&strm, 5, -1, 0);  /* invalid verbosity */
    BZ2_bzCompressInit(&strm, 5, 0, -1);  /* invalid workFactor */
    BZ2_bzDecompressInit(NULL, 0, 0);
    BZ2_bzDecompressInit(&strm, -1, 0);   /* invalid verbosity */
    BZ2_bzDecompressInit(&strm, 0, 2);    /* invalid small */
    BZ2_bzCompress(NULL, BZ_RUN);
    BZ2_bzDecompress(NULL);
    BZ2_bzCompressEnd(NULL);
    BZ2_bzDecompressEnd(NULL);
    BZ2_bzBuffToBuffCompress(NULL, NULL, NULL, 0, 5, 0, 0);
    BZ2_bzBuffToBuffDecompress(NULL, NULL, NULL, 0, 0, 0);
    BZ2_bzlibVersion();
}

int main(int argc, char **argv) {
    exercise_param_errors();

    for (int i = 1; i < argc; i++) {
        size_t size;
        unsigned char *data = read_file(argv[i], &size);
        if (!data) continue;

        /* Try as uncompressed input (compress it) */
        exercise_compress(data, size);
        exercise_streaming_compress(data, size);

        /* Try as compressed input (decompress it) */
        exercise_decompress(data, size);
        exercise_streaming_decompress(data, size);

        free(data);
    }
    return 0;
}
