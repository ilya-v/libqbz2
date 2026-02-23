/* fuzz_fileio.c — Fuzz the high-level FILE* I/O API.
 *
 * Exercises BZ2_bzWriteOpen/Write/WriteClose (compression) and
 * BZ2_bzReadOpen/Read/ReadClose (decompression) using tmpfile()
 * to avoid disk I/O overhead.
 *
 * The harness:
 * 1. Compresses fuzz input via the Write API to a temp file
 * 2. Reads it back via the Read API from the same temp file
 * 3. Verifies round-trip: decompressed output must match original input
 *
 * Also tests decompression of raw fuzz input (possibly malformed) to
 * exercise error handling in the Read API.
 *
 * Compiled with: clang -fsanitize=fuzzer,address -I../include -L../build -lqbz2 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "bzlib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;

    uint8_t mode = data[0];
    uint8_t params = data[1];
    const uint8_t *input = data + 2;
    size_t input_size = size - 2;

    if (mode & 1) {
        /* === Decompression of raw fuzz input via Read API === */
        /* Tests error handling for malformed compressed data */
        FILE *f = tmpfile();
        if (!f) return 0;

        fwrite(input, 1, input_size, f);
        rewind(f);

        int bzerror;
        int small = (params >> 4) & 1;
        BZFILE *bzf = BZ2_bzReadOpen(&bzerror, f, 0, small, NULL, 0);
        if (bzf) {
            char buf[4096];
            while (bzerror == BZ_OK) {
                int n = BZ2_bzRead(&bzerror, bzf, buf, sizeof(buf));
                (void)n;
            }
            /* Valid terminal states: BZ_STREAM_END or any error */
            BZ2_bzReadClose(&bzerror, bzf);
        }

        fclose(f);
    } else {
        /* === Round-trip: compress via Write API, decompress via Read API === */
        int blockSize100k = (params % 9) + 1;
        int workFactor = (input_size > 0) ? (input[0] % 251) : 0;

        /* Compress to temp file */
        FILE *f = tmpfile();
        if (!f) return 0;

        int bzerror;
        BZFILE *bzf = BZ2_bzWriteOpen(&bzerror, f, blockSize100k, 0, workFactor);
        if (!bzf || bzerror != BZ_OK) {
            fclose(f);
            return 0;
        }

        if (input_size > 0) {
            BZ2_bzWrite(&bzerror, bzf, (void *)input, (int)input_size);
            if (bzerror != BZ_OK) {
                BZ2_bzWriteClose(&bzerror, bzf, 1, NULL, NULL);
                fclose(f);
                return 0;
            }
        }

        unsigned int nbytes_in, nbytes_out;
        BZ2_bzWriteClose(&bzerror, bzf, 0, &nbytes_in, &nbytes_out);
        if (bzerror != BZ_OK) {
            fclose(f);
            return 0;
        }

        /* Decompress from the same temp file */
        rewind(f);

        bzf = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
        if (!bzf || bzerror != BZ_OK) {
            fclose(f);
            return 0;
        }

        /* Read decompressed data */
        size_t decomp_cap = input_size + 1024;
        char *decomp = malloc(decomp_cap);
        if (!decomp) {
            BZ2_bzReadClose(&bzerror, bzf);
            fclose(f);
            return 0;
        }

        size_t decomp_len = 0;
        while (bzerror == BZ_OK) {
            size_t space = decomp_cap - decomp_len;
            if (space == 0) {
                decomp_cap *= 2;
                char *tmp = realloc(decomp, decomp_cap);
                if (!tmp) {
                    free(decomp);
                    BZ2_bzReadClose(&bzerror, bzf);
                    fclose(f);
                    return 0;
                }
                decomp = tmp;
                space = decomp_cap - decomp_len;
            }

            int n = BZ2_bzRead(&bzerror, bzf, decomp + decomp_len, (int)space);
            if (n > 0) decomp_len += (size_t)n;
        }

        BZ2_bzReadClose(&bzerror, bzf);
        fclose(f);

        /* Verify round-trip */
        if (decomp_len != input_size ||
            (input_size > 0 && memcmp(decomp, input, input_size) != 0)) {
            fprintf(stderr, "ROUND-TRIP FAILURE via FILE* API!\n");
            fprintf(stderr, "  original: %zu bytes, decompressed: %zu bytes\n",
                    input_size, decomp_len);
            free(decomp);
            abort();
        }

        free(decomp);
    }

    return 0;
}
