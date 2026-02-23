/* fuzz_diff_streaming.c — Differential fuzzer for the streaming API.
 *
 * Compares libqbz2 vs reference libbz2 using the streaming compression/
 * decompression API (BZ2_bzCompressInit/Compress/CompressEnd and
 * DecompressInit/Decompress/DecompressEnd).
 *
 * Both libraries process identical input with identical parameters.
 * The compressed output must be byte-for-byte identical.
 *
 * Reference libbz2 is loaded via dlopen to avoid symbol conflicts.
 * Compiled with: clang -fsanitize=fuzzer,address -I../include -L../build -lqbz2 -ldl */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include "bzlib.h"

/* Reference library function pointers — streaming API */
static int (*ref_compressInit)(bz_stream *, int, int, int);
static int (*ref_compress)(bz_stream *, int);
static int (*ref_compressEnd)(bz_stream *);
static int (*ref_decompressInit)(bz_stream *, int, int);
static int (*ref_decompress)(bz_stream *);
static int (*ref_decompressEnd)(bz_stream *);
static void *ref_lib = NULL;

__attribute__((constructor))
static void init_reference(void) {
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
        fprintf(stderr, "FATAL: Cannot load reference libbz2_ref.so\n");
        abort();
    }

    ref_compressInit = dlsym(ref_lib, "BZ2_bzCompressInit");
    ref_compress = dlsym(ref_lib, "BZ2_bzCompress");
    ref_compressEnd = dlsym(ref_lib, "BZ2_bzCompressEnd");
    ref_decompressInit = dlsym(ref_lib, "BZ2_bzDecompressInit");
    ref_decompress = dlsym(ref_lib, "BZ2_bzDecompress");
    ref_decompressEnd = dlsym(ref_lib, "BZ2_bzDecompressEnd");

    if (!ref_compressInit || !ref_compress || !ref_compressEnd ||
        !ref_decompressInit || !ref_decompress || !ref_decompressEnd) {
        fprintf(stderr, "FATAL: Cannot find streaming BZ2 symbols in reference\n");
        abort();
    }
}

/* Helper: compress entire input using streaming API, return compressed data.
 * If use_ref is nonzero, use the reference library functions. */
static int stream_compress(const uint8_t *input, size_t input_size,
                           char **out, size_t *out_len,
                           int blockSize100k, int workFactor,
                           int use_ref) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret;
    if (use_ref)
        ret = ref_compressInit(&strm, blockSize100k, 0, workFactor);
    else
        ret = BZ2_bzCompressInit(&strm, blockSize100k, 0, workFactor);

    if (ret != BZ_OK) return ret;

    size_t cap = input_size + input_size / 100 + 1024;
    if (cap < 1024) cap = 1024;
    *out = malloc(cap);
    if (!*out) {
        if (use_ref) ref_compressEnd(&strm);
        else BZ2_bzCompressEnd(&strm);
        return BZ_MEM_ERROR;
    }

    strm.next_in = (char *)input;
    strm.avail_in = (unsigned int)input_size;
    *out_len = 0;

    /* Feed all input then finish */
    int action = (input_size == 0) ? BZ_FINISH : BZ_RUN;
    do {
        if (strm.avail_in == 0 && action == BZ_RUN)
            action = BZ_FINISH;

        size_t space = cap - *out_len;
        if (space == 0) {
            cap *= 2;
            char *tmp = realloc(*out, cap);
            if (!tmp) {
                free(*out); *out = NULL;
                if (use_ref) ref_compressEnd(&strm);
                else BZ2_bzCompressEnd(&strm);
                return BZ_MEM_ERROR;
            }
            *out = tmp;
            space = cap - *out_len;
        }

        strm.next_out = *out + *out_len;
        strm.avail_out = (unsigned int)space;

        if (use_ref)
            ret = ref_compress(&strm, action);
        else
            ret = BZ2_bzCompress(&strm, action);

        *out_len += space - strm.avail_out;

        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_RUN_OK && ret != BZ_FLUSH_OK && ret != BZ_FINISH_OK) {
            free(*out); *out = NULL;
            if (use_ref) ref_compressEnd(&strm);
            else BZ2_bzCompressEnd(&strm);
            return ret;
        }
    } while (ret != BZ_STREAM_END);

    if (use_ref) ref_compressEnd(&strm);
    else BZ2_bzCompressEnd(&strm);

    return BZ_STREAM_END;
}

/* Helper: decompress entire input using streaming API. */
static int stream_decompress(const char *input, size_t input_size,
                             char **out, size_t *out_len,
                             int small, int use_ref) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret;
    if (use_ref)
        ret = ref_decompressInit(&strm, 0, small);
    else
        ret = BZ2_bzDecompressInit(&strm, 0, small);

    if (ret != BZ_OK) return ret;

    size_t cap = input_size * 4 + 1024;
    if (cap > 4 * 1024 * 1024) cap = 4 * 1024 * 1024;
    *out = malloc(cap);
    if (!*out) {
        if (use_ref) ref_decompressEnd(&strm);
        else BZ2_bzDecompressEnd(&strm);
        return BZ_MEM_ERROR;
    }

    strm.next_in = (char *)input;
    strm.avail_in = (unsigned int)input_size;
    *out_len = 0;

    do {
        size_t space = cap - *out_len;
        if (space == 0) {
            if (cap >= 4 * 1024 * 1024) {
                /* Output too large — bail out to avoid OOM */
                free(*out); *out = NULL;
                if (use_ref) ref_decompressEnd(&strm);
                else BZ2_bzDecompressEnd(&strm);
                return BZ_MEM_ERROR;
            }
            cap *= 2;
            if (cap > 4 * 1024 * 1024) cap = 4 * 1024 * 1024;
            char *tmp = realloc(*out, cap);
            if (!tmp) {
                free(*out); *out = NULL;
                if (use_ref) ref_decompressEnd(&strm);
                else BZ2_bzDecompressEnd(&strm);
                return BZ_MEM_ERROR;
            }
            *out = tmp;
            space = cap - *out_len;
        }

        strm.next_out = *out + *out_len;
        strm.avail_out = (unsigned int)space;

        if (use_ref)
            ret = ref_decompress(&strm);
        else
            ret = BZ2_bzDecompress(&strm);

        *out_len += space - strm.avail_out;

        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_OK) {
            if (use_ref) ref_decompressEnd(&strm);
            else BZ2_bzDecompressEnd(&strm);
            return ret;
        }
    } while (strm.avail_in > 0 || strm.avail_out == 0);

    if (use_ref) ref_decompressEnd(&strm);
    else BZ2_bzDecompressEnd(&strm);

    return ret;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) return 0;

    uint8_t mode = data[0];
    uint8_t param1 = data[1];
    uint8_t param2 = data[2];
    const uint8_t *input = data + 3;
    size_t input_size = size - 3;

    if (mode & 1) {
        /* === Decompression via streaming API === */
        int small = param1 & 1;

        char *out_qbz2 = NULL, *out_ref = NULL;
        size_t len_qbz2 = 0, len_ref = 0;

        int ret_qbz2 = stream_decompress((const char *)input, input_size,
                                          &out_qbz2, &len_qbz2, small, 0);
        int ret_ref = stream_decompress((const char *)input, input_size,
                                         &out_ref, &len_ref, small, 1);

        /* Skip BZ_MEM_ERROR from our buffer limits — not a real divergence */
        if (ret_qbz2 == BZ_MEM_ERROR || ret_ref == BZ_MEM_ERROR) {
            free(out_qbz2);
            free(out_ref);
            return 0;
        }

        if (ret_qbz2 != ret_ref) {
            int qbz2_ok = (ret_qbz2 == BZ_STREAM_END || ret_qbz2 == BZ_OK);
            int ref_ok = (ret_ref == BZ_STREAM_END || ret_ref == BZ_OK);
            if (qbz2_ok != ref_ok) {
                fprintf(stderr, "TRUE DIVERGENCE (streaming decompress): "
                        "success/failure mismatch!\n");
            } else {
                fprintf(stderr, "ERROR DIVERGENCE (streaming decompress): "
                        "both failed, different codes!\n");
            }
            fprintf(stderr, "  input size: %zu, small=%d\n", input_size, small);
            fprintf(stderr, "  libqbz2: %d, ref: %d\n", ret_qbz2, ret_ref);
            free(out_qbz2);
            free(out_ref);
            abort();
        }

        if (ret_qbz2 == BZ_STREAM_END) {
            if (len_qbz2 != len_ref || memcmp(out_qbz2, out_ref, len_qbz2) != 0) {
                fprintf(stderr, "TRUE DIVERGENCE (streaming decompress): "
                        "output content mismatch!\n");
                fprintf(stderr, "  libqbz2: %zu bytes, ref: %zu bytes\n",
                        len_qbz2, len_ref);
                free(out_qbz2);
                free(out_ref);
                abort();
            }
        }

        free(out_qbz2);
        free(out_ref);
    } else {
        /* === Compression via streaming API === */
        int blockSize100k = (param1 % 9) + 1;
        int workFactor = param2 % 251;

        char *out_qbz2 = NULL, *out_ref = NULL;
        size_t len_qbz2 = 0, len_ref = 0;

        int ret_qbz2 = stream_compress(input, input_size,
                                        &out_qbz2, &len_qbz2,
                                        blockSize100k, workFactor, 0);
        int ret_ref = stream_compress(input, input_size,
                                       &out_ref, &len_ref,
                                       blockSize100k, workFactor, 1);

        if (ret_qbz2 == BZ_MEM_ERROR || ret_ref == BZ_MEM_ERROR) {
            free(out_qbz2);
            free(out_ref);
            return 0;
        }

        if (ret_qbz2 != ret_ref) {
            int qbz2_ok = (ret_qbz2 == BZ_STREAM_END);
            int ref_ok = (ret_ref == BZ_STREAM_END);
            if (qbz2_ok != ref_ok) {
                fprintf(stderr, "TRUE DIVERGENCE (streaming compress): "
                        "success/failure mismatch!\n");
            } else {
                fprintf(stderr, "ERROR DIVERGENCE (streaming compress): "
                        "both failed, different codes!\n");
            }
            fprintf(stderr, "  input size: %zu, blockSize=%d, workFactor=%d\n",
                    input_size, blockSize100k, workFactor);
            fprintf(stderr, "  libqbz2: %d, ref: %d\n", ret_qbz2, ret_ref);
            free(out_qbz2);
            free(out_ref);
            abort();
        }

        if (ret_qbz2 == BZ_STREAM_END) {
            if (len_qbz2 != len_ref) {
                fprintf(stderr, "TRUE DIVERGENCE (streaming compress): "
                        "output length mismatch!\n");
                fprintf(stderr, "  libqbz2: %zu bytes, ref: %zu bytes\n",
                        len_qbz2, len_ref);
                free(out_qbz2);
                free(out_ref);
                abort();
            }
            if (memcmp(out_qbz2, out_ref, len_qbz2) != 0) {
                fprintf(stderr, "TRUE DIVERGENCE (streaming compress): "
                        "output content mismatch!\n");
                fprintf(stderr, "  both produced %zu bytes but content differs\n",
                        len_qbz2);
                for (size_t i = 0; i < len_qbz2; i++) {
                    if (out_qbz2[i] != out_ref[i]) {
                        fprintf(stderr, "  first diff at byte %zu: "
                                "qbz2=0x%02x ref=0x%02x\n",
                                i, (uint8_t)out_qbz2[i], (uint8_t)out_ref[i]);
                        break;
                    }
                }
                free(out_qbz2);
                free(out_ref);
                abort();
            }
        }

        free(out_qbz2);
        free(out_ref);
    }

    return 0;
}
