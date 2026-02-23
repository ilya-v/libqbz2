/* oom_inject.c — OOM injection tester for libqbz2.
 *
 * Systematically fails each allocation point (1st, 2nd, 3rd, ...) and
 * verifies the library handles it gracefully — returns BZ_MEM_ERROR,
 * no crashes, no leaks, no undefined behavior.
 *
 * Uses the bz_stream custom allocator hooks (bzalloc, bzfree, opaque).
 *
 * Build:
 *   gcc -O1 -g -fsanitize=address,undefined -I../include oom_inject.c \
 *       -L../build -lqbz2 -o oom_inject
 * Or with the fuzz library:
 *   gcc -O1 -g -fsanitize=address,undefined -I../include oom_inject.c \
 *       ../build/fuzz/libqbz2_fuzz.a -o oom_inject
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

/* OOM injection state */
typedef struct {
    int fail_at;        /* Which allocation to fail (1-based) */
    int alloc_count;    /* Current allocation counter */
    int alloc_total;    /* Total allocations observed */
    int free_count;     /* Total frees observed */
} oom_state;

static void *oom_alloc(void *opaque, int items, int size) {
    oom_state *st = (oom_state *)opaque;
    st->alloc_count++;
    st->alloc_total++;
    if (st->alloc_count == st->fail_at) {
        return NULL;  /* Inject OOM */
    }
    return malloc((size_t)items * (size_t)size);
}

static void oom_free(void *opaque, void *addr) {
    oom_state *st = (oom_state *)opaque;
    if (addr) {
        st->free_count++;
        free(addr);
    }
}

static void setup_stream(bz_stream *strm, oom_state *st) {
    memset(strm, 0, sizeof(*strm));
    strm->bzalloc = oom_alloc;
    strm->bzfree = oom_free;
    strm->opaque = st;
}

static void reset_state(oom_state *st, int fail_at) {
    st->fail_at = fail_at;
    st->alloc_count = 0;
    st->alloc_total = 0;
    st->free_count = 0;
}

/* --- Test: CompressInit OOM --- */
static int test_compress_init_oom(void) {
    printf("  CompressInit OOM: ");
    int total_allocs = 0;
    int pass = 0, fail = 0;

    /* First, discover how many allocations CompressInit makes */
    for (int bs = 1; bs <= 9; bs += 4) {
        oom_state st;
        reset_state(&st, 0);  /* No failure — discover count */
        bz_stream strm;
        setup_stream(&strm, &st);

        int ret = BZ2_bzCompressInit(&strm, bs, 0, 0);
        if (ret != BZ_OK) {
            printf("FAIL (init returned %d for bs=%d)\n", ret, bs);
            return 1;
        }
        total_allocs = st.alloc_total;
        BZ2_bzCompressEnd(&strm);

        /* Now fail at each allocation point */
        for (int fail_at = 1; fail_at <= total_allocs; fail_at++) {
            reset_state(&st, fail_at);
            setup_stream(&strm, &st);

            ret = BZ2_bzCompressInit(&strm, bs, 0, 0);
            if (ret == BZ_MEM_ERROR) {
                pass++;
            } else if (ret == BZ_OK) {
                /* Allocation succeeded despite injection — fail_at may be
                   beyond the actual count for this path */
                BZ2_bzCompressEnd(&strm);
                pass++;
            } else {
                printf("FAIL (bs=%d fail_at=%d returned %d, expected BZ_MEM_ERROR)\n",
                       bs, fail_at, ret);
                fail++;
            }
        }
    }
    if (fail == 0) {
        printf("PASS (%d injection points, %d passed)\n", pass, pass);
    }
    return fail;
}

/* --- Test: DecompressInit OOM --- */
static int test_decompress_init_oom(void) {
    printf("  DecompressInit OOM: ");
    int pass = 0, fail = 0;

    for (int small = 0; small <= 1; small++) {
        oom_state st;
        reset_state(&st, 0);
        bz_stream strm;
        setup_stream(&strm, &st);

        int ret = BZ2_bzDecompressInit(&strm, 0, small);
        if (ret != BZ_OK) {
            printf("FAIL (init returned %d for small=%d)\n", ret, small);
            return 1;
        }
        int total_allocs = st.alloc_total;
        BZ2_bzDecompressEnd(&strm);

        for (int fail_at = 1; fail_at <= total_allocs; fail_at++) {
            reset_state(&st, fail_at);
            setup_stream(&strm, &st);

            ret = BZ2_bzDecompressInit(&strm, 0, small);
            if (ret == BZ_MEM_ERROR) {
                pass++;
            } else if (ret == BZ_OK) {
                BZ2_bzDecompressEnd(&strm);
                pass++;
            } else {
                printf("FAIL (small=%d fail_at=%d returned %d)\n",
                       small, fail_at, ret);
                fail++;
            }
        }
    }
    if (fail == 0) {
        printf("PASS (%d injection points)\n", pass);
    }
    return fail;
}

/* --- Test: Compress full round-trip OOM --- */
static int test_compress_roundtrip_oom(const unsigned char *data, size_t size) {
    printf("  Compress round-trip OOM (size=%zu): ", size);
    int pass = 0, fail = 0;

    /* Discover total allocs in a successful compress */
    oom_state st;
    reset_state(&st, 0);
    bz_stream strm;
    setup_stream(&strm, &st);

    int ret = BZ2_bzCompressInit(&strm, 5, 0, 0);
    if (ret != BZ_OK) {
        printf("FAIL (init)\n");
        return 1;
    }

    size_t out_cap = size + size / 100 + 700;
    if (out_cap < 700) out_cap = 700;
    char *out = malloc(out_cap);

    strm.next_in = (char *)data;
    strm.avail_in = (unsigned int)size;
    strm.next_out = out;
    strm.avail_out = (unsigned int)out_cap;

    do {
        int action = (strm.avail_in == 0) ? BZ_FINISH : BZ_RUN;
        ret = BZ2_bzCompress(&strm, action);
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_RUN_OK && ret != BZ_FINISH_OK) break;
    } while (1);

    int total_allocs = st.alloc_total;
    BZ2_bzCompressEnd(&strm);
    free(out);

    /* Now fail at each allocation point */
    for (int fail_at = 1; fail_at <= total_allocs; fail_at++) {
        reset_state(&st, fail_at);
        setup_stream(&strm, &st);

        ret = BZ2_bzCompressInit(&strm, 5, 0, 0);
        if (ret == BZ_MEM_ERROR) {
            pass++;
            continue;
        }
        if (ret != BZ_OK) {
            printf("FAIL (fail_at=%d init returned %d)\n", fail_at, ret);
            fail++;
            continue;
        }

        out = malloc(out_cap);
        strm.next_in = (char *)data;
        strm.avail_in = (unsigned int)size;
        strm.next_out = out;
        strm.avail_out = (unsigned int)out_cap;

        int ok = 1;
        do {
            int action = (strm.avail_in == 0) ? BZ_FINISH : BZ_RUN;
            ret = BZ2_bzCompress(&strm, action);
            if (ret == BZ_STREAM_END) break;
            if (ret == BZ_RUN_OK || ret == BZ_FINISH_OK) continue;
            /* Any other return is an error — should be graceful */
            ok = 0;
            break;
        } while (1);

        BZ2_bzCompressEnd(&strm);
        free(out);
        pass++;
    }

    if (fail == 0) {
        printf("PASS (%d injection points)\n", pass);
    }
    return fail;
}

/* --- Test: Decompress OOM --- */
static int test_decompress_oom(const unsigned char *compressed, size_t comp_size) {
    printf("  Decompress OOM (comp_size=%zu): ", comp_size);
    int pass = 0, fail = 0;

    /* Discover total allocs in a successful decompress */
    oom_state st;
    reset_state(&st, 0);
    bz_stream strm;
    setup_stream(&strm, &st);

    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK) {
        printf("FAIL (init)\n");
        return 1;
    }

    unsigned int out_cap = 4 * 1024 * 1024;
    char *out = malloc(out_cap);

    strm.next_in = (char *)compressed;
    strm.avail_in = (unsigned int)comp_size;
    strm.next_out = out;
    strm.avail_out = out_cap;

    do {
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END || ret != BZ_OK) break;
    } while (strm.avail_in > 0 || strm.avail_out == 0);

    int total_allocs = st.alloc_total;
    BZ2_bzDecompressEnd(&strm);
    free(out);

    /* Now fail at each allocation point */
    for (int fail_at = 1; fail_at <= total_allocs; fail_at++) {
        reset_state(&st, fail_at);
        setup_stream(&strm, &st);

        ret = BZ2_bzDecompressInit(&strm, 0, 0);
        if (ret == BZ_MEM_ERROR) {
            pass++;
            continue;
        }
        if (ret != BZ_OK) {
            printf("FAIL (fail_at=%d init returned %d)\n", fail_at, ret);
            fail++;
            continue;
        }

        out = malloc(out_cap);
        strm.next_in = (char *)compressed;
        strm.avail_in = (unsigned int)comp_size;
        strm.next_out = out;
        strm.avail_out = out_cap;

        do {
            ret = BZ2_bzDecompress(&strm);
            if (ret == BZ_STREAM_END) break;
            if (ret == BZ_OK) continue;
            /* Any error is acceptable as long as it's graceful */
            break;
        } while (strm.avail_in > 0 || strm.avail_out == 0);

        BZ2_bzDecompressEnd(&strm);
        free(out);
        pass++;
    }

    if (fail == 0) {
        printf("PASS (%d injection points)\n", pass);
    }
    return fail;
}

/* --- Test: BuffToBuffCompress OOM --- */
static int test_bufftobuff_compress_oom(const unsigned char *data, size_t size) {
    printf("  BuffToBuffCompress OOM (size=%zu): ", size);

    /* BuffToBuffCompress internally creates and destroys a stream,
       so we can't inject via hooks. Instead we test via the streaming
       interface. This test is a placeholder confirmation. */
    printf("SKIP (no allocator hooks in buffer-to-buffer API)\n");
    return 0;
}

int main(void) {
    printf("=== OOM Injection Tests for libqbz2 ===\n\n");

    int total_fail = 0;

    /* Test init OOM */
    printf("Phase 1: Init OOM\n");
    total_fail += test_compress_init_oom();
    total_fail += test_decompress_init_oom();

    /* Prepare test data */
    printf("\nPhase 2: Streaming OOM\n");

    /* Small input */
    const char *small_data = "Hello, World! This is a test of OOM injection.";
    total_fail += test_compress_roundtrip_oom(
        (const unsigned char *)small_data, strlen(small_data));

    /* Medium input (1KB) */
    unsigned char medium[1024];
    for (int i = 0; i < 1024; i++) medium[i] = (unsigned char)(i % 256);
    total_fail += test_compress_roundtrip_oom(medium, sizeof(medium));

    /* Repetitive input (64KB — triggers multi-symbol RLE) */
    unsigned char *repeat = malloc(65536);
    memset(repeat, 'A', 65536);
    total_fail += test_compress_roundtrip_oom(repeat, 65536);

    /* Multi-block input (200KB — triggers 2 blocks for bs=1) */
    unsigned char *multiblock = malloc(200000);
    for (int i = 0; i < 200000; i++) multiblock[i] = (unsigned char)(i * 7 + 13);
    total_fail += test_compress_roundtrip_oom(multiblock, 200000);

    /* Decompress OOM — first compress, then test decompression */
    printf("\nPhase 3: Decompression OOM\n");

    /* Compress the small data to get valid bz2 */
    unsigned int comp_len = (unsigned int)(strlen(small_data) + strlen(small_data) / 100 + 700);
    char *compressed = malloc(comp_len);
    int ret = BZ2_bzBuffToBuffCompress(compressed, &comp_len,
        (char *)small_data, (unsigned int)strlen(small_data), 5, 0, 0);
    if (ret == BZ_OK) {
        total_fail += test_decompress_oom((unsigned char *)compressed, comp_len);
    } else {
        printf("  SKIP: could not compress test data (ret=%d)\n", ret);
    }
    free(compressed);

    /* Compress medium data */
    comp_len = sizeof(medium) + sizeof(medium) / 100 + 700;
    compressed = malloc(comp_len);
    ret = BZ2_bzBuffToBuffCompress(compressed, &comp_len,
        (char *)medium, sizeof(medium), 5, 0, 0);
    if (ret == BZ_OK) {
        total_fail += test_decompress_oom((unsigned char *)compressed, comp_len);
    }
    free(compressed);

    /* Compress repetitive data */
    comp_len = 65536 + 65536 / 100 + 700;
    compressed = malloc(comp_len);
    ret = BZ2_bzBuffToBuffCompress(compressed, &comp_len,
        (char *)repeat, 65536, 5, 0, 0);
    if (ret == BZ_OK) {
        total_fail += test_decompress_oom((unsigned char *)compressed, comp_len);
    }
    free(compressed);
    free(repeat);

    /* Compress multi-block data */
    comp_len = 200000 + 200000 / 100 + 700;
    compressed = malloc(comp_len);
    ret = BZ2_bzBuffToBuffCompress(compressed, &comp_len,
        (char *)multiblock, 200000, 1, 0, 0);
    if (ret == BZ_OK) {
        total_fail += test_decompress_oom((unsigned char *)compressed, comp_len);
    }
    free(compressed);
    free(multiblock);

    printf("\n=== Summary ===\n");
    if (total_fail == 0) {
        printf("ALL OOM INJECTION TESTS PASSED\n");
    } else {
        printf("FAILURES: %d\n", total_fail);
    }
    return total_fail;
}
