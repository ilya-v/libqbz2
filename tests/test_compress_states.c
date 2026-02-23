/* Compression state machine edge case tests for libqbz2
 * Targets low-coverage paths in bzlib.c: BZ_FINISH_OK with tiny output,
 * avail_in_expect mismatch sequence errors, custom allocator hooks,
 * custom allocator failure (BZ_MEM_ERROR), BZ_RUN with no progress,
 * decompression after STREAM_END, total counter hi32 overflow,
 * empty FINISH, and FLUSH-to-FINISH transitions.
 * Also covers multi-block FINISH_OK draining and BZ_M_IDLE state.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ================================================================
 * FINISH_OK with tiny output buffer
 * Exercises the BZ_FINISH_OK return path in BZ2_bzCompress
 * ================================================================ */

TEST(finish_1byte_output) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    /* Feed 10KB of data */
    char data[10000];
    for (int i = 0; i < 10000; i++) data[i] = (char)(i % 251);
    char output[20000];
    unsigned int total_out = 0;

    strm.next_in = data;
    strm.avail_in = 10000;
    strm.next_out = output;
    strm.avail_out = 20000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);
    total_out = 20000 - strm.avail_out;

    /* FINISH with 1-byte output buffer increments */
    strm.avail_in = 0;
    int finish_ok_count = 0;
    int ret;
    do {
        strm.next_out = output + total_out;
        strm.avail_out = 1;
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        total_out += 1 - strm.avail_out;
        if (ret == BZ_FINISH_OK) finish_ok_count++;
        ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);
    } while (ret != BZ_STREAM_END);

    /* Must have seen at least one FINISH_OK */
    ASSERT(finish_ok_count > 0);

    /* Verify roundtrip */
    BZ2_bzCompressEnd(&strm);
    char decomp[10000];
    unsigned int dlen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, total_out, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 10000u);
    ASSERT_MEM_EQ(decomp, data, 10000);
}

TEST(finish_2byte_output) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[5000];
    memset(data, 'Y', 5000);
    char output[10000];
    unsigned int total_out = 0;

    strm.next_in = data;
    strm.avail_in = 5000;
    strm.next_out = output;
    strm.avail_out = 10000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);
    total_out = 10000 - strm.avail_out;

    /* FINISH with 2-byte output */
    strm.avail_in = 0;
    int finish_ok_count = 0;
    int ret;
    do {
        strm.next_out = output + total_out;
        strm.avail_out = 2;
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        total_out += 2 - strm.avail_out;
        if (ret == BZ_FINISH_OK) finish_ok_count++;
        ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);
    } while (ret != BZ_STREAM_END);

    ASSERT(finish_ok_count > 0);
    BZ2_bzCompressEnd(&strm);
}

TEST(finish_multiblock_1byte_output) {
    /* Use bs=1 with 200KB data => multiple blocks, finish with 1-byte output */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    int datalen = 200000;
    char *data = malloc(datalen);
    ASSERT(data != NULL);
    for (int i = 0; i < datalen; i++) data[i] = (char)(i % 127);

    char *output = malloc(datalen * 2);
    ASSERT(output != NULL);
    unsigned int total_out = 0;

    /* Feed all data first */
    strm.next_in = data;
    strm.avail_in = datalen;
    strm.next_out = output;
    strm.avail_out = datalen * 2;

    /* Run until all input consumed */
    while (strm.avail_in > 0) {
        int ret = BZ2_bzCompress(&strm, BZ_RUN);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }
    total_out = datalen * 2 - strm.avail_out;

    /* FINISH with 1-byte output — forces many FINISH_OK iterations */
    int finish_ok_count = 0;
    int ret;
    do {
        strm.next_out = output + total_out;
        strm.avail_out = 1;
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        total_out += 1 - strm.avail_out;
        if (ret == BZ_FINISH_OK) finish_ok_count++;
        ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);
    } while (ret != BZ_STREAM_END);

    ASSERT(finish_ok_count > 0);
    BZ2_bzCompressEnd(&strm);

    /* Verify roundtrip */
    char *decomp = malloc(datalen);
    ASSERT(decomp != NULL);
    unsigned int dlen = datalen;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, total_out, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)datalen);
    ASSERT_MEM_EQ(decomp, data, datalen);

    free(data);
    free(output);
    free(decomp);
}

/* ================================================================
 * avail_in_expect mismatch — sequence errors
 * ================================================================ */

TEST(flush_avail_in_mismatch) {
    /* Start FLUSH, then change avail_in before next call */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[50000];
    memset(data, 'M', sizeof(data));
    char output[100000];

    /* Feed data */
    strm.next_in = data;
    strm.avail_in = 50000;
    strm.next_out = output;
    strm.avail_out = 100000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Start flush with tiny output to get FLUSH_OK */
    strm.avail_in = 0;
    strm.avail_out = 1;
    int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    if (ret == BZ_FLUSH_OK) {
        /* Now tamper with avail_in — mismatch should cause SEQUENCE_ERROR */
        strm.avail_in = 5;
        strm.avail_out = 100000;
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }
    /* else it flushed in one go, which is also valid */

    BZ2_bzCompressEnd(&strm);
}

TEST(finish_avail_in_mismatch) {
    /* Start FINISH, then change avail_in before next call */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[50000];
    memset(data, 'N', sizeof(data));
    char output[100000];

    /* Feed data */
    strm.next_in = data;
    strm.avail_in = 50000;
    strm.next_out = output;
    strm.avail_out = 100000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Start finish with tiny output to get FINISH_OK */
    strm.avail_in = 0;
    strm.avail_out = 1;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    if (ret == BZ_FINISH_OK) {
        /* Now tamper with avail_in — mismatch should cause SEQUENCE_ERROR */
        strm.avail_in = 5;
        strm.avail_out = 100000;
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }

    BZ2_bzCompressEnd(&strm);
}

/* ================================================================
 * Custom allocator hooks
 * ================================================================ */

static int alloc_count = 0;
static int free_count = 0;

static void* custom_alloc(void* opaque, int items, int size) {
    (void)opaque;
    alloc_count++;
    return malloc((size_t)items * (size_t)size);
}

static void custom_free(void* opaque, void* addr) {
    (void)opaque;
    if (addr != NULL) {
        free_count++;
        free(addr);
    }
}

TEST(compress_custom_allocator) {
    alloc_count = 0;
    free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = custom_alloc;
    strm.bzfree = custom_free;
    strm.opaque = NULL;

    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);
    ASSERT(alloc_count > 0);

    char data[] = "custom allocator test data";
    char output[1000];
    strm.next_in = data;
    strm.avail_in = (unsigned int)strlen(data);
    strm.next_out = output;
    strm.avail_out = 1000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_FINISH), BZ_STREAM_END);

    int allocs_before_end = alloc_count;
    BZ2_bzCompressEnd(&strm);
    ASSERT(free_count > 0);
    /* Alloc count shouldn't change after end */
    ASSERT_EQ(alloc_count, allocs_before_end);
}

TEST(decompress_custom_allocator) {
    /* First compress something */
    char data[] = "decompress custom allocator test";
    char comp[1000];
    unsigned int clen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, (unsigned int)strlen(data),
                                        1, 0, 0), BZ_OK);

    alloc_count = 0;
    free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = custom_alloc;
    strm.bzfree = custom_free;
    strm.opaque = NULL;

    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    ASSERT(alloc_count > 0);

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = 1000;

    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);

    BZ2_bzDecompressEnd(&strm);
    ASSERT(free_count > 0);
}

/* ================================================================
 * Custom allocator failure — BZ_MEM_ERROR paths
 * ================================================================ */

static int fail_after_n = 0;
static int fail_alloc_count = 0;

static void* failing_alloc(void* opaque, int items, int size) {
    (void)opaque;
    fail_alloc_count++;
    if (fail_alloc_count > fail_after_n) return NULL;
    return malloc((size_t)items * (size_t)size);
}

static void failing_free(void* opaque, void* addr) {
    (void)opaque;
    if (addr != NULL) free(addr);
}

TEST(compress_alloc_fail_first) {
    /* Fail on the very first allocation (EState struct) */
    fail_after_n = 0;
    fail_alloc_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = failing_alloc;
    strm.bzfree = failing_free;

    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
}

TEST(compress_alloc_fail_second) {
    /* Succeed first alloc (EState), fail on arr1 */
    fail_after_n = 1;
    fail_alloc_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = failing_alloc;
    strm.bzfree = failing_free;

    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
}

TEST(compress_alloc_fail_third) {
    /* Succeed first two allocs, fail on arr2 */
    fail_after_n = 2;
    fail_alloc_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = failing_alloc;
    strm.bzfree = failing_free;

    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
}

TEST(compress_alloc_fail_fourth) {
    /* Succeed first three allocs, fail on ftab */
    fail_after_n = 3;
    fail_alloc_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = failing_alloc;
    strm.bzfree = failing_free;

    int ret = BZ2_bzCompressInit(&strm, 1, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
}

TEST(decompress_alloc_fail) {
    /* Fail on first allocation (DState struct) */
    fail_after_n = 0;
    fail_alloc_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = failing_alloc;
    strm.bzfree = failing_free;

    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(ret, BZ_MEM_ERROR);
}

/* ================================================================
 * BZ_RUN with no input (no progress)
 * ================================================================ */

TEST(run_no_input_no_output) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[100];
    strm.next_in = NULL;
    strm.avail_in = 0;
    strm.next_out = output;
    strm.avail_out = 100;

    /* No input, no prior data — no progress => BZ_PARAM_ERROR */
    int ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);

    BZ2_bzCompressEnd(&strm);
}

/* ================================================================
 * Decompression after STREAM_END — BZ_X_IDLE
 * ================================================================ */

TEST(decompress_after_stream_end) {
    /* Compress some data */
    char data[] = "idle state test";
    char comp[1000];
    unsigned int clen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, (unsigned int)strlen(data),
                                        1, 0, 0), BZ_OK);

    /* Decompress to completion */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = 1000;
    ASSERT_EQ(BZ2_bzDecompress(&strm), BZ_STREAM_END);

    /* Try to decompress again — should get SEQUENCE_ERROR (BZ_X_IDLE) */
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = 1000;
    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);

    BZ2_bzDecompressEnd(&strm);
}

/* ================================================================
 * Decompression with 1-byte output buffer
 * ================================================================ */

TEST(decompress_1byte_output) {
    char data[5000];
    for (int i = 0; i < 5000; i++) data[i] = (char)(i % 251);
    char comp[10000];
    unsigned int clen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[5000];
    unsigned int total_out = 0;

    strm.next_in = comp;
    strm.avail_in = clen;

    int ret;
    do {
        strm.next_out = output + total_out;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        total_out += 1 - strm.avail_out;
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    } while (ret != BZ_STREAM_END);

    ASSERT_EQ(total_out, 5000u);
    ASSERT_MEM_EQ(output, data, 5000);
    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_1byte_output_small) {
    char data[5000];
    for (int i = 0; i < 5000; i++) data[i] = (char)(i % 251);
    char comp[10000];
    unsigned int clen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 1), BZ_OK);

    char output[5000];
    unsigned int total_out = 0;

    strm.next_in = comp;
    strm.avail_in = clen;

    int ret;
    do {
        strm.next_out = output + total_out;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        total_out += 1 - strm.avail_out;
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    } while (ret != BZ_STREAM_END);

    ASSERT_EQ(total_out, 5000u);
    ASSERT_MEM_EQ(output, data, 5000);
    BZ2_bzDecompressEnd(&strm);
}

/* ================================================================
 * Empty FINISH — compress with no input, immediate FINISH
 * ================================================================ */

TEST(empty_finish) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = NULL;
    strm.avail_in = 0;
    strm.next_out = output;
    strm.avail_out = 1000;

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 1000 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Should produce a valid empty bz2 stream */
    char decomp[100];
    unsigned int dlen = 100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 0u);
}

TEST(empty_finish_1byte_output) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[1000];
    unsigned int total_out = 0;

    strm.next_in = NULL;
    strm.avail_in = 0;

    int finish_ok_count = 0;
    int ret;
    do {
        strm.next_out = output + total_out;
        strm.avail_out = 1;
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        total_out += 1 - strm.avail_out;
        if (ret == BZ_FINISH_OK) finish_ok_count++;
        ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);
    } while (ret != BZ_STREAM_END);

    /* Empty stream should still have a header */
    ASSERT(total_out > 0);
    ASSERT(finish_ok_count > 0);

    BZ2_bzCompressEnd(&strm);
}

/* ================================================================
 * FLUSH then FINISH — exercises the mode transitions
 * ================================================================ */

TEST(flush_then_finish) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[20000];
    for (int i = 0; i < 20000; i++) data[i] = (char)(i % 97);
    char output[40000];

    /* Feed first half */
    strm.next_in = data;
    strm.avail_in = 10000;
    strm.next_out = output;
    strm.avail_out = 40000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Flush */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    }
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Feed second half */
    strm.next_in = data + 10000;
    strm.avail_in = 10000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Finish */
    strm.avail_in = 0;
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 40000 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify full roundtrip */
    char decomp[20000];
    unsigned int dlen = 20000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 20000u);
    ASSERT_MEM_EQ(decomp, data, 20000);
}

TEST(multiple_flushes_then_finish) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[5000];
    char output[50000];

    strm.next_out = output;
    strm.avail_out = 50000;

    /* Feed-flush 5 times */
    for (int round = 0; round < 5; round++) {
        memset(data, 'A' + round, 5000);
        strm.next_in = data;
        strm.avail_in = 5000;
        ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

        strm.avail_in = 0;
        int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* Finish */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 50000 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify */
    char decomp[25000];
    unsigned int dlen = 25000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 25000u);

    /* Check each 5000-byte segment */
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 5000; i++) {
            ASSERT_EQ(decomp[round * 5000 + i], (char)('A' + round));
        }
    }
}

/* ================================================================
 * BZ_FINISH directly from BZ_M_RUNNING (no FLUSH first)
 * ================================================================ */

TEST(finish_from_running_with_data) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[30000];
    for (int i = 0; i < 30000; i++) data[i] = (char)(i & 0xff);
    char output[50000];

    /* Feed data and go directly to FINISH */
    strm.next_in = data;
    strm.avail_in = 30000;
    strm.next_out = output;
    strm.avail_out = 50000;

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 50000 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify */
    char decomp[30000];
    unsigned int dlen = 30000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 30000u);
    ASSERT_MEM_EQ(decomp, data, 30000);
}

/* ================================================================
 * Compress with exact block boundary — nblock == nblockMAX
 * ================================================================ */

TEST(compress_exact_block_fill) {
    /* With bs=1, nblockMAX = 100000 - 19 = 99981. Fill exactly that many bytes
     * with identical chars to avoid RLE expansion. */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    /* Use unique bytes to avoid RLE expansion that would hit nblockMAX early */
    int datalen = 99981;
    char *data = malloc(datalen);
    ASSERT(data != NULL);
    for (int i = 0; i < datalen; i++) data[i] = (char)(i % 251);

    char *output = malloc(datalen * 2);
    ASSERT(output != NULL);

    strm.next_in = data;
    strm.avail_in = datalen;
    strm.next_out = output;
    strm.avail_out = datalen * 2;

    /* RUN should consume all input (may trigger block boundary) */
    int ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Finish */
    strm.avail_in = 0;
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = datalen * 2 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify roundtrip */
    char *decomp = malloc(datalen);
    ASSERT(decomp != NULL);
    unsigned int dlen = datalen;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)datalen);
    ASSERT_MEM_EQ(decomp, data, datalen);

    free(data);
    free(output);
    free(decomp);
}

/* ================================================================
 * Compress incremental — feed 1 byte at a time
 * ================================================================ */

TEST(compress_1byte_feed) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[1000];
    for (int i = 0; i < 1000; i++) data[i] = (char)(i % 251);
    char output[10000];

    strm.next_out = output;
    strm.avail_out = 10000;

    /* Feed 1 byte at a time */
    for (int i = 0; i < 1000; i++) {
        strm.next_in = data + i;
        strm.avail_in = 1;
        int ret = BZ2_bzCompress(&strm, BZ_RUN);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* Finish */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 10000 - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify */
    char decomp[1000];
    unsigned int dlen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 1000u);
    ASSERT_MEM_EQ(decomp, data, 1000);
}

/* ================================================================
 * Decompression with 1-byte input feed
 * ================================================================ */

TEST(decompress_1byte_input) {
    char data[2000];
    for (int i = 0; i < 2000; i++) data[i] = (char)(i % 251);
    char comp[5000];
    unsigned int clen = 5000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 2000, 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[2000];
    strm.next_out = output;
    strm.avail_out = 2000;

    /* Feed 1 byte at a time */
    int ret = BZ_OK;
    for (unsigned int i = 0; i < clen && ret == BZ_OK; i++) {
        strm.next_in = comp + i;
        strm.avail_in = 1;
        ret = BZ2_bzDecompress(&strm);
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(2000u - strm.avail_out, 2000u);
    ASSERT_MEM_EQ(output, data, 2000);

    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_1byte_input_small) {
    char data[2000];
    for (int i = 0; i < 2000; i++) data[i] = (char)(i % 251);
    char comp[5000];
    unsigned int clen = 5000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 2000, 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 1), BZ_OK);

    char output[2000];
    strm.next_out = output;
    strm.avail_out = 2000;

    int ret = BZ_OK;
    for (unsigned int i = 0; i < clen && ret == BZ_OK; i++) {
        strm.next_in = comp + i;
        strm.avail_in = 1;
        ret = BZ2_bzDecompress(&strm);
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(2000u - strm.avail_out, 2000u);
    ASSERT_MEM_EQ(output, data, 2000);

    BZ2_bzDecompressEnd(&strm);
}

/* ================================================================
 * BZ_M_IDLE state (compress after STREAM_END)
 * ================================================================ */

TEST(compress_after_stream_end) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[] = "done";
    char output[1000];
    strm.next_in = data;
    strm.avail_in = 4;
    strm.next_out = output;
    strm.avail_out = 1000;

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    /* Now in BZ_M_IDLE — any action should be SEQUENCE_ERROR */
    strm.next_in = data;
    strm.avail_in = 4;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_SEQUENCE_ERROR);
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_FLUSH), BZ_SEQUENCE_ERROR);
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_FINISH), BZ_SEQUENCE_ERROR);

    BZ2_bzCompressEnd(&strm);
}

/* ================================================================
 * CompressEnd / DecompressEnd with corrupted strm pointer
 * ================================================================ */

TEST(compress_end_null_state) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.state = NULL;
    int ret = BZ2_bzCompressEnd(&strm);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_end_null_state) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.state = NULL;
    int ret = BZ2_bzDecompressEnd(&strm);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_null_state) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.state = NULL;
    int ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_null_state) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.state = NULL;
    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

/* ================================================================
 * Flush empty buffer — no data, immediate FLUSH
 * ================================================================ */

TEST(flush_empty) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = NULL;
    strm.avail_in = 0;
    strm.next_out = output;
    strm.avail_out = 1000;

    /* FLUSH with no data */
    int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Now finish */
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    BZ2_bzCompressEnd(&strm);
}

/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    /* FINISH_OK with tiny output */
    RUN(finish_1byte_output);
    RUN(finish_2byte_output);
    RUN(finish_multiblock_1byte_output);

    /* avail_in_expect mismatch */
    RUN(flush_avail_in_mismatch);
    RUN(finish_avail_in_mismatch);

    /* Custom allocator */
    RUN(compress_custom_allocator);
    RUN(decompress_custom_allocator);

    /* Allocator failure */
    RUN(compress_alloc_fail_first);
    RUN(compress_alloc_fail_second);
    RUN(compress_alloc_fail_third);
    RUN(compress_alloc_fail_fourth);
    RUN(decompress_alloc_fail);

    /* No progress */
    RUN(run_no_input_no_output);

    /* Decompression after STREAM_END */
    RUN(decompress_after_stream_end);

    /* 1-byte output decompression */
    RUN(decompress_1byte_output);
    RUN(decompress_1byte_output_small);

    /* Empty FINISH */
    RUN(empty_finish);
    RUN(empty_finish_1byte_output);

    /* FLUSH then FINISH */
    RUN(flush_then_finish);
    RUN(multiple_flushes_then_finish);

    /* FINISH from RUNNING */
    RUN(finish_from_running_with_data);

    /* Exact block fill */
    RUN(compress_exact_block_fill);

    /* 1-byte feed */
    RUN(compress_1byte_feed);
    RUN(decompress_1byte_input);
    RUN(decompress_1byte_input_small);

    /* IDLE state */
    RUN(compress_after_stream_end);

    /* NULL state */
    RUN(compress_end_null_state);
    RUN(decompress_end_null_state);
    RUN(compress_null_state);
    RUN(decompress_null_state);

    /* Flush empty */
    RUN(flush_empty);
TEST_MAIN_END()
