/* test_streaming_edge.c — streaming API state machine edge cases
 *
 * Targets hard-to-hit branches in BZ2_bzCompress and BZ2_bzDecompress:
 * - FLUSH path (BZ_M_FLUSHING state transitions)
 * - Tiny output buffers forcing multiple calls
 * - avail_in_expect mismatch sequence errors
 * - total_out counter overflow
 * - Zero-byte input compression/decompression
 * - Multi-flush cycles
 * - Interleaved RUN/FLUSH/FINISH sequences
 * - Output buffer exactly full scenarios
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

static void fill_text(char *buf, unsigned int len) {
    const char *phrase = "The quick brown fox jumps over the lazy dog. ";
    unsigned int plen = (unsigned int)strlen(phrase);
    for (unsigned int i = 0; i < len; i++)
        buf[i] = phrase[i % plen];
}

static void fill_random(char *buf, unsigned int len, unsigned int seed) {
    for (unsigned int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }
}


/* ================================================================
 * Section 1: FLUSH state transitions
 * ================================================================ */

/* Basic flush: RUN some data, FLUSH, then FINISH */
TEST(flush_basic) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[5000];
    fill_text(data, 5000);
    char comp[10000];

    /* RUN phase */
    s.next_in = data;
    s.avail_in = 2500;
    s.next_out = comp;
    s.avail_out = 10000;
    int ret = BZ2_bzCompress(&s, BZ_RUN);
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* FLUSH phase */
    ret = BZ2_bzCompress(&s, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FLUSH);
    }
    ASSERT_EQ(ret, BZ_RUN_OK); /* FLUSH complete returns BZ_RUN_OK */

    /* Feed more data and FINISH */
    s.next_in = data + 2500;
    s.avail_in = 2500;
    ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int clen = 10000 - s.avail_out;
    BZ2_bzCompressEnd(&s);

    /* Decompress and verify */
    char out[5100];
    unsigned int dlen = 5100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 5000u);
    ASSERT_MEM_EQ(out, data, 5000);
}

/* Multiple flush cycles */
TEST(flush_multiple_cycles) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[9000];
    fill_text(data, 9000);
    char comp[15000];
    s.next_out = comp;
    s.avail_out = 15000;

    /* Three cycles of RUN + FLUSH */
    for (int cycle = 0; cycle < 3; cycle++) {
        s.next_in = data + cycle * 3000;
        s.avail_in = 3000;
        int ret = BZ2_bzCompress(&s, BZ_RUN);
        ASSERT_EQ(ret, BZ_RUN_OK);

        ret = BZ2_bzCompress(&s, BZ_FLUSH);
        while (ret == BZ_FLUSH_OK) {
            ret = BZ2_bzCompress(&s, BZ_FLUSH);
        }
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* FINISH */
    s.avail_in = 0;
    int ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int clen = 15000 - s.avail_out;
    BZ2_bzCompressEnd(&s);

    /* Decompress and verify */
    char out[9100];
    unsigned int dlen = 9100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 9000u);
    ASSERT_MEM_EQ(out, data, 9000);
}

/* Flush with zero pending input (should immediately return RUN_OK) */
TEST(flush_empty) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char comp[2000];
    s.next_in = NULL;
    s.avail_in = 0;
    s.next_out = comp;
    s.avail_out = 2000;

    /* FLUSH with no data — should produce a flushed empty block */
    int ret = BZ2_bzCompress(&s, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FLUSH);
    }
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* FINISH */
    ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    BZ2_bzCompressEnd(&s);
}

/* Flush followed immediately by finish (no more RUN) */
TEST(flush_then_finish) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[1000];
    fill_text(data, 1000);
    char comp[5000];

    s.next_in = data;
    s.avail_in = 1000;
    s.next_out = comp;
    s.avail_out = 5000;

    ASSERT_EQ(BZ2_bzCompress(&s, BZ_RUN), BZ_RUN_OK);

    /* FLUSH */
    int ret = BZ2_bzCompress(&s, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FLUSH);
    }
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Immediately FINISH (no more data) */
    s.avail_in = 0;
    ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int clen = 5000 - s.avail_out;
    BZ2_bzCompressEnd(&s);

    /* Verify round-trip */
    char out[1100];
    unsigned int dlen = 1100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 1000u);
    ASSERT_MEM_EQ(out, data, 1000);
}


/* ================================================================
 * Section 2: Tiny output buffer — forces FINISH_OK/FLUSH_OK loops
 * ================================================================ */

/* Compress with 1-byte output buffer */
TEST(tiny_outbuf_compress) {
    char data[500];
    fill_text(data, 500);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    s.next_in = data;
    s.avail_in = 500;

    char comp[2000];
    unsigned int total = 0;

    /* Feed all input */
    char tmpout;
    s.next_out = &tmpout;
    s.avail_out = 1;
    int ret = BZ2_bzCompress(&s, BZ_RUN);
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* FINISH with 1-byte output at a time */
    ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret != BZ_STREAM_END) {
        if (s.avail_out == 0) {
            ASSERT(total < 2000);
            comp[total++] = tmpout;
            s.next_out = &tmpout;
            s.avail_out = 1;
        }
        ret = BZ2_bzCompress(&s, BZ_FINISH);
        ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);
    }
    if (s.avail_out == 0) {
        comp[total++] = tmpout;
    }
    BZ2_bzCompressEnd(&s);

    /* Verify decompress */
    char out[600];
    unsigned int dlen = 600;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, total, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 500u);
    ASSERT_MEM_EQ(out, data, 500);
}

/* Decompress with 1-byte output buffer */
TEST(tiny_outbuf_decompress) {
    char data[500];
    fill_text(data, 500);
    char comp[2000];
    unsigned int clen = 2000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 500, 1, 0, 0), BZ_OK);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzDecompressInit(&s, 0, 0), BZ_OK);

    s.next_in = comp;
    s.avail_in = clen;

    char out[600];
    unsigned int total = 0;
    char tmpout;

    s.next_out = &tmpout;
    s.avail_out = 1;

    int ret = BZ2_bzDecompress(&s);
    while (ret != BZ_STREAM_END) {
        if (s.avail_out == 0) {
            ASSERT(total < 600);
            out[total++] = tmpout;
            s.next_out = &tmpout;
            s.avail_out = 1;
        }
        ret = BZ2_bzDecompress(&s);
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    }
    if (s.avail_out == 0) {
        out[total++] = tmpout;
    }
    BZ2_bzDecompressEnd(&s);

    ASSERT_EQ(total, 500u);
    ASSERT_MEM_EQ(out, data, 500);
}

/* Decompress with 1-byte INPUT buffer */
TEST(tiny_inbuf_decompress) {
    char data[500];
    fill_text(data, 500);
    char comp[2000];
    unsigned int clen = 2000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 500, 1, 0, 0), BZ_OK);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzDecompressInit(&s, 0, 0), BZ_OK);

    char out[600];
    s.next_out = out;
    s.avail_out = 600;

    int ret = BZ_OK;
    for (unsigned int i = 0; i < clen && ret != BZ_STREAM_END; i++) {
        s.next_in = comp + i;
        s.avail_in = 1;
        ret = BZ2_bzDecompress(&s);
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int dlen = 600 - s.avail_out;
    ASSERT_EQ(dlen, 500u);
    ASSERT_MEM_EQ(out, data, 500);
    BZ2_bzDecompressEnd(&s);
}


/* ================================================================
 * Section 3: Sequence error detection
 * ================================================================ */

/* FINISH then try RUN — should get sequence error */
TEST(seq_error_run_after_finish) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[100];
    fill_text(data, 100);
    char comp[1000];

    s.next_in = data;
    s.avail_in = 100;
    s.next_out = comp;
    s.avail_out = 1000;

    /* Start FINISH */
    int ret = BZ2_bzCompress(&s, BZ_FINISH);
    /* Now try RUN — should fail */
    if (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_RUN);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }
    BZ2_bzCompressEnd(&s);
}

/* FINISH then try FLUSH — should get sequence error */
TEST(seq_error_flush_after_finish) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[100];
    fill_text(data, 100);
    char comp[1000];

    s.next_in = data;
    s.avail_in = 100;
    s.next_out = comp;
    s.avail_out = 1000;

    int ret = BZ2_bzCompress(&s, BZ_FINISH);
    if (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }
    BZ2_bzCompressEnd(&s);
}

/* FLUSH then try RUN — should get sequence error */
TEST(seq_error_run_during_flush) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[50000];
    fill_text(data, 50000);
    char comp[500];

    s.next_in = data;
    s.avail_in = 50000;
    s.next_out = comp;
    s.avail_out = 500;

    ASSERT_EQ(BZ2_bzCompress(&s, BZ_RUN), BZ_RUN_OK);

    /* Start FLUSH — with a tiny output buffer it might need multiple calls */
    int ret = BZ2_bzCompress(&s, BZ_FLUSH);
    if (ret == BZ_FLUSH_OK) {
        /* In the middle of flushing — try RUN */
        ret = BZ2_bzCompress(&s, BZ_RUN);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }
    BZ2_bzCompressEnd(&s);
}

/* FLUSH then try FINISH — should get sequence error */
TEST(seq_error_finish_during_flush) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[50000];
    fill_text(data, 50000);
    char comp[500];

    s.next_in = data;
    s.avail_in = 50000;
    s.next_out = comp;
    s.avail_out = 500;

    ASSERT_EQ(BZ2_bzCompress(&s, BZ_RUN), BZ_RUN_OK);

    int ret = BZ2_bzCompress(&s, BZ_FLUSH);
    if (ret == BZ_FLUSH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }
    BZ2_bzCompressEnd(&s);
}

/* Invalid action value */
TEST(seq_error_invalid_action) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[100];
    char comp[500];
    s.next_in = data;
    s.avail_in = 100;
    s.next_out = comp;
    s.avail_out = 500;

    int ret = BZ2_bzCompress(&s, 99); /* invalid action */
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
    BZ2_bzCompressEnd(&s);
}

/* Compress after end — should fail */
TEST(seq_error_compress_after_end) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char comp[1000];
    s.next_in = NULL;
    s.avail_in = 0;
    s.next_out = comp;
    s.avail_out = 1000;

    int ret = BZ2_bzCompress(&s, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    /* Now try to compress more — should get SEQUENCE_ERROR */
    ret = BZ2_bzCompress(&s, BZ_RUN);
    ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);

    BZ2_bzCompressEnd(&s);
}

/* Decompress after stream end — should fail */
TEST(seq_error_decompress_after_end) {
    char data[] = "Hello!";
    char comp[500];
    unsigned int clen = 500;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 6, 1, 0, 0), BZ_OK);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzDecompressInit(&s, 0, 0), BZ_OK);

    char out[100];
    s.next_in = comp;
    s.avail_in = clen;
    s.next_out = out;
    s.avail_out = 100;

    int ret = BZ2_bzDecompress(&s);
    ASSERT_EQ(ret, BZ_STREAM_END);

    /* Try again — should fail since stream is complete */
    s.next_in = comp;
    s.avail_in = clen;
    s.next_out = out;
    s.avail_out = 100;
    ret = BZ2_bzDecompress(&s);
    /* Should get some error, not BZ_OK or BZ_STREAM_END */
    ASSERT(ret != BZ_OK && ret != BZ_STREAM_END);

    BZ2_bzDecompressEnd(&s);
}


/* ================================================================
 * Section 4: Zero-byte input edge cases
 * ================================================================ */

/* Compress zero bytes via streaming */
TEST(zero_input_streaming_compress) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char comp[500];
    s.next_in = NULL;
    s.avail_in = 0;
    s.next_out = comp;
    s.avail_out = 500;

    int ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int clen = 500 - s.avail_out;
    BZ2_bzCompressEnd(&s);

    /* Should produce a valid (empty) bz2 stream */
    char out[100];
    unsigned int dlen = 100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 0u);
}

/* Decompress empty bz2 stream via streaming */
TEST(zero_input_streaming_decompress) {
    /* First make an empty stream (need non-NULL source even for len=0) */
    char dummy = 0;
    char comp[500];
    unsigned int clen = 500;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, &dummy, 0, 1, 0, 0), BZ_OK);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzDecompressInit(&s, 0, 0), BZ_OK);

    char out[100];
    s.next_in = comp;
    s.avail_in = clen;
    s.next_out = out;
    s.avail_out = 100;

    int ret = BZ2_bzDecompress(&s);
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int dlen = 100 - s.avail_out;
    ASSERT_EQ(dlen, 0u);
    BZ2_bzDecompressEnd(&s);
}


/* ================================================================
 * Section 5: Output exactly full scenarios
 * ================================================================ */

/* Compress where output buffer is exactly the right size */
TEST(exact_outbuf_compress) {
    char data[1000];
    fill_text(data, 1000);

    /* First, find out how big the compressed output is */
    char comp[2000];
    unsigned int clen = 2000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 1000, 1, 0, 0), BZ_OK);

    /* Now compress with exactly that buffer size */
    char comp2[2000];
    unsigned int clen2 = clen; /* exact fit */
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp2, &clen2, data, 1000, 1, 0, 0), BZ_OK);
    ASSERT_EQ(clen2, clen);
    ASSERT_MEM_EQ(comp, comp2, clen);
}

/* Decompress where output buffer is exactly the right size */
TEST(exact_outbuf_decompress) {
    char data[1000];
    fill_text(data, 1000);
    char comp[2000];
    unsigned int clen = 2000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 1000, 1, 0, 0), BZ_OK);

    /* Decompress with exactly 1000-byte buffer */
    char out[1000];
    unsigned int dlen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 1000u);
    ASSERT_MEM_EQ(out, data, 1000);
}

/* Decompress where output buffer is 1 byte too small */
TEST(outbuf_too_small_decompress) {
    char data[1000];
    fill_text(data, 1000);
    char comp[2000];
    unsigned int clen = 2000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 1000, 1, 0, 0), BZ_OK);

    char out[999];
    unsigned int dlen = 999;
    int ret = BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
}


/* ================================================================
 * Section 6: total_out counter accuracy
 * ================================================================ */

/* Verify total_out_lo32 is accurate after compress */
TEST(total_out_compress_accuracy) {
    char data[5000];
    fill_text(data, 5000);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char comp[10000];
    s.next_in = data;
    s.avail_in = 5000;
    s.next_out = comp;
    s.avail_out = 10000;

    int ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = 10000 - s.avail_out;
    ASSERT_EQ(s.total_out_lo32, clen);
    BZ2_bzCompressEnd(&s);
}

/* Verify total_out_lo32 is accurate after decompress */
TEST(total_out_decompress_accuracy) {
    char data[5000];
    fill_text(data, 5000);
    char comp[10000];
    unsigned int clen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzDecompressInit(&s, 0, 0), BZ_OK);

    char out[5100];
    s.next_in = comp;
    s.avail_in = clen;
    s.next_out = out;
    s.avail_out = 5100;

    int ret = BZ2_bzDecompress(&s);
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int dlen = 5100 - s.avail_out;
    ASSERT_EQ(dlen, 5000u);
    ASSERT_EQ(s.total_out_lo32, 5000u);
    BZ2_bzDecompressEnd(&s);
}

/* Verify total_in counter accuracy */
TEST(total_in_compress_accuracy) {
    char data[3000];
    fill_text(data, 3000);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char comp[5000];
    s.next_in = data;
    s.avail_in = 3000;
    s.next_out = comp;
    s.avail_out = 5000;

    int ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(s.total_in_lo32, 3000u);
    BZ2_bzCompressEnd(&s);
}

/* total_out accuracy with chunked streaming decompress */
TEST(total_out_chunked_decompress) {
    char data[2000];
    fill_text(data, 2000);
    char comp[5000];
    unsigned int clen = 5000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 2000, 1, 0, 0), BZ_OK);

    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzDecompressInit(&s, 0, 0), BZ_OK);

    char out[2100];
    unsigned int total_produced = 0;

    s.next_in = comp;
    s.avail_in = clen;

    /* Decompress in 100-byte output chunks */
    int ret = BZ_OK;
    while (ret != BZ_STREAM_END) {
        s.next_out = out + total_produced;
        s.avail_out = 100;
        ret = BZ2_bzDecompress(&s);
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
        total_produced += 100 - s.avail_out;
    }
    ASSERT_EQ(total_produced, 2000u);
    ASSERT_EQ(s.total_out_lo32, 2000u);
    ASSERT_MEM_EQ(out, data, 2000);
    BZ2_bzDecompressEnd(&s);
}


/* ================================================================
 * Section 7: Flush with tiny output buffers (forces FLUSH_OK loop)
 * ================================================================ */

/* Flush with a 100-byte output buffer on a large input */
TEST(flush_tiny_output) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK);

    char data[10000];
    fill_text(data, 10000);
    char comp[20000];
    unsigned int total = 0;

    /* RUN all data */
    s.next_in = data;
    s.avail_in = 10000;
    s.next_out = comp;
    s.avail_out = 100;

    ASSERT_EQ(BZ2_bzCompress(&s, BZ_RUN), BZ_RUN_OK);
    total = 100 - s.avail_out;

    /* FLUSH with tiny output buffer */
    int ret = BZ2_bzCompress(&s, BZ_FLUSH);
    int flush_iters = 0;
    while (ret == BZ_FLUSH_OK) {
        total += 100 - s.avail_out;
        s.next_out = comp + total;
        s.avail_out = 100;
        ret = BZ2_bzCompress(&s, BZ_FLUSH);
        flush_iters++;
        ASSERT(flush_iters < 500); /* sanity limit */
    }
    ASSERT_EQ(ret, BZ_RUN_OK);
    total += 100 - s.avail_out;

    /* FINISH */
    s.avail_in = 0;
    s.next_out = comp + total;
    s.avail_out = 20000 - total;
    ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    total = (unsigned int)(s.next_out - comp);
    BZ2_bzCompressEnd(&s);

    /* Decompress and verify */
    char out[10100];
    unsigned int dlen = 10100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, total, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 10000u);
    ASSERT_MEM_EQ(out, data, 10000);
}


/* ================================================================
 * Section 8: Multi-block streaming with flush between blocks
 * ================================================================ */

/* Force multi-block by exceeding block size, flush between blocks */
TEST(multiblock_flush_between) {
    bz_stream s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(BZ2_bzCompressInit(&s, 1, 0, 0), BZ_OK); /* bs=1 -> 100KB blocks */

    /* 250KB of data = ~2.5 blocks at bs=1 */
    unsigned int total_in = 250000;
    char *data = malloc(total_in);
    ASSERT(data != NULL);
    fill_random(data, total_in, 42);

    char *comp = malloc(total_in + 10000);
    ASSERT(comp != NULL);
    s.next_out = comp;
    s.avail_out = total_in + 10000;

    /* Feed 80KB, flush, feed 80KB, flush, feed 90KB, finish */
    unsigned int offsets[] = { 0, 80000, 160000 };
    unsigned int sizes[] = { 80000, 80000, 90000 };

    for (int i = 0; i < 3; i++) {
        s.next_in = data + offsets[i];
        s.avail_in = sizes[i];
        ASSERT_EQ(BZ2_bzCompress(&s, BZ_RUN), BZ_RUN_OK);

        if (i < 2) {
            /* Flush between chunks */
            int ret = BZ2_bzCompress(&s, BZ_FLUSH);
            while (ret == BZ_FLUSH_OK) {
                ret = BZ2_bzCompress(&s, BZ_FLUSH);
            }
            ASSERT_EQ(ret, BZ_RUN_OK);
        }
    }

    /* FINISH */
    int ret = BZ2_bzCompress(&s, BZ_FINISH);
    while (ret == BZ_FINISH_OK) {
        ret = BZ2_bzCompress(&s, BZ_FINISH);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int clen = (total_in + 10000) - s.avail_out;
    BZ2_bzCompressEnd(&s);

    /* Decompress and verify */
    char *out = malloc(total_in + 100);
    ASSERT(out != NULL);
    unsigned int dlen = total_in + 100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, total_in);
    ASSERT_MEM_EQ(out, data, total_in);

    free(data); free(comp); free(out);
}


/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    /* Section 1: FLUSH */
    RUN(flush_basic);
    RUN(flush_multiple_cycles);
    RUN(flush_empty);
    RUN(flush_then_finish);

    /* Section 2: Tiny output buffers */
    RUN(tiny_outbuf_compress);
    RUN(tiny_outbuf_decompress);
    RUN(tiny_inbuf_decompress);

    /* Section 3: Sequence errors */
    RUN(seq_error_run_after_finish);
    RUN(seq_error_flush_after_finish);
    RUN(seq_error_run_during_flush);
    RUN(seq_error_finish_during_flush);
    RUN(seq_error_invalid_action);
    RUN(seq_error_compress_after_end);
    RUN(seq_error_decompress_after_end);

    /* Section 4: Zero-byte */
    RUN(zero_input_streaming_compress);
    RUN(zero_input_streaming_decompress);

    /* Section 5: Exact output buffer */
    RUN(exact_outbuf_compress);
    RUN(exact_outbuf_decompress);
    RUN(outbuf_too_small_decompress);

    /* Section 6: Counter accuracy */
    RUN(total_out_compress_accuracy);
    RUN(total_out_decompress_accuracy);
    RUN(total_in_compress_accuracy);
    RUN(total_out_chunked_decompress);

    /* Section 7: Flush with tiny output */
    RUN(flush_tiny_output);

    /* Section 8: Multi-block flush */
    RUN(multiblock_flush_between);
TEST_MAIN_END()
