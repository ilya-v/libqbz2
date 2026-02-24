/* coverage_byte_driver.c — Feed bz2 input in small chunks to exercise
 * all GET_BITS input exhaustion branches in decompress.c.
 *
 * Each GET_BITS macro has a save/restore point that triggers when
 * avail_in==0. By feeding small chunks at varying sizes (1, 2, 3, 5, 7
 * bytes), we force the decompressor to hit these suspend points at
 * different positions in the bitstream, maximizing branch coverage.
 *
 * Also exercises verbose decompression (verbosity >= 2) for VPrintf coverage.
 *
 * Build with gcov:
 *   gcc --coverage -O0 -g -Iinclude coverage_byte_driver.c libqbz2_cov.a -o coverage_byte_driver
 *
 * Usage: ./coverage_byte_driver file1.bz2 file2.bz2 ...
 * Only processes files with valid bz2 magic and size < 8KB for speed. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0 || len > 8192) { fclose(f); *size = 0; return NULL; }
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    *size = fread(buf, 1, (size_t)len, f);
    fclose(f);
    return buf;
}

/* Feed data in fixed-size chunks to exercise GET_BITS suspend points.
 * Different chunk sizes cause input exhaustion at different decode positions,
 * covering different avail_in==0 branches in decompress.c. */
static void chunk_decompress(const unsigned char *data, size_t size,
                             int small_mode, unsigned int chunk_size) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzDecompressInit(&strm, 0, small_mode) != BZ_OK) return;

    char out[65536];
    size_t pos = 0;
    int ret = BZ_OK;
    unsigned int total_out = 0;

    while (pos < size && ret == BZ_OK) {
        unsigned int avail = chunk_size;
        if (pos + avail > size) avail = (unsigned int)(size - pos);
        strm.next_in = (char *)data + pos;
        strm.avail_in = avail;
        pos += avail;

        do {
            strm.next_out = out;
            strm.avail_out = sizeof(out);
            ret = BZ2_bzDecompress(&strm);
            total_out += sizeof(out) - strm.avail_out;
            if (total_out > 4 * 1024 * 1024) { ret = -999; break; }
        } while (ret == BZ_OK && strm.avail_out == 0);
    }

    BZ2_bzDecompressEnd(&strm);
}

/* Feed data one byte at a time with a tiny output buffer (1 byte) to
 * exercise both input exhaustion AND output drain branches simultaneously. */
static void byte_tiny_output_decompress(const unsigned char *data, size_t size,
                                         int small_mode) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzDecompressInit(&strm, 0, small_mode) != BZ_OK) return;

    char outbyte;
    size_t pos = 0;
    int ret = BZ_OK;
    unsigned int total_out = 0;

    while (pos < size && ret == BZ_OK) {
        strm.next_in = (char *)data + pos;
        strm.avail_in = 1;
        pos++;

        do {
            strm.next_out = &outbyte;
            strm.avail_out = 1;
            ret = BZ2_bzDecompress(&strm);
            if (strm.avail_out == 0) total_out++;
            if (total_out > 4 * 1024 * 1024) { ret = -999; break; }
        } while (ret == BZ_OK && (strm.avail_in > 0 || strm.avail_out == 0));
    }

    BZ2_bzDecompressEnd(&strm);
}

static void verbose_decompress(const unsigned char *data, size_t size) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzDecompressInit(&strm, 3, 0) != BZ_OK) return;

    unsigned int out_cap = 4 * 1024 * 1024;
    char *out = malloc(out_cap);
    if (!out) { BZ2_bzDecompressEnd(&strm); return; }

    strm.next_in = (char *)data;
    strm.avail_in = (unsigned int)size;
    strm.next_out = out;
    strm.avail_out = out_cap;

    int ret;
    do {
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_OK && strm.avail_out == 0) break;
    } while (ret == BZ_OK);

    BZ2_bzDecompressEnd(&strm);
    free(out);
}

int main(int argc, char **argv) {
    int processed = 0;
    /* Chunk sizes: 1 (original), 2, 3, 5, 7 — these are coprime, so they
     * hit different byte-alignment patterns in the bitstream. */
    static const unsigned int chunk_sizes[] = {1, 2, 3, 5, 7};
    static const int n_chunks = sizeof(chunk_sizes) / sizeof(chunk_sizes[0]);

    for (int i = 1; i < argc; i++) {
        size_t size;
        unsigned char *data = read_file(argv[i], &size);
        if (!data) continue;
        if (size < 4 || data[0] != 'B' || data[1] != 'Z' || data[2] != 'h') {
            free(data);
            continue;
        }

        for (int c = 0; c < n_chunks; c++) {
            /* Normal mode with each chunk size */
            chunk_decompress(data, size, 0, chunk_sizes[c]);
            /* Small mode with each chunk size */
            chunk_decompress(data, size, 1, chunk_sizes[c]);
        }

        /* Byte-at-a-time with tiny output buffer (1 byte) */
        byte_tiny_output_decompress(data, size, 0);
        byte_tiny_output_decompress(data, size, 1);

        /* Verbose decompress */
        verbose_decompress(data, size);

        free(data);
        processed++;
    }
    fprintf(stderr, "Processed %d bz2 files with %d chunk sizes\n",
            processed, n_chunks);
    return 0;
}
