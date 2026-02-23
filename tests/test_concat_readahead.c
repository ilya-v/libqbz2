/*
 * test_concat_readahead.c — Targeted tests for decompression read-ahead
 * correctness at concatenated stream boundaries.
 *
 * The 64-bit decompression bitstream absorbs 4 bytes at a time when
 * avail_in >= 4 && bsLive <= 32. At stream end, excess bytes are
 * returned via next_in/avail_in adjustment. These tests verify that
 * the returned bytes are correct across many boundary alignments, data
 * sizes, and feeding patterns.
 *
 * Scenarios tested:
 *   - Byte-at-a-time decompression of concatenated streams
 *   - Very small payloads (1 byte, 2 bytes) that produce minimal streams
 *   - All block sizes (1-9) in concatenation pairs
 *   - N-stream chains (2, 3, 5, 10)
 *   - Small-mode decompression of concatenated streams
 *   - Mixed feeding patterns (all-at-once vs byte-at-a-time)
 *   - Identical payloads (streams with same compressed representation)
 *   - Reference library comparison for unused byte counts
 */

#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

/* Reference library function pointers for differential comparison */
static int (*ref_bzCompressInit)(bz_stream*, int, int, int);
static int (*ref_bzCompress)(bz_stream*, int);
static int (*ref_bzCompressEnd)(bz_stream*);
static int (*ref_bzDecompressInit)(bz_stream*, int, int);
static int (*ref_bzDecompress)(bz_stream*);
static int (*ref_bzDecompressEnd)(bz_stream*);

static void *ref_lib = NULL;

static void load_ref(void) {
    if (ref_lib) return;
    ref_lib = dlopen("./reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_lib) ref_lib = dlopen("reference/libbz2_ref.so", RTLD_NOW);
    if (!ref_lib) {
        fprintf(stderr, "WARNING: cannot load reference library: %s\n", dlerror());
        return;
    }
    ref_bzCompressInit = dlsym(ref_lib, "BZ2_bzCompressInit");
    ref_bzCompress = dlsym(ref_lib, "BZ2_bzCompress");
    ref_bzCompressEnd = dlsym(ref_lib, "BZ2_bzCompressEnd");
    ref_bzDecompressInit = dlsym(ref_lib, "BZ2_bzDecompressInit");
    ref_bzDecompress = dlsym(ref_lib, "BZ2_bzDecompress");
    ref_bzDecompressEnd = dlsym(ref_lib, "BZ2_bzDecompressEnd");
}

/* Compress data to a malloc'd buffer, returning compressed size */
static char *compress_buf(const char *src, unsigned int srcLen,
                          unsigned int *outLen, int blockSize) {
    unsigned int dlen = srcLen + srcLen/100 + 600;
    char *dst = malloc(dlen);
    if (!dst) return NULL;
    int rc = BZ2_bzBuffToBuffCompress(dst, &dlen, (char*)src, srcLen,
                                      blockSize, 0, 0);
    if (rc != BZ_OK) { free(dst); return NULL; }
    *outLen = dlen;
    return dst;
}

/*
 * Decompress concatenated streams one byte at a time through the
 * streaming API. Returns the number of streams successfully decompressed.
 * Fills outBufs[i]/outLens[i] with each stream's decompressed data.
 */
static int decompress_concat_byte_at_a_time(
    const char *data, unsigned int dataLen,
    char outBufs[][65536], unsigned int outLens[], int maxStreams,
    int small)
{
    int nStreams = 0;
    unsigned int pos = 0;

    while (pos < dataLen && nStreams < maxStreams) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        int ret = BZ2_bzDecompressInit(&strm, 0, small);
        if (ret != BZ_OK) break;

        strm.next_out = outBufs[nStreams];
        strm.avail_out = 65536;

        /* Feed one byte at a time */
        while (pos < dataLen) {
            strm.next_in = (char*)data + pos;
            strm.avail_in = 1;
            ret = BZ2_bzDecompress(&strm);
            if (ret == BZ_STREAM_END) {
                /* The library may have returned excess bytes.
                   But since we fed exactly 1 byte, avail_in should be 0
                   (the byte was consumed or returned). */
                pos += 1;
                /* Adjust: if avail_in > 0, the byte wasn't consumed */
                if (strm.avail_in > 0) {
                    pos -= strm.avail_in;
                }
                break;
            }
            if (ret != BZ_OK) {
                BZ2_bzDecompressEnd(&strm);
                return nStreams; /* error */
            }
            pos += 1;
            if (strm.avail_in > 0) {
                /* Shouldn't happen with 1-byte feed, but be safe */
                pos -= strm.avail_in;
            }
        }

        outLens[nStreams] = 65536 - strm.avail_out;
        BZ2_bzDecompressEnd(&strm);
        nStreams++;
    }

    return nStreams;
}

/*
 * Decompress concatenated streams feeding all data at once.
 * After each BZ_STREAM_END, reinit and continue from where next_in points.
 */
static int decompress_concat_all_at_once(
    const char *data, unsigned int dataLen,
    char outBufs[][65536], unsigned int outLens[], int maxStreams,
    int small)
{
    int nStreams = 0;
    const char *ptr = data;
    unsigned int remaining = dataLen;

    while (remaining > 0 && nStreams < maxStreams) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        int ret = BZ2_bzDecompressInit(&strm, 0, small);
        if (ret != BZ_OK) break;

        strm.next_in = (char*)ptr;
        strm.avail_in = remaining;
        strm.next_out = outBufs[nStreams];
        strm.avail_out = 65536;

        ret = BZ2_bzDecompress(&strm);
        if (ret != BZ_STREAM_END) {
            BZ2_bzDecompressEnd(&strm);
            break;
        }

        outLens[nStreams] = 65536 - strm.avail_out;

        /* Advance past consumed bytes */
        unsigned int consumed = remaining - strm.avail_in;
        ptr += consumed;
        remaining = strm.avail_in;

        BZ2_bzDecompressEnd(&strm);
        nStreams++;
    }

    return nStreams;
}

/*
 * Same as decompress_concat_all_at_once but using reference library.
 */
static int ref_decompress_concat_all_at_once(
    const char *data, unsigned int dataLen,
    char outBufs[][65536], unsigned int outLens[], int maxStreams,
    int small)
{
    if (!ref_lib) return -1;
    int nStreams = 0;
    const char *ptr = data;
    unsigned int remaining = dataLen;

    while (remaining > 0 && nStreams < maxStreams) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        int ret = ref_bzDecompressInit(&strm, 0, small);
        if (ret != BZ_OK) break;

        strm.next_in = (char*)ptr;
        strm.avail_in = remaining;
        strm.next_out = outBufs[nStreams];
        strm.avail_out = 65536;

        ret = ref_bzDecompress(&strm);
        if (ret != BZ_STREAM_END) {
            ref_bzDecompressEnd(&strm);
            break;
        }

        outLens[nStreams] = 65536 - strm.avail_out;
        unsigned int consumed = remaining - strm.avail_in;
        ptr += consumed;
        remaining = strm.avail_in;

        ref_bzDecompressEnd(&strm);
        nStreams++;
    }

    return nStreams;
}

/* ======== TESTS ======== */

/*
 * Test 1: Byte-at-a-time decompression of two concatenated streams
 * with a 1-byte payload in each. This is the minimal case where the
 * read-ahead is most likely to over-consume across the boundary.
 */
TEST(concat_1byte_payloads_byte_feed) {
    char data1 = 'A', data2 = 'B';
    unsigned int clen1, clen2;
    char *c1 = compress_buf(&data1, 1, &clen1, 1);
    char *c2 = compress_buf(&data2, 1, &clen2, 1);
    ASSERT(c1 && c2);

    char *concat = malloc(clen1 + clen2);
    memcpy(concat, c1, clen1);
    memcpy(concat + clen1, c2, clen2);

    char outBufs[2][65536];
    unsigned int outLens[2];
    int n = decompress_concat_byte_at_a_time(concat, clen1 + clen2,
                                              outBufs, outLens, 2, 0);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(outLens[0], 1u);
    ASSERT_EQ(outLens[1], 1u);
    ASSERT_EQ(outBufs[0][0], 'A');
    ASSERT_EQ(outBufs[1][0], 'B');

    free(c1); free(c2); free(concat);
}

/*
 * Test 2: Same as above but in small mode (different BWT path).
 */
TEST(concat_1byte_payloads_byte_feed_small) {
    char data1 = 'X', data2 = 'Y';
    unsigned int clen1, clen2;
    char *c1 = compress_buf(&data1, 1, &clen1, 1);
    char *c2 = compress_buf(&data2, 1, &clen2, 1);
    ASSERT(c1 && c2);

    char *concat = malloc(clen1 + clen2);
    memcpy(concat, c1, clen1);
    memcpy(concat + clen1, c2, clen2);

    char outBufs[2][65536];
    unsigned int outLens[2];
    int n = decompress_concat_byte_at_a_time(concat, clen1 + clen2,
                                              outBufs, outLens, 2, 1);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(outLens[0], 1u);
    ASSERT_EQ(outLens[1], 1u);
    ASSERT_EQ(outBufs[0][0], 'X');
    ASSERT_EQ(outBufs[1][0], 'Y');

    free(c1); free(c2); free(concat);
}

/*
 * Test 3: All block sizes (1-9), concatenate two streams compressed
 * at the same block size, decompress byte-at-a-time.
 */
TEST(concat_all_blocksizes_byte_feed) {
    const char *msg1 = "Hello from stream one!";
    const char *msg2 = "Greetings from stream two!";

    for (int bs = 1; bs <= 9; bs++) {
        unsigned int clen1, clen2;
        char *c1 = compress_buf(msg1, strlen(msg1), &clen1, bs);
        char *c2 = compress_buf(msg2, strlen(msg2), &clen2, bs);
        ASSERT(c1 && c2);

        char *concat = malloc(clen1 + clen2);
        memcpy(concat, c1, clen1);
        memcpy(concat + clen1, c2, clen2);

        char outBufs[2][65536];
        unsigned int outLens[2];
        int n = decompress_concat_byte_at_a_time(concat, clen1 + clen2,
                                                  outBufs, outLens, 2, 0);
        ASSERT_EQ(n, 2);
        ASSERT_EQ(outLens[0], (unsigned int)strlen(msg1));
        ASSERT_EQ(outLens[1], (unsigned int)strlen(msg2));
        ASSERT_MEM_EQ(outBufs[0], msg1, strlen(msg1));
        ASSERT_MEM_EQ(outBufs[1], msg2, strlen(msg2));

        free(c1); free(c2); free(concat);
    }
}

/*
 * Test 4: Mixed block sizes across concatenated streams, byte feed.
 * BS1 + BS9 and BS9 + BS1 (different internal buffer sizes).
 */
TEST(concat_mixed_blocksizes_byte_feed) {
    char data1[200], data2[200];
    for (int i = 0; i < 200; i++) { data1[i] = 'a' + (i % 26); }
    for (int i = 0; i < 200; i++) { data2[i] = 'A' + (i % 26); }

    /* BS1 then BS9 */
    unsigned int clen1, clen2;
    char *c1 = compress_buf(data1, 200, &clen1, 1);
    char *c2 = compress_buf(data2, 200, &clen2, 9);
    ASSERT(c1 && c2);

    char *concat = malloc(clen1 + clen2);
    memcpy(concat, c1, clen1);
    memcpy(concat + clen1, c2, clen2);

    char outBufs[2][65536];
    unsigned int outLens[2];
    int n = decompress_concat_byte_at_a_time(concat, clen1 + clen2,
                                              outBufs, outLens, 2, 0);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(outLens[0], 200u);
    ASSERT_EQ(outLens[1], 200u);
    ASSERT_MEM_EQ(outBufs[0], data1, 200);
    ASSERT_MEM_EQ(outBufs[1], data2, 200);

    free(c1); free(c2); free(concat);

    /* BS9 then BS1 */
    c1 = compress_buf(data1, 200, &clen1, 9);
    c2 = compress_buf(data2, 200, &clen2, 1);
    ASSERT(c1 && c2);

    concat = malloc(clen1 + clen2);
    memcpy(concat, c1, clen1);
    memcpy(concat + clen1, c2, clen2);

    n = decompress_concat_byte_at_a_time(concat, clen1 + clen2,
                                          outBufs, outLens, 2, 0);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(outLens[0], 200u);
    ASSERT_EQ(outLens[1], 200u);
    ASSERT_MEM_EQ(outBufs[0], data1, 200);
    ASSERT_MEM_EQ(outBufs[1], data2, 200);

    free(c1); free(c2); free(concat);
}

/*
 * Test 5: Five concatenated streams, byte-at-a-time feed.
 * Tests that excess byte return works correctly across many boundaries.
 */
TEST(concat_five_streams_byte_feed) {
    const char *msgs[5] = {"one", "two", "three", "four", "five"};
    char *comps[5];
    unsigned int clens[5];
    unsigned int total = 0;

    for (int i = 0; i < 5; i++) {
        comps[i] = compress_buf(msgs[i], strlen(msgs[i]), &clens[i], 1);
        ASSERT(comps[i]);
        total += clens[i];
    }

    char *concat = malloc(total);
    unsigned int off = 0;
    for (int i = 0; i < 5; i++) {
        memcpy(concat + off, comps[i], clens[i]);
        off += clens[i];
    }

    char outBufs[5][65536];
    unsigned int outLens[5];
    int n = decompress_concat_byte_at_a_time(concat, total,
                                              outBufs, outLens, 5, 0);
    ASSERT_EQ(n, 5);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(outLens[i], (unsigned int)strlen(msgs[i]));
        ASSERT_MEM_EQ(outBufs[i], msgs[i], strlen(msgs[i]));
    }

    for (int i = 0; i < 5; i++) free(comps[i]);
    free(concat);
}

/*
 * Test 6: Ten concatenated streams with 2-byte payloads, byte-at-a-time.
 * High stream count with minimal payloads maximizes boundary transitions.
 */
TEST(concat_ten_tiny_streams_byte_feed) {
    char payloads[10][2];
    char *comps[10];
    unsigned int clens[10];
    unsigned int total = 0;

    for (int i = 0; i < 10; i++) {
        payloads[i][0] = '0' + i;
        payloads[i][1] = 'a' + i;
        comps[i] = compress_buf(payloads[i], 2, &clens[i], 1);
        ASSERT(comps[i]);
        total += clens[i];
    }

    char *concat = malloc(total);
    unsigned int off = 0;
    for (int i = 0; i < 10; i++) {
        memcpy(concat + off, comps[i], clens[i]);
        off += clens[i];
    }

    char outBufs[10][65536];
    unsigned int outLens[10];
    int n = decompress_concat_byte_at_a_time(concat, total,
                                              outBufs, outLens, 10, 0);
    ASSERT_EQ(n, 10);
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(outLens[i], 2u);
        ASSERT_MEM_EQ(outBufs[i], payloads[i], 2);
    }

    for (int i = 0; i < 10; i++) free(comps[i]);
    free(concat);
}

/*
 * Test 7: All-at-once feed of concatenated streams, verify unused bytes
 * are returned correctly. Compare qbz2 vs reference library: the number
 * of consumed bytes after each STREAM_END must match exactly.
 */
TEST(concat_unused_bytes_vs_reference) {
    load_ref();
    if (!ref_lib) {
        fprintf(stderr, "  SKIP: reference library not available\n");
        return;
    }

    /* Test with various payload sizes to hit different bit alignments */
    unsigned int sizes[] = {1, 2, 3, 5, 10, 50, 100, 255};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int si = 0; si < nsizes; si++) {
        unsigned int sz = sizes[si];
        char *data = malloc(sz);
        for (unsigned int i = 0; i < sz; i++) data[i] = (char)(i ^ 0x5A);

        unsigned int clen;
        char *comp = compress_buf(data, sz, &clen, 1);
        ASSERT(comp);

        /* Concatenate two copies */
        char *concat = malloc(clen * 2);
        memcpy(concat, comp, clen);
        memcpy(concat + clen, comp, clen);

        /* Decompress first stream with qbz2 */
        bz_stream qs;
        memset(&qs, 0, sizeof(qs));
        ASSERT_EQ(BZ2_bzDecompressInit(&qs, 0, 0), BZ_OK);
        qs.next_in = concat;
        qs.avail_in = clen * 2;
        char qout[65536];
        qs.next_out = qout;
        qs.avail_out = 65536;
        ASSERT_EQ(BZ2_bzDecompress(&qs), BZ_STREAM_END);
        unsigned int q_consumed = (clen * 2) - qs.avail_in;
        unsigned int q_outlen = 65536 - qs.avail_out;
        BZ2_bzDecompressEnd(&qs);

        /* Same with reference */
        bz_stream rs;
        memset(&rs, 0, sizeof(rs));
        ASSERT_EQ(ref_bzDecompressInit(&rs, 0, 0), BZ_OK);
        rs.next_in = concat;
        rs.avail_in = clen * 2;
        char rout[65536];
        rs.next_out = rout;
        rs.avail_out = 65536;
        ASSERT_EQ(ref_bzDecompress(&rs), BZ_STREAM_END);
        unsigned int r_consumed = (clen * 2) - rs.avail_in;
        unsigned int r_outlen = 65536 - rs.avail_out;
        ref_bzDecompressEnd(&rs);

        /* Output must match */
        ASSERT_EQ(q_outlen, r_outlen);
        ASSERT_MEM_EQ(qout, rout, q_outlen);

        /* Consumed bytes must match — this is the critical check */
        if (q_consumed != r_consumed) {
            fprintf(stderr, "  DIVERGENCE: payload size %u: qbz2 consumed %u, ref consumed %u (clen=%u)\n",
                    sz, q_consumed, r_consumed, clen);
        }
        ASSERT_EQ(q_consumed, r_consumed);

        /* Now decompress the second stream from remaining bytes — both must succeed */
        if (qs.avail_in > 0) {
            /* qbz2 second stream */
            memset(&qs, 0, sizeof(qs));
            ASSERT_EQ(BZ2_bzDecompressInit(&qs, 0, 0), BZ_OK);
            qs.next_in = concat + q_consumed;
            qs.avail_in = clen * 2 - q_consumed;
            qs.next_out = qout;
            qs.avail_out = 65536;
            ASSERT_EQ(BZ2_bzDecompress(&qs), BZ_STREAM_END);
            unsigned int q2_outlen = 65536 - qs.avail_out;
            BZ2_bzDecompressEnd(&qs);

            ASSERT_EQ(q2_outlen, sz);
            ASSERT_MEM_EQ(qout, data, sz);
        }

        free(data); free(comp); free(concat);
    }
}

/*
 * Test 8: Byte-at-a-time with various payload sizes to test different
 * bit alignments at the stream trailer. Different payload sizes produce
 * different numbers of Huffman-coded symbols, which means the stream
 * trailer lands at different bit offsets within the last absorbed bytes.
 */
TEST(concat_various_sizes_byte_feed) {
    /* Sizes chosen to hit different bit alignments mod 8 */
    unsigned int sizes[] = {1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 63, 64,
                            100, 127, 128, 200, 255, 256, 500};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int si = 0; si < nsizes; si++) {
        unsigned int sz = sizes[si];
        char *data = malloc(sz);
        for (unsigned int i = 0; i < sz; i++) data[i] = (char)(i * 37 + 13);

        unsigned int clen;
        char *comp = compress_buf(data, sz, &clen, 1);
        ASSERT(comp);

        /* Concatenate with a known second stream */
        char msg2 = 'Z';
        unsigned int clen2;
        char *c2 = compress_buf(&msg2, 1, &clen2, 1);
        ASSERT(c2);

        char *concat = malloc(clen + clen2);
        memcpy(concat, comp, clen);
        memcpy(concat + clen, c2, clen2);

        char outBufs[2][65536];
        unsigned int outLens[2];
        int n = decompress_concat_byte_at_a_time(concat, clen + clen2,
                                                  outBufs, outLens, 2, 0);
        ASSERT_EQ(n, 2);
        ASSERT_EQ(outLens[0], sz);
        ASSERT_MEM_EQ(outBufs[0], data, sz);
        ASSERT_EQ(outLens[1], 1u);
        ASSERT_EQ(outBufs[1][0], 'Z');

        free(data); free(comp); free(c2); free(concat);
    }
}

/*
 * Test 9: Feed exactly N bytes at a time (N=2,3,4,5,6,7,8) to hit
 * different absorption patterns in GET_BITS. The 4-byte bulk path
 * triggers when avail_in >= 4, so feeding 3 bytes forces the 1-byte
 * path, while feeding 4 bytes triggers the bulk path at every call.
 */
TEST(concat_variable_feed_sizes) {
    const char *msg1 = "Feed size test stream one";
    const char *msg2 = "Feed size test stream two";

    unsigned int clen1, clen2;
    char *c1 = compress_buf(msg1, strlen(msg1), &clen1, 1);
    char *c2 = compress_buf(msg2, strlen(msg2), &clen2, 1);
    ASSERT(c1 && c2);

    char *concat = malloc(clen1 + clen2);
    memcpy(concat, c1, clen1);
    memcpy(concat + clen1, c2, clen2);
    unsigned int total = clen1 + clen2;

    int feed_sizes[] = {2, 3, 4, 5, 6, 7, 8};
    int nfeed = sizeof(feed_sizes) / sizeof(feed_sizes[0]);

    for (int fi = 0; fi < nfeed; fi++) {
        int feed = feed_sizes[fi];
        int nStreams = 0;
        unsigned int pos = 0;

        char outputs[2][65536];
        unsigned int outlens[2] = {0, 0};

        while (pos < total && nStreams < 2) {
            bz_stream strm;
            memset(&strm, 0, sizeof(strm));
            ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

            strm.next_out = outputs[nStreams];
            strm.avail_out = 65536;

            int ret = BZ_OK;
            while (pos < total && ret == BZ_OK) {
                unsigned int chunk = (unsigned int)feed;
                if (pos + chunk > total) chunk = total - pos;
                strm.next_in = concat + pos;
                strm.avail_in = chunk;

                ret = BZ2_bzDecompress(&strm);
                unsigned int consumed = chunk - strm.avail_in;
                pos += consumed;
            }

            if (ret == BZ_STREAM_END) {
                outlens[nStreams] = 65536 - strm.avail_out;
                nStreams++;
            }
            BZ2_bzDecompressEnd(&strm);
            if (ret != BZ_STREAM_END) break;
        }

        ASSERT_EQ(nStreams, 2);
        ASSERT_EQ(outlens[0], (unsigned int)strlen(msg1));
        ASSERT_EQ(outlens[1], (unsigned int)strlen(msg2));
        ASSERT_MEM_EQ(outputs[0], msg1, strlen(msg1));
        ASSERT_MEM_EQ(outputs[1], msg2, strlen(msg2));
    }

    free(c1); free(c2); free(concat);
}

/*
 * Test 10: Repeated identical payloads. If the library caches or
 * reuses internal state incorrectly between stream reinits, identical
 * payloads would expose it.
 */
TEST(concat_identical_payloads) {
    const char *msg = "Identical payload test";
    unsigned int clen;
    char *comp = compress_buf(msg, strlen(msg), &clen, 1);
    ASSERT(comp);

    /* Concatenate 5 copies */
    unsigned int total = clen * 5;
    char *concat = malloc(total);
    for (int i = 0; i < 5; i++)
        memcpy(concat + i * clen, comp, clen);

    char outBufs[5][65536];
    unsigned int outLens[5];
    int n = decompress_concat_byte_at_a_time(concat, total,
                                              outBufs, outLens, 5, 0);
    ASSERT_EQ(n, 5);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(outLens[i], (unsigned int)strlen(msg));
        ASSERT_MEM_EQ(outBufs[i], msg, strlen(msg));
    }

    free(comp); free(concat);
}

/*
 * Test 11: All-at-once vs byte-at-a-time must produce identical results
 * for concatenated streams. Both feeding strategies must find the same
 * stream boundaries.
 */
TEST(concat_allonce_vs_byteattime_match) {
    char data1[100], data2[50], data3[75];
    for (int i = 0; i < 100; i++) data1[i] = (char)(i ^ 0xAA);
    for (int i = 0; i < 50; i++) data2[i] = (char)(i ^ 0x55);
    for (int i = 0; i < 75; i++) data3[i] = (char)(i ^ 0xCC);

    unsigned int clens[3];
    char *comps[3];
    comps[0] = compress_buf(data1, 100, &clens[0], 1);
    comps[1] = compress_buf(data2, 50, &clens[1], 5);
    comps[2] = compress_buf(data3, 75, &clens[2], 9);
    ASSERT(comps[0] && comps[1] && comps[2]);

    unsigned int total = clens[0] + clens[1] + clens[2];
    char *concat = malloc(total);
    unsigned int off = 0;
    for (int i = 0; i < 3; i++) {
        memcpy(concat + off, comps[i], clens[i]);
        off += clens[i];
    }

    char aoBufs[3][65536], baBufs[3][65536];
    unsigned int aoLens[3], baLens[3];

    int nao = decompress_concat_all_at_once(concat, total, aoBufs, aoLens, 3, 0);
    int nba = decompress_concat_byte_at_a_time(concat, total, baBufs, baLens, 3, 0);

    ASSERT_EQ(nao, 3);
    ASSERT_EQ(nba, 3);

    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(aoLens[i], baLens[i]);
        ASSERT_MEM_EQ(aoBufs[i], baBufs[i], aoLens[i]);
    }

    /* Verify actual data */
    ASSERT_EQ(aoLens[0], 100u);
    ASSERT_MEM_EQ(aoBufs[0], data1, 100);
    ASSERT_EQ(aoLens[1], 50u);
    ASSERT_MEM_EQ(aoBufs[1], data2, 50);
    ASSERT_EQ(aoLens[2], 75u);
    ASSERT_MEM_EQ(aoBufs[2], data3, 75);

    for (int i = 0; i < 3; i++) free(comps[i]);
    free(concat);
}

/*
 * Test 12: Highly repetitive data (all zeros) — this exercises the
 * RLE and run-length encoding paths heavily, producing a very short
 * compressed stream where the boundary position is critical.
 */
TEST(concat_zeros_byte_feed) {
    char zeros[1000];
    memset(zeros, 0, 1000);

    unsigned int clen;
    char *comp = compress_buf(zeros, 1000, &clen, 1);
    ASSERT(comp);

    /* Two copies concatenated */
    char *concat = malloc(clen * 2);
    memcpy(concat, comp, clen);
    memcpy(concat + clen, comp, clen);

    char outBufs[2][65536];
    unsigned int outLens[2];
    int n = decompress_concat_byte_at_a_time(concat, clen * 2,
                                              outBufs, outLens, 2, 0);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(outLens[0], 1000u);
    ASSERT_EQ(outLens[1], 1000u);

    char expected[1000];
    memset(expected, 0, 1000);
    ASSERT_MEM_EQ(outBufs[0], expected, 1000);
    ASSERT_MEM_EQ(outBufs[1], expected, 1000);

    free(comp); free(concat);
}

/*
 * Test 13: All-at-once differential comparison against reference.
 * For each block size, concatenate two streams and verify qbz2 and
 * reference produce the same stream count, same output, and same
 * consumed-byte counts.
 */
TEST(concat_differential_all_blocksizes) {
    load_ref();
    if (!ref_lib) {
        fprintf(stderr, "  SKIP: reference library not available\n");
        return;
    }

    for (int bs = 1; bs <= 9; bs++) {
        char data1[300], data2[200];
        for (int i = 0; i < 300; i++) data1[i] = (char)(i * 7 + bs);
        for (int i = 0; i < 200; i++) data2[i] = (char)(i * 11 + bs);

        unsigned int clen1, clen2;
        char *c1 = compress_buf(data1, 300, &clen1, bs);
        char *c2 = compress_buf(data2, 200, &clen2, bs);
        ASSERT(c1 && c2);

        char *concat = malloc(clen1 + clen2);
        memcpy(concat, c1, clen1);
        memcpy(concat + clen1, c2, clen2);
        unsigned int total = clen1 + clen2;

        char qBufs[2][65536], rBufs[2][65536];
        unsigned int qLens[2], rLens[2];

        int qn = decompress_concat_all_at_once(concat, total, qBufs, qLens, 2, 0);
        int rn = ref_decompress_concat_all_at_once(concat, total, rBufs, rLens, 2, 0);

        ASSERT_EQ(qn, 2);
        ASSERT_EQ(rn, 2);
        for (int i = 0; i < 2; i++) {
            ASSERT_EQ(qLens[i], rLens[i]);
            ASSERT_MEM_EQ(qBufs[i], rBufs[i], qLens[i]);
        }

        free(c1); free(c2); free(concat);
    }
}

/*
 * Test 14: Concatenated streams through the high-level bzRead/bzWrite
 * FILE* API, with very small payloads.
 */
TEST(concat_fileio_tiny_payloads) {
    const char *path = "/tmp/libqbz2_test_concat_readahead.bz2";

    /* Create three tiny compressed streams */
    char p1[] = "A";
    char p2[] = "BC";
    char p3[] = "DEF";
    unsigned int clens[3];
    char *comps[3];
    comps[0] = compress_buf(p1, 1, &clens[0], 1);
    comps[1] = compress_buf(p2, 2, &clens[1], 1);
    comps[2] = compress_buf(p3, 3, &clens[2], 1);
    ASSERT(comps[0] && comps[1] && comps[2]);

    /* Write concatenated to file */
    FILE *f = fopen(path, "wb");
    ASSERT(f);
    for (int i = 0; i < 3; i++)
        fwrite(comps[i], 1, clens[i], f);
    fclose(f);

    /* Read back using bzReadOpen/bzRead/bzReadGetUnused loop */
    f = fopen(path, "rb");
    ASSERT(f);

    const char *expected[] = {"A", "BC", "DEF"};
    unsigned int expected_lens[] = {1, 2, 3};

    char unused_buf[BZ_MAX_UNUSED];
    int nUnused = 0;
    void *unused_ptr = NULL;

    for (int stream = 0; stream < 3; stream++) {
        int bzerr;
        BZFILE *bz;
        if (stream == 0) {
            bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
        } else {
            bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused_buf, nUnused);
        }
        ASSERT_EQ(bzerr, BZ_OK);
        ASSERT(bz);

        char buf[1000];
        int nr = BZ2_bzRead(&bzerr, bz, buf, 1000);
        ASSERT_EQ(bzerr, BZ_STREAM_END);
        ASSERT_EQ((unsigned int)nr, expected_lens[stream]);
        ASSERT_MEM_EQ(buf, expected[stream], nr);

        /* Get unused bytes for next stream */
        BZ2_bzReadGetUnused(&bzerr, bz, &unused_ptr, &nUnused);
        ASSERT_EQ(bzerr, BZ_OK);
        if (nUnused > 0)
            memcpy(unused_buf, unused_ptr, nUnused);

        BZ2_bzReadClose(&bzerr, bz);
    }

    fclose(f);
    for (int i = 0; i < 3; i++) free(comps[i]);
    remove(path);
}

/*
 * Test 15: Streaming decompression where the output buffer is only
 * 1 byte, forcing many BZ_OK returns before STREAM_END. This tests
 * that the read-ahead doesn't corrupt state across many partial returns.
 */
TEST(concat_1byte_output_buffer) {
    const char *msg1 = "Output buffer one byte";
    const char *msg2 = "Second stream here";
    unsigned int clen1, clen2;
    char *c1 = compress_buf(msg1, strlen(msg1), &clen1, 1);
    char *c2 = compress_buf(msg2, strlen(msg2), &clen2, 1);
    ASSERT(c1 && c2);

    char *concat = malloc(clen1 + clen2);
    memcpy(concat, c1, clen1);
    memcpy(concat + clen1, c2, clen2);
    unsigned int total = clen1 + clen2;

    /* Decompress first stream with 1-byte output buffer */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    strm.next_in = concat;
    strm.avail_in = total;

    char out1[1000];
    unsigned int out1_len = 0;
    int ret;
    do {
        char byte;
        strm.next_out = &byte;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        if (strm.avail_out == 0) {
            out1[out1_len++] = byte;
        }
    } while (ret == BZ_OK);

    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(out1_len, (unsigned int)strlen(msg1));
    ASSERT_MEM_EQ(out1, msg1, out1_len);

    /* Get remaining for second stream */
    unsigned int remaining = strm.avail_in;
    char *rem_ptr = strm.next_in;
    BZ2_bzDecompressEnd(&strm);

    /* Second stream, also 1-byte output */
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    strm.next_in = rem_ptr;
    strm.avail_in = remaining;

    char out2[1000];
    unsigned int out2_len = 0;
    do {
        char byte;
        strm.next_out = &byte;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        if (strm.avail_out == 0) {
            out2[out2_len++] = byte;
        }
    } while (ret == BZ_OK);

    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(out2_len, (unsigned int)strlen(msg2));
    ASSERT_MEM_EQ(out2, msg2, out2_len);

    BZ2_bzDecompressEnd(&strm);
    free(c1); free(c2); free(concat);
}

/*
 * Test 16: Small mode decompression of concatenated streams with
 * all-at-once feed. Small mode uses a different BWT path (GET_LL
 * instead of tt[]).
 */
TEST(concat_small_mode_all_at_once) {
    char data1[500], data2[300];
    for (int i = 0; i < 500; i++) data1[i] = (char)(i % 200);
    for (int i = 0; i < 300; i++) data2[i] = (char)((i * 3) % 200);

    unsigned int clen1, clen2;
    char *c1 = compress_buf(data1, 500, &clen1, 1);
    char *c2 = compress_buf(data2, 300, &clen2, 1);
    ASSERT(c1 && c2);

    char *concat = malloc(clen1 + clen2);
    memcpy(concat, c1, clen1);
    memcpy(concat + clen1, c2, clen2);

    char outBufs[2][65536];
    unsigned int outLens[2];
    int n = decompress_concat_all_at_once(concat, clen1 + clen2,
                                           outBufs, outLens, 2, 1);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(outLens[0], 500u);
    ASSERT_EQ(outLens[1], 300u);
    ASSERT_MEM_EQ(outBufs[0], data1, 500);
    ASSERT_MEM_EQ(outBufs[1], data2, 300);

    free(c1); free(c2); free(concat);
}

/*
 * Test 17: Concatenated stream where the first stream's compressed data
 * length is exactly 4 bytes more than a multiple of 4. This means the
 * 4-byte bulk absorption path will consume the last 4 bytes of stream 1
 * in a single gulp, and some of those bytes may actually be the start
 * of stream 2's header.
 */
TEST(concat_boundary_alignment_stress) {
    /* Try many different small payloads to get various compressed lengths */
    for (int len1 = 1; len1 <= 30; len1++) {
        for (int len2 = 1; len2 <= 5; len2++) {
            char *d1 = malloc(len1);
            char *d2 = malloc(len2);
            for (int i = 0; i < len1; i++) d1[i] = (char)(i + len1);
            for (int i = 0; i < len2; i++) d2[i] = (char)(i + len2 + 100);

            unsigned int clen1, clen2;
            char *c1 = compress_buf(d1, len1, &clen1, 1);
            char *c2 = compress_buf(d2, len2, &clen2, 1);
            if (!c1 || !c2) { free(d1); free(d2); free(c1); free(c2); continue; }

            char *concat = malloc(clen1 + clen2);
            memcpy(concat, c1, clen1);
            memcpy(concat + clen1, c2, clen2);

            /* All-at-once decompress */
            char outBufs[2][65536];
            unsigned int outLens[2];
            int n = decompress_concat_all_at_once(concat, clen1 + clen2,
                                                   outBufs, outLens, 2, 0);
            ASSERT_EQ(n, 2);
            ASSERT_EQ(outLens[0], (unsigned int)len1);
            ASSERT_EQ(outLens[1], (unsigned int)len2);
            ASSERT_MEM_EQ(outBufs[0], d1, len1);
            ASSERT_MEM_EQ(outBufs[1], d2, len2);

            free(d1); free(d2); free(c1); free(c2); free(concat);
        }
    }
}

/*
 * Test 18: Verify that after STREAM_END, total_in reflects the actual
 * bytes consumed (not the bytes pre-absorbed by the 4-byte read-ahead).
 * Compare against reference library.
 */
TEST(concat_total_in_accuracy) {
    load_ref();
    if (!ref_lib) {
        fprintf(stderr, "  SKIP: reference library not available\n");
        return;
    }

    for (int sz = 1; sz <= 100; sz++) {
        char *data = malloc(sz);
        for (int i = 0; i < sz; i++) data[i] = (char)(i ^ sz);

        unsigned int clen;
        char *comp = compress_buf(data, sz, &clen, 1);
        ASSERT(comp);

        /* qbz2 */
        bz_stream qs;
        memset(&qs, 0, sizeof(qs));
        ASSERT_EQ(BZ2_bzDecompressInit(&qs, 0, 0), BZ_OK);
        qs.next_in = comp;
        qs.avail_in = clen;
        char qout[65536];
        qs.next_out = qout;
        qs.avail_out = 65536;
        ASSERT_EQ(BZ2_bzDecompress(&qs), BZ_STREAM_END);
        unsigned int q_total_lo = qs.total_in_lo32;
        unsigned int q_avail = qs.avail_in;
        BZ2_bzDecompressEnd(&qs);

        /* ref */
        bz_stream rs;
        memset(&rs, 0, sizeof(rs));
        ASSERT_EQ(ref_bzDecompressInit(&rs, 0, 0), BZ_OK);
        rs.next_in = comp;
        rs.avail_in = clen;
        char rout[65536];
        rs.next_out = rout;
        rs.avail_out = 65536;
        ASSERT_EQ(ref_bzDecompress(&rs), BZ_STREAM_END);
        unsigned int r_total_lo = rs.total_in_lo32;
        unsigned int r_avail = rs.avail_in;
        ref_bzDecompressEnd(&rs);

        /* total_in must match */
        if (q_total_lo != r_total_lo) {
            fprintf(stderr, "  DIVERGENCE sz=%d: qbz2 total_in=%u, ref total_in=%u\n",
                    sz, q_total_lo, r_total_lo);
        }
        ASSERT_EQ(q_total_lo, r_total_lo);

        /* avail_in must match */
        ASSERT_EQ(q_avail, r_avail);

        free(data); free(comp);
    }
}

/*
 * Test 19: Concatenation where one stream has highly repetitive data
 * (exercises batch CRC path) and the other has random-ish data.
 * Verifies the CRC batch optimization doesn't affect stream boundary
 * detection.
 */
TEST(concat_repetitive_then_random_byte_feed) {
    /* Stream 1: all 'A' (triggers batch CRC in compression, short output) */
    char rep[500];
    memset(rep, 'A', 500);

    /* Stream 2: pseudo-random */
    char rnd[500];
    for (int i = 0; i < 500; i++) rnd[i] = (char)((i * 73 + 17) % 256);

    unsigned int clen1, clen2;
    char *c1 = compress_buf(rep, 500, &clen1, 1);
    char *c2 = compress_buf(rnd, 500, &clen2, 1);
    ASSERT(c1 && c2);

    char *concat = malloc(clen1 + clen2);
    memcpy(concat, c1, clen1);
    memcpy(concat + clen1, c2, clen2);

    char outBufs[2][65536];
    unsigned int outLens[2];
    int n = decompress_concat_byte_at_a_time(concat, clen1 + clen2,
                                              outBufs, outLens, 2, 0);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(outLens[0], 500u);
    ASSERT_EQ(outLens[1], 500u);
    ASSERT_MEM_EQ(outBufs[0], rep, 500);
    ASSERT_MEM_EQ(outBufs[1], rnd, 500);

    free(c1); free(c2); free(concat);
}

/*
 * Test 20: Concatenated empty-ish streams. The bzip2 format doesn't
 * truly support 0-byte payloads (the minimum is 1 block), but we test
 * with single-byte payloads across all block sizes in small mode with
 * byte-at-a-time feed — the most hostile combination.
 */
TEST(concat_minimal_small_all_blocksizes_byte_feed) {
    for (int bs = 1; bs <= 9; bs++) {
        char d1 = (char)bs;
        char d2 = (char)(bs + 10);

        unsigned int clen1, clen2;
        char *c1 = compress_buf(&d1, 1, &clen1, bs);
        char *c2 = compress_buf(&d2, 1, &clen2, bs);
        ASSERT(c1 && c2);

        char *concat = malloc(clen1 + clen2);
        memcpy(concat, c1, clen1);
        memcpy(concat + clen1, c2, clen2);

        char outBufs[2][65536];
        unsigned int outLens[2];
        int n = decompress_concat_byte_at_a_time(concat, clen1 + clen2,
                                                  outBufs, outLens, 2, 1);
        ASSERT_EQ(n, 2);
        ASSERT_EQ(outLens[0], 1u);
        ASSERT_EQ(outLens[1], 1u);
        ASSERT_EQ(outBufs[0][0], d1);
        ASSERT_EQ(outBufs[1][0], d2);

        free(c1); free(c2); free(concat);
    }
}

/* ======== MAIN ======== */

TEST_MAIN_BEGIN()
    RUN(concat_1byte_payloads_byte_feed);
    RUN(concat_1byte_payloads_byte_feed_small);
    RUN(concat_all_blocksizes_byte_feed);
    RUN(concat_mixed_blocksizes_byte_feed);
    RUN(concat_five_streams_byte_feed);
    RUN(concat_ten_tiny_streams_byte_feed);
    RUN(concat_unused_bytes_vs_reference);
    RUN(concat_various_sizes_byte_feed);
    RUN(concat_variable_feed_sizes);
    RUN(concat_identical_payloads);
    RUN(concat_allonce_vs_byteattime_match);
    RUN(concat_zeros_byte_feed);
    RUN(concat_differential_all_blocksizes);
    RUN(concat_fileio_tiny_payloads);
    RUN(concat_1byte_output_buffer);
    RUN(concat_small_mode_all_at_once);
    RUN(concat_boundary_alignment_stress);
    RUN(concat_total_in_accuracy);
    RUN(concat_repetitive_then_random_byte_feed);
    RUN(concat_minimal_small_all_blocksizes_byte_feed);
TEST_MAIN_END()
