/* fuzz_streaming.c — Fuzz the streaming compression/decompression API.
 *
 * Exercises BZ2_bzCompressInit/BZ2_bzCompress/BZ2_bzCompressEnd and
 * BZ2_bzDecompressInit/BZ2_bzDecompress/BZ2_bzDecompressEnd with random
 * chunk sizes, partial reads/writes, and interleaved flush operations.
 * Compiled with: clang -fsanitize=fuzzer,address -I../include -L../build -lqbz2 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

/* Consume one byte from the fuzz input, advance pointer */
static inline uint8_t consume_byte(const uint8_t **data, size_t *size) {
    if (*size == 0) return 0;
    uint8_t b = **data;
    (*data)++;
    (*size)--;
    return b;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) return 0;

    /* Parameters from fuzz input */
    unsigned int blockSize100k = (consume_byte(&data, &size) % 9) + 1;
    unsigned int workFactor = consume_byte(&data, &size);
    uint8_t chunk_seed = consume_byte(&data, &size);
    uint8_t action_seed = consume_byte(&data, &size);

    const uint8_t *input = data;
    size_t input_size = size;

    /* --- Compress with streaming API --- */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret = BZ2_bzCompressInit(&strm, blockSize100k, 0 /* verbosity */, workFactor);
    if (ret != BZ_OK) return 0;

    /* Output buffer: generous size */
    size_t out_capacity = input_size + input_size / 100 + 1024;
    if (out_capacity < 1024) out_capacity = 1024;
    char *compressed = malloc(out_capacity);
    if (!compressed) {
        BZ2_bzCompressEnd(&strm);
        return 0;
    }

    size_t compressed_len = 0;
    size_t in_pos = 0;

    /* Feed input in variable-sized chunks */
    while (in_pos < input_size || ret != BZ_STREAM_END) {
        /* Determine chunk size from seed */
        size_t chunk = ((chunk_seed * 17 + 31) % 512) + 1;
        chunk_seed = (uint8_t)(chunk_seed * 37 + 13);

        size_t avail = input_size - in_pos;
        if (chunk > avail) chunk = avail;

        strm.next_in = (char *)(input + in_pos);
        strm.avail_in = (unsigned int)chunk;
        in_pos += chunk;

        /* Decide action: run, flush, or finish */
        int action;
        if (in_pos >= input_size) {
            action = BZ_FINISH;
        } else if ((action_seed & 0x07) == 0) {
            action = BZ_FLUSH;
        } else {
            action = BZ_RUN;
        }
        action_seed = (uint8_t)(action_seed * 41 + 7);

        do {
            size_t space = out_capacity - compressed_len;
            if (space == 0) {
                /* Expand output buffer */
                out_capacity *= 2;
                char *tmp = realloc(compressed, out_capacity);
                if (!tmp) {
                    free(compressed);
                    BZ2_bzCompressEnd(&strm);
                    return 0;
                }
                compressed = tmp;
                space = out_capacity - compressed_len;
            }

            strm.next_out = compressed + compressed_len;
            strm.avail_out = (unsigned int)space;

            ret = BZ2_bzCompress(&strm, action);
            compressed_len += space - strm.avail_out;

            if (ret == BZ_STREAM_END) break;
            if (ret == BZ_RUN_OK || ret == BZ_FLUSH_OK || ret == BZ_FINISH_OK) {
                /* Continue if output buffer was full */
                if (strm.avail_out == 0) continue;
                break;
            }
            /* Unexpected error */
            if (ret != BZ_RUN_OK && ret != BZ_FLUSH_OK && ret != BZ_FINISH_OK) {
                free(compressed);
                BZ2_bzCompressEnd(&strm);
                return 0;
            }
        } while (strm.avail_out == 0);

        if (ret == BZ_STREAM_END) break;
    }

    BZ2_bzCompressEnd(&strm);

    /* --- Decompress with streaming API --- */
    bz_stream dstrm;
    memset(&dstrm, 0, sizeof(dstrm));

    ret = BZ2_bzDecompressInit(&dstrm, 0 /* verbosity */, 0 /* small */);
    if (ret != BZ_OK) {
        free(compressed);
        return 0;
    }

    size_t decomp_capacity = input_size + 1024;
    char *decompressed = malloc(decomp_capacity);
    if (!decompressed) {
        free(compressed);
        BZ2_bzDecompressEnd(&dstrm);
        return 0;
    }

    size_t decomp_len = 0;
    size_t comp_pos = 0;

    while (comp_pos < compressed_len) {
        size_t chunk = ((chunk_seed * 17 + 31) % 512) + 1;
        chunk_seed = (uint8_t)(chunk_seed * 37 + 13);

        size_t avail = compressed_len - comp_pos;
        if (chunk > avail) chunk = avail;

        dstrm.next_in = compressed + comp_pos;
        dstrm.avail_in = (unsigned int)chunk;
        comp_pos += chunk;

        do {
            size_t space = decomp_capacity - decomp_len;
            if (space == 0) {
                decomp_capacity *= 2;
                char *tmp = realloc(decompressed, decomp_capacity);
                if (!tmp) {
                    free(compressed);
                    free(decompressed);
                    BZ2_bzDecompressEnd(&dstrm);
                    return 0;
                }
                decompressed = tmp;
                space = decomp_capacity - decomp_len;
            }

            dstrm.next_out = decompressed + decomp_len;
            dstrm.avail_out = (unsigned int)space;

            ret = BZ2_bzDecompress(&dstrm);
            decomp_len += space - dstrm.avail_out;

            if (ret == BZ_STREAM_END) break;
            if (ret == BZ_OK) {
                if (dstrm.avail_out == 0) continue;
                break;
            }
            /* Decompression error on our own compressed data — bug */
            free(compressed);
            free(decompressed);
            BZ2_bzDecompressEnd(&dstrm);
            abort();
        } while (dstrm.avail_out == 0);

        if (ret == BZ_STREAM_END) break;
    }

    BZ2_bzDecompressEnd(&dstrm);

    /* Verify round-trip */
    if (ret == BZ_STREAM_END) {
        if (decomp_len != input_size || memcmp(decompressed, input, input_size) != 0) {
            /* Round-trip mismatch through streaming API */
            abort();
        }
    }

    free(compressed);
    free(decompressed);
    return 0;
}
