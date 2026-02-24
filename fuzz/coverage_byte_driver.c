/* coverage_byte_driver.c — Feed bz2 input byte-at-a-time to exercise
 * all GET_BITS input exhaustion branches in decompress.c.
 *
 * Each GET_BITS macro has a save/restore point that triggers when
 * avail_in==0. By feeding single bytes, we force the decompressor
 * to hit these points at every position in the bitstream.
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

static void byte_at_a_time_decompress(const unsigned char *data, size_t size, int small_mode) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzDecompressInit(&strm, 0, small_mode) != BZ_OK) return;

    char out[65536];
    size_t pos = 0;
    int ret = BZ_OK;
    unsigned int total_out = 0;

    while (pos < size && ret == BZ_OK) {
        strm.next_in = (char *)data + pos;
        strm.avail_in = 1;
        pos++;

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
    for (int i = 1; i < argc; i++) {
        size_t size;
        unsigned char *data = read_file(argv[i], &size);
        if (!data) continue;
        if (size < 4 || data[0] != 'B' || data[1] != 'Z' || data[2] != 'h') {
            free(data);
            continue;
        }

        /* Byte-at-a-time decompress: both normal and small mode */
        byte_at_a_time_decompress(data, size, 0);
        byte_at_a_time_decompress(data, size, 1);

        /* Verbose decompress */
        verbose_decompress(data, size);

        free(data);
        processed++;
    }
    fprintf(stderr, "Processed %d bz2 files byte-at-a-time\n", processed);
    return 0;
}
