/* test_crc32_internal.c — CRC-32 implementation correctness tests
 *
 * Exercises BZ2_crc32_update() across all code paths:
 * - PCLMULQDQ hardware path (len >= 64)
 * - Slicing-by-8 software path
 * - Byte-at-a-time alignment and tail
 * - Various buffer sizes from 0 to 4096+
 * - Aligned and misaligned buffers
 * - Incremental vs one-shot computation
 * - Known test vectors
 *
 * All results are verified against a byte-at-a-time reference using
 * the BZ_UPDATE_CRC macro to ensure the optimised paths match exactly.
 */
#include "test_framework.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Internal header for BZ2_crc32_update and BZ2_crc32Table */
#include "qbz2_internal.h"

/* Reference CRC computation: byte-at-a-time using the public table.
 * This is the canonical bzip2 CRC that all optimised paths must match. */
static UInt32 crc32_reference(UInt32 crc, const UChar *buf, UInt32 len)
{
    for (UInt32 i = 0; i < len; i++) {
        crc = (crc << 8) ^ BZ2_crc32Table[(crc >> 24) ^ buf[i]];
    }
    return crc;
}

/* Full bzip2 CRC: init, update, finalise */
static UInt32 crc32_full_ref(const UChar *buf, UInt32 len)
{
    UInt32 crc = 0xFFFFFFFFU;
    crc = crc32_reference(crc, buf, len);
    return crc ^ 0xFFFFFFFFU;
}

static UInt32 crc32_full_opt(const UChar *buf, UInt32 len)
{
    UInt32 crc = 0xFFFFFFFFU;
    crc = BZ2_crc32_update(crc, buf, len);
    return crc ^ 0xFFFFFFFFU;
}

/* Helper: fill buffer with deterministic pseudo-random data */
static void fill_prng(UChar *buf, UInt32 len, UInt32 seed)
{
    for (UInt32 i = 0; i < len; i++) {
        seed = seed * 1103515245U + 12345U;
        buf[i] = (UChar)(seed >> 16);
    }
}

/* ================================================================
 * Basic correctness: verify optimised matches reference for many sizes
 * ================================================================ */

TEST(crc32_empty) {
    UInt32 crc_ref = crc32_full_ref(NULL, 0);
    /* For empty input, init ^ final = 0xFFFFFFFF ^ 0xFFFFFFFF = 0 */
    UInt32 crc_opt = 0xFFFFFFFFU;
    crc_opt = BZ2_crc32_update(crc_opt, (const UChar *)"", 0);
    crc_opt ^= 0xFFFFFFFFU;
    ASSERT_EQ(crc_opt, crc_ref);
}

TEST(crc32_single_byte) {
    for (int b = 0; b < 256; b++) {
        UChar byte = (UChar)b;
        UInt32 ref = crc32_full_ref(&byte, 1);
        UInt32 opt = crc32_full_opt(&byte, 1);
        ASSERT_EQ(opt, ref);
    }
}

TEST(crc32_sizes_1_to_128) {
    /* Test every size from 1 to 128 — this crosses the PCLMULQDQ threshold (64)
     * and exercises alignment/tail handling at every offset. */
    UChar buf[128];
    fill_prng(buf, 128, 42);

    for (UInt32 len = 1; len <= 128; len++) {
        UInt32 ref = crc32_full_ref(buf, len);
        UInt32 opt = crc32_full_opt(buf, len);
        ASSERT_EQ(opt, ref);
    }
}

TEST(crc32_pclmul_boundary_sizes) {
    /* Sizes around the PCLMULQDQ threshold and internal block boundaries:
     * 63, 64, 65, 79, 80, 81, 127, 128, 129, 191, 192, 193 */
    static const UInt32 sizes[] = {
        63, 64, 65, 79, 80, 81, 95, 96, 97,
        127, 128, 129, 191, 192, 193, 255, 256, 257,
        511, 512, 513, 1023, 1024, 1025, 2048, 4096
    };
    UChar *buf = malloc(4096);
    ASSERT(buf != NULL);
    fill_prng(buf, 4096, 12345);

    for (int i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++) {
        UInt32 len = sizes[i];
        UInt32 ref = crc32_full_ref(buf, len);
        UInt32 opt = crc32_full_opt(buf, len);
        ASSERT_EQ(opt, ref);
    }
    free(buf);
}

/* ================================================================
 * Alignment tests: misaligned buffers stress alignment handling
 * ================================================================ */

TEST(crc32_misaligned_1_through_7) {
    /* Allocate oversized buffer and test at each misalignment */
    UChar backing[4096 + 16];
    fill_prng(backing, sizeof(backing), 99);

    for (int offset = 0; offset < 8; offset++) {
        UChar *buf = backing + offset;
        for (UInt32 len = 64; len <= 512; len += 64) {
            UInt32 ref = crc32_full_ref(buf, len);
            UInt32 opt = crc32_full_opt(buf, len);
            ASSERT_EQ(opt, ref);
        }
    }
}

/* ================================================================
 * Incremental computation: split buffer and verify CRC matches
 * ================================================================ */

TEST(crc32_incremental_split) {
    /* Compute CRC in one shot, then in two parts, verify they match */
    UChar buf[1024];
    fill_prng(buf, 1024, 7777);

    UInt32 one_shot = BZ2_crc32_update(0xFFFFFFFFU, buf, 1024);

    /* Split at every power-of-2 boundary */
    static const UInt32 splits[] = { 1, 2, 4, 8, 16, 32, 63, 64, 65, 128, 256, 512 };
    for (int i = 0; i < (int)(sizeof(splits) / sizeof(splits[0])); i++) {
        UInt32 split = splits[i];
        if (split >= 1024) continue;
        UInt32 crc = BZ2_crc32_update(0xFFFFFFFFU, buf, split);
        crc = BZ2_crc32_update(crc, buf + split, 1024 - split);
        ASSERT_EQ(crc, one_shot);
    }
}

TEST(crc32_incremental_byte_at_a_time) {
    /* Feed one byte at a time, should match bulk computation */
    UChar buf[256];
    fill_prng(buf, 256, 31337);

    UInt32 bulk = BZ2_crc32_update(0xFFFFFFFFU, buf, 256);

    UInt32 incr = 0xFFFFFFFFU;
    for (int i = 0; i < 256; i++) {
        incr = BZ2_crc32_update(incr, buf + i, 1);
    }
    ASSERT_EQ(incr, bulk);
}

/* ================================================================
 * Data pattern tests: various patterns that might expose issues
 * ================================================================ */

TEST(crc32_all_zeros) {
    UChar buf[512];
    memset(buf, 0, 512);
    for (UInt32 len = 1; len <= 512; len *= 2) {
        UInt32 ref = crc32_full_ref(buf, len);
        UInt32 opt = crc32_full_opt(buf, len);
        ASSERT_EQ(opt, ref);
    }
}

TEST(crc32_all_ones) {
    UChar buf[512];
    memset(buf, 0xFF, 512);
    for (UInt32 len = 1; len <= 512; len *= 2) {
        UInt32 ref = crc32_full_ref(buf, len);
        UInt32 opt = crc32_full_opt(buf, len);
        ASSERT_EQ(opt, ref);
    }
}

TEST(crc32_ascending) {
    UChar buf[512];
    for (int i = 0; i < 512; i++) buf[i] = (UChar)(i & 0xFF);
    for (UInt32 len = 1; len <= 512; len *= 2) {
        UInt32 ref = crc32_full_ref(buf, len);
        UInt32 opt = crc32_full_opt(buf, len);
        ASSERT_EQ(opt, ref);
    }
}

TEST(crc32_alternating) {
    UChar buf[512];
    for (int i = 0; i < 512; i++) buf[i] = (i & 1) ? 0xFF : 0x00;
    for (UInt32 len = 1; len <= 512; len *= 2) {
        UInt32 ref = crc32_full_ref(buf, len);
        UInt32 opt = crc32_full_opt(buf, len);
        ASSERT_EQ(opt, ref);
    }
}

/* ================================================================
 * Initial CRC value tests: non-standard init values
 * ================================================================ */

TEST(crc32_nonstandard_init) {
    /* BZ2_crc32_update must work correctly with any initial CRC value,
     * not just 0xFFFFFFFF. This matters for streaming/incremental use. */
    UChar buf[256];
    fill_prng(buf, 256, 5555);

    static const UInt32 inits[] = {
        0x00000000, 0xFFFFFFFF, 0xDEADBEEF, 0x12345678,
        0x80000000, 0x00000001, 0x7FFFFFFF, 0xAAAAAAAA
    };
    for (int i = 0; i < (int)(sizeof(inits) / sizeof(inits[0])); i++) {
        UInt32 ref = crc32_reference(inits[i], buf, 256);
        UInt32 opt = BZ2_crc32_update(inits[i], buf, 256);
        ASSERT_EQ(opt, ref);
    }
}

/* ================================================================
 * Large buffer test: exercise the 64-byte loop multiple iterations
 * ================================================================ */

TEST(crc32_large_buffer) {
    /* 64KB buffer exercises multiple iterations of the PCLMULQDQ main loop */
    UInt32 len = 65536;
    UChar *buf = malloc(len);
    ASSERT(buf != NULL);
    fill_prng(buf, len, 0xCAFE);

    UInt32 ref = crc32_full_ref(buf, len);
    UInt32 opt = crc32_full_opt(buf, len);
    ASSERT_EQ(opt, ref);

    free(buf);
}

/* ================================================================
 * Tail bytes test: exercise remaining bytes after PCLMULQDQ main loop
 * ================================================================ */

TEST(crc32_pclmul_tail_bytes) {
    /* The PCLMULQDQ path processes 64 bytes at a time, then 16 bytes at a time,
     * then has a byte-at-a-time tail. Test sizes that leave 1-15 tail bytes. */
    UChar *buf = malloc(256);
    ASSERT(buf != NULL);
    fill_prng(buf, 256, 8888);

    for (UInt32 tail = 1; tail <= 15; tail++) {
        /* 64 (PCLMULQDQ initial) + 16 * N (block loop) + tail */
        for (int n = 0; n < 3; n++) {
            UInt32 len = 64 + (UInt32)(16 * n) + tail;
            UInt32 ref = crc32_full_ref(buf, len);
            UInt32 opt = crc32_full_opt(buf, len);
            ASSERT_EQ(opt, ref);
        }
    }
    free(buf);
}

/* ================================================================
 * Known test vector: "123456789" is the standard CRC-32 check value
 * ================================================================ */

TEST(crc32_known_vector_bzip2) {
    /* bzip2 uses CRC-32 with polynomial 0x04C11DB7, MSB-first convention.
     * The check value for ASCII "123456789" with init=0xFFFFFFFF and
     * final XOR=0xFFFFFFFF is 0xFC891918 (AUTODIN-II, non-reflected). */
    const UChar *data = (const UChar *)"123456789";
    UInt32 crc = crc32_full_opt(data, 9);
    UInt32 ref = crc32_full_ref(data, 9);
    ASSERT_EQ(crc, ref);
    ASSERT_EQ(crc, 0xFC891918U);
}

/* ================================================================
 * Multi-call chaining: verify CRC is correctly threaded through calls
 * ================================================================ */

TEST(crc32_three_way_split) {
    /* Split a 300-byte buffer into 3 parts at various boundaries */
    UChar buf[300];
    fill_prng(buf, 300, 2024);

    UInt32 ref = BZ2_crc32_update(0xFFFFFFFFU, buf, 300);

    /* Split: 100/100/100 */
    UInt32 crc = BZ2_crc32_update(0xFFFFFFFFU, buf, 100);
    crc = BZ2_crc32_update(crc, buf + 100, 100);
    crc = BZ2_crc32_update(crc, buf + 200, 100);
    ASSERT_EQ(crc, ref);

    /* Split: 1/64/235 (crosses PCLMULQDQ threshold in middle) */
    crc = BZ2_crc32_update(0xFFFFFFFFU, buf, 1);
    crc = BZ2_crc32_update(crc, buf + 1, 64);
    crc = BZ2_crc32_update(crc, buf + 65, 235);
    ASSERT_EQ(crc, ref);

    /* Split: 63/1/236 (just below threshold, then single byte, then large) */
    crc = BZ2_crc32_update(0xFFFFFFFFU, buf, 63);
    crc = BZ2_crc32_update(crc, buf + 63, 1);
    crc = BZ2_crc32_update(crc, buf + 64, 236);
    ASSERT_EQ(crc, ref);
}

/* ================================================================
 * End-to-end: compress and decompress, verify CRC embedded in stream
 * ================================================================ */

TEST(crc32_roundtrip_verification) {
    /* Compress data, then decompress and verify output matches.
     * The bzip2 format embeds CRC-32 in the stream — if our CRC
     * computation is wrong, decompression will fail with BZ_DATA_ERROR. */
    UChar input[4096];
    fill_prng(input, sizeof(input), 1234);

    char compressed[8192];
    unsigned int cLen = sizeof(compressed);
    int rc = BZ2_bzBuffToBuffCompress(compressed, &cLen,
                                       (char *)input, sizeof(input),
                                       1, 0, 0);
    ASSERT_EQ(rc, BZ_OK);

    char output[4096];
    unsigned int oLen = sizeof(output);
    rc = BZ2_bzBuffToBuffDecompress(output, &oLen,
                                     compressed, cLen,
                                     0, 0);
    ASSERT_EQ(rc, BZ_OK);
    ASSERT_EQ(oLen, sizeof(input));
    ASSERT(memcmp(output, input, sizeof(input)) == 0);
}

/* ================================================================
 * Test suite registration
 * ================================================================ */

TEST_MAIN_BEGIN()
    RUN(crc32_empty);
    RUN(crc32_single_byte);
    RUN(crc32_sizes_1_to_128);
    RUN(crc32_pclmul_boundary_sizes);
    RUN(crc32_misaligned_1_through_7);
    RUN(crc32_incremental_split);
    RUN(crc32_incremental_byte_at_a_time);
    RUN(crc32_all_zeros);
    RUN(crc32_all_ones);
    RUN(crc32_ascending);
    RUN(crc32_alternating);
    RUN(crc32_nonstandard_init);
    RUN(crc32_large_buffer);
    RUN(crc32_pclmul_tail_bytes);
    RUN(crc32_known_vector_bzip2);
    RUN(crc32_three_way_split);
    RUN(crc32_roundtrip_verification);
TEST_MAIN_END()
