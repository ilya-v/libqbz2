/* fuzz_compress.c — Fuzz the compression path of libqbz2.
 *
 * Feed random uncompressed data into BZ2_bzBuffToBuffCompress at various
 * block sizes and work factors. Crash-finding only (no differential comparison).
 * Compiled with: clang -fsanitize=fuzzer,address -I../include -L../build -lqbz2 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;

    /* Use first two bytes as parameters */
    unsigned int blockSize100k = (data[0] % 9) + 1;  /* 1-9 */
    unsigned int workFactor = data[1];                 /* 0-255, clamped by API */

    const uint8_t *input = data + 2;
    size_t input_size = size - 2;

    /* Allocate output buffer: worst case is input + 1% + 600 bytes */
    unsigned int dest_len = (unsigned int)(input_size + input_size / 100 + 600 + 1);
    if (dest_len < 600) dest_len = 600;

    char *dest = malloc(dest_len);
    if (!dest) return 0;

    int ret = BZ2_bzBuffToBuffCompress(
        dest, &dest_len,
        (char *)input, (unsigned int)input_size,
        blockSize100k, 0 /* verbosity */, workFactor
    );

    /* Valid return codes for compression */
    if (ret != BZ_OK && ret != BZ_OUTBUFF_FULL && ret != BZ_PARAM_ERROR &&
        ret != BZ_MEM_ERROR && ret != BZ_CONFIG_ERROR) {
        /* Unexpected error code — abort to flag it */
        abort();
    }

    /* If compression succeeded, verify round-trip by decompressing */
    if (ret == BZ_OK) {
        unsigned int decomp_len = (unsigned int)(input_size + 1);
        char *decomp = malloc(decomp_len);
        if (decomp) {
            int dret = BZ2_bzBuffToBuffDecompress(
                decomp, &decomp_len,
                dest, dest_len,
                0 /* small */, 0 /* verbosity */
            );
            if (dret == BZ_OK) {
                if (decomp_len != input_size ||
                    memcmp(decomp, input, input_size) != 0) {
                    /* Round-trip mismatch — critical bug */
                    abort();
                }
            }
            free(decomp);
        }
    }

    free(dest);
    return 0;
}
