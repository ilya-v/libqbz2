/* fuzz_decompress.c — Fuzz the decompression path of libqbz2.
 *
 * Feed mutated compressed data into BZ2_bzBuffToBuffDecompress.
 * Tests error handling and robustness against malformed input.
 * Compiled with: clang -fsanitize=fuzzer,address -I../include -L../build -lqbz2 */

#include <stdint.h>
#include <stdlib.h>
#include "bzlib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) return 0;

    /* Try decompressing with both small=0 and small=1 modes */
    for (int small = 0; small <= 1; small++) {
        /* Allocate a generous output buffer */
        unsigned int dest_len = 1024 * 1024;  /* 1 MB */
        char *dest = malloc(dest_len);
        if (!dest) return 0;

        int ret = BZ2_bzBuffToBuffDecompress(
            dest, &dest_len,
            (char *)data, (unsigned int)size,
            small, 0 /* verbosity */
        );

        /* All of these are valid return codes for decompression of arbitrary data */
        if (ret != BZ_OK && ret != BZ_PARAM_ERROR && ret != BZ_MEM_ERROR &&
            ret != BZ_OUTBUFF_FULL && ret != BZ_DATA_ERROR &&
            ret != BZ_DATA_ERROR_MAGIC && ret != BZ_UNEXPECTED_EOF &&
            ret != BZ_CONFIG_ERROR) {
            /* Unexpected error code */
            abort();
        }

        free(dest);
    }

    return 0;
}
