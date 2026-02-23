/* fuzz_differential.c — Differential fuzzer: compare libqbz2 vs reference libbz2.
 *
 * Feeds identical inputs to both libraries and compares outputs byte-for-byte.
 * Tests both compression and decompression paths, all block sizes.
 *
 * Build: links against both libqbz2 (under test) and the reference libbz2.
 * The reference libbz2 symbols are accessed via dlopen/dlsym to avoid symbol
 * conflicts, OR via renamed symbols (prefixed with REF_).
 *
 * This harness uses a two-library approach:
 * - libqbz2 symbols are linked directly
 * - Reference libbz2 is loaded via dlopen at startup
 *
 * Compiled with: clang -fsanitize=fuzzer,address -I../include -L../build -lqbz2 -ldl */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include "bzlib.h"

/* Reference library function pointers */
static int (*ref_compress)(char *, unsigned int *, char *, unsigned int,
                           int, int, int);
static int (*ref_decompress)(char *, unsigned int *, char *, unsigned int,
                             int, int);
static void *ref_lib = NULL;

/* Initialize reference library via dlopen */
__attribute__((constructor))
static void init_reference(void) {
    /* Try several paths for the reference libbz2 */
    const char *paths[] = {
        "./reference/libbz2_ref.so",
        "../reference/libbz2_ref.so",
        "../../reference/libbz2_ref.so",
        "/var/home/user/x/claude-play/libqbz2/reference/libbz2_ref.so",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        ref_lib = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (ref_lib) break;
    }

    if (!ref_lib) {
        fprintf(stderr, "FATAL: Cannot load reference libbz2. Build it first:\n");
        fprintf(stderr, "  cd reference/bzip2 && gcc -shared -fPIC -o ../libbz2_ref.so "
                        "blocksort.c huffman.c crctable.c randtable.c compress.c "
                        "decompress.c bzlib.c\n");
        abort();
    }

    ref_compress = dlsym(ref_lib, "BZ2_bzBuffToBuffCompress");
    ref_decompress = dlsym(ref_lib, "BZ2_bzBuffToBuffDecompress");

    if (!ref_compress || !ref_decompress) {
        fprintf(stderr, "FATAL: Cannot find BZ2 symbols in reference library\n");
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;

    /* Extract parameters */
    uint8_t mode = data[0];  /* bit 0: compress(0) or decompress(1) */
    uint8_t params = data[1];
    const uint8_t *input = data + 2;
    size_t input_size = size - 2;

    if (mode & 1) {
        /* === Decompression differential test === */
        /* Feed the same (possibly malformed) compressed data to both libraries */
        unsigned int dest_len_qbz2 = 4 * 1024 * 1024;  /* 4 MB max output */
        unsigned int dest_len_ref = dest_len_qbz2;
        char *dest_qbz2 = malloc(dest_len_qbz2);
        char *dest_ref = malloc(dest_len_ref);
        if (!dest_qbz2 || !dest_ref) {
            free(dest_qbz2);
            free(dest_ref);
            return 0;
        }

        int small = (params >> 4) & 1;

        int ret_qbz2 = BZ2_bzBuffToBuffDecompress(
            dest_qbz2, &dest_len_qbz2,
            (char *)input, (unsigned int)input_size,
            small, 0
        );

        int ret_ref = ref_decompress(
            dest_ref, &dest_len_ref,
            (char *)input, (unsigned int)input_size,
            small, 0
        );

        /* Compare results — classify divergence type */
        if (ret_qbz2 != ret_ref) {
            int qbz2_ok = (ret_qbz2 == BZ_OK);
            int ref_ok = (ret_ref == BZ_OK);
            if (qbz2_ok != ref_ok) {
                fprintf(stderr, "TRUE DIVERGENCE: decompression success/failure mismatch!\n");
            } else {
                fprintf(stderr, "ERROR DIVERGENCE: both failed but different error codes!\n");
            }
            fprintf(stderr, "  input size: %zu, small=%d\n", input_size, small);
            fprintf(stderr, "  libqbz2: %d, ref: %d\n", ret_qbz2, ret_ref);
            abort();
        }

        if (ret_qbz2 == BZ_OK) {
            if (dest_len_qbz2 != dest_len_ref) {
                fprintf(stderr, "DIVERGENCE: decompression output length mismatch!\n");
                fprintf(stderr, "  libqbz2: %u bytes, ref: %u bytes\n",
                        dest_len_qbz2, dest_len_ref);
                abort();
            }
            if (memcmp(dest_qbz2, dest_ref, dest_len_qbz2) != 0) {
                fprintf(stderr, "DIVERGENCE: decompression output content mismatch!\n");
                fprintf(stderr, "  both returned %u bytes but content differs\n",
                        dest_len_qbz2);
                abort();
            }
        }

        free(dest_qbz2);
        free(dest_ref);
    } else {
        /* === Compression differential test === */
        unsigned int blockSize100k = (params % 9) + 1;
        /* Vary workFactor: 0 means default (30), otherwise use fuzz-derived value.
         * Valid range is 0-250, values > 250 are treated as 250 by libbz2. */
        unsigned int workFactor = (input_size > 0) ? (input[0] % 251) : 0;

        unsigned int dest_len_qbz2 = (unsigned int)(input_size + input_size / 100 + 600 + 1);
        unsigned int dest_len_ref = dest_len_qbz2;
        if (dest_len_qbz2 < 600) { dest_len_qbz2 = 600; dest_len_ref = 600; }

        char *dest_qbz2 = malloc(dest_len_qbz2);
        char *dest_ref = malloc(dest_len_ref);
        if (!dest_qbz2 || !dest_ref) {
            free(dest_qbz2);
            free(dest_ref);
            return 0;
        }

        int ret_qbz2 = BZ2_bzBuffToBuffCompress(
            dest_qbz2, &dest_len_qbz2,
            (char *)input, (unsigned int)input_size,
            blockSize100k, 0, workFactor
        );

        int ret_ref = ref_compress(
            dest_ref, &dest_len_ref,
            (char *)input, (unsigned int)input_size,
            blockSize100k, 0, workFactor
        );

        /* Compare return codes — classify divergence type */
        if (ret_qbz2 != ret_ref) {
            int qbz2_ok = (ret_qbz2 == BZ_OK);
            int ref_ok = (ret_ref == BZ_OK);
            if (qbz2_ok != ref_ok) {
                fprintf(stderr, "TRUE DIVERGENCE: compression success/failure mismatch!\n");
            } else {
                fprintf(stderr, "ERROR DIVERGENCE: both failed but different error codes!\n");
            }
            fprintf(stderr, "  input size: %zu, blockSize=%u, workFactor=%u\n",
                    input_size, blockSize100k, workFactor);
            fprintf(stderr, "  libqbz2: %d, ref: %d\n", ret_qbz2, ret_ref);
            abort();
        }

        /* Compare compressed output byte-for-byte */
        if (ret_qbz2 == BZ_OK) {
            if (dest_len_qbz2 != dest_len_ref) {
                fprintf(stderr, "DIVERGENCE: compressed output length mismatch!\n");
                fprintf(stderr, "  input size: %zu, blockSize=%u\n",
                        input_size, blockSize100k);
                fprintf(stderr, "  libqbz2: %u bytes, ref: %u bytes\n",
                        dest_len_qbz2, dest_len_ref);
                abort();
            }
            if (memcmp(dest_qbz2, dest_ref, dest_len_qbz2) != 0) {
                fprintf(stderr, "DIVERGENCE: compressed output content mismatch!\n");
                fprintf(stderr, "  input size: %zu, blockSize=%u\n",
                        input_size, blockSize100k);
                fprintf(stderr, "  both produced %u bytes but content differs\n",
                        dest_len_qbz2);
                /* Find first difference */
                for (unsigned int i = 0; i < dest_len_qbz2; i++) {
                    if (dest_qbz2[i] != dest_ref[i]) {
                        fprintf(stderr, "  first diff at byte %u: qbz2=0x%02x ref=0x%02x\n",
                                i, (uint8_t)dest_qbz2[i], (uint8_t)dest_ref[i]);
                        break;
                    }
                }
                abort();
            }
        }

        free(dest_qbz2);
        free(dest_ref);
    }

    return 0;
}
