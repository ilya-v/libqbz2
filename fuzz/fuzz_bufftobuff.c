/* fuzz_bufftobuff.c — Fuzz the buffer-to-buffer API with all parameter combos.
 *
 * Exercises BZ2_bzBuffToBuffCompress and BZ2_bzBuffToBuffDecompress with
 * various block sizes, work factors, small mode, and edge-case buffer sizes.
 * Compiled with: clang -fsanitize=fuzzer,address -I../include -L../build -lqbz2 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) return 0;

    /* Parameters from fuzz input */
    unsigned int blockSize100k = (data[0] % 9) + 1;
    unsigned int workFactor = data[1];
    uint8_t flags = data[2];
    int small_decomp = flags & 1;
    int tight_buffer = (flags >> 1) & 1;

    const uint8_t *input = data + 3;
    size_t input_size = size - 3;

    /* --- Compression --- */
    unsigned int comp_len = (unsigned int)(input_size + input_size / 100 + 600 + 1);
    if (comp_len < 600) comp_len = 600;

    /* Optionally use a tight output buffer to exercise BZ_OUTBUFF_FULL */
    if (tight_buffer && input_size > 10) {
        comp_len = (unsigned int)(input_size / 2);
        if (comp_len < 1) comp_len = 1;
    }

    char *compressed = malloc(comp_len);
    if (!compressed) return 0;

    unsigned int actual_comp_len = comp_len;
    int ret = BZ2_bzBuffToBuffCompress(
        compressed, &actual_comp_len,
        (char *)input, (unsigned int)input_size,
        blockSize100k, 0, workFactor
    );

    if (ret == BZ_OK) {
        /* --- Decompression --- */
        unsigned int decomp_len = (unsigned int)(input_size + 1);
        char *decompressed = malloc(decomp_len);
        if (decompressed) {
            unsigned int actual_decomp_len = decomp_len;
            int dret = BZ2_bzBuffToBuffDecompress(
                decompressed, &actual_decomp_len,
                compressed, actual_comp_len,
                small_decomp, 0
            );

            if (dret == BZ_OK) {
                /* Verify round-trip */
                if (actual_decomp_len != input_size ||
                    memcmp(decompressed, input, input_size) != 0) {
                    abort();
                }
            } else if (dret != BZ_OUTBUFF_FULL && dret != BZ_MEM_ERROR) {
                /* Decompression of our own valid compressed data failed
                   with an unexpected error */
                abort();
            }
            free(decompressed);
        }

        /* Also test decompression with a tiny buffer to exercise BZ_OUTBUFF_FULL */
        if (input_size > 0) {
            unsigned int tiny = 1;
            char tiny_buf[1];
            int tret = BZ2_bzBuffToBuffDecompress(
                tiny_buf, &tiny,
                compressed, actual_comp_len,
                small_decomp, 0
            );
            /* Should get BZ_OUTBUFF_FULL (unless input was empty) */
            (void)tret;
        }
    }

    free(compressed);
    return 0;
}
