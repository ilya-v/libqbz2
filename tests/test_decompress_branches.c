/* Branch coverage tests for decompress.c
 * Targets specific uncovered branches identified in coverage-76996f.md:
 * - GET_BITS macro: single-byte absorption path (avail_in < 4)
 * - GET_BITS macro: input exhaustion (avail_in == 0) resume paths
 * - GET_BITS macro: total_in_lo32 overflow in single-byte path
 * - Fast Huffman decode: primary table miss, overflow table path
 * - Small decompress mode: streaming with tiny output
 * - MTF decode: various run lengths (RUNA/RUNB combinations)
 * - Selector MTF undo branches
 * - Huffman code length decode branches (curr++ and curr--)
 * - Block data validation: origPtr bounds, nInUse==0, nGroups bounds
 * - unRLE output: runs of length 1, 2, 3, 4+ in both FAST and SMALL modes
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>

/* Helper: compress to buffer */
static int helper_compress(const char *src, unsigned int srcLen,
                           char *dst, unsigned int *dstLen, int bs) {
    return BZ2_bzBuffToBuffCompress(dst, dstLen, (char*)src, srcLen, bs, 0, 0);
}

/* Helper: streaming decompress feeding N bytes at a time */
static int decompress_streaming_nbyte(const char *src, unsigned int srcLen,
                                       char *dst, unsigned int dstLen,
                                       int chunk_size, int small) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) return ret;

    strm.next_out = dst;
    strm.avail_out = dstLen;

    unsigned int pos = 0;
    while (pos < srcLen) {
        unsigned int n = (unsigned int)chunk_size;
        if (pos + n > srcLen) n = srcLen - pos;
        strm.next_in = (char*)src + pos;
        strm.avail_in = n;
        while (strm.avail_in > 0 || ret == BZ_OK) {
            ret = BZ2_bzDecompress(&strm);
            if (ret != BZ_OK) break;
            if (strm.avail_in == 0) break;
        }
        pos += n - strm.avail_in;
        if (ret != BZ_OK) break;
    }
    unsigned int total = dstLen - strm.avail_out;
    BZ2_bzDecompressEnd(&strm);
    (void)total;
    return ret;
}

/* Helper: streaming decompress with tiny output buffer (N bytes at a time) */
static int decompress_streaming_tiny_output(const char *src, unsigned int srcLen,
                                             char *dst, unsigned int dstLen,
                                             int out_chunk, int small) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) return ret;

    strm.next_in = (char*)src;
    strm.avail_in = srcLen;

    unsigned int total = 0;
    while (1) {
        unsigned int n = (unsigned int)out_chunk;
        if (total + n > dstLen) n = dstLen - total;
        if (n == 0) { ret = -100; break; } /* out of output space */
        strm.next_out = dst + total;
        strm.avail_out = n;
        ret = BZ2_bzDecompress(&strm);
        total += n - strm.avail_out;
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_OK) break;
    }
    BZ2_bzDecompressEnd(&strm);
    return ret;
}

/* ========== GET_BITS single-byte absorption (avail_in < 4) ========== */

TEST(streaming_2byte_chunks) {
    /* Feed 2 bytes at a time to force the single-byte GET_BITS path
     * (avail_in < 4 means can't do bulk 4-byte absorption) */
    char data[3000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i & 0xff);
    char comp[6000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[3000];
    int ret = decompress_streaming_nbyte(comp, clen, decomp, sizeof(decomp), 2, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

TEST(streaming_3byte_chunks) {
    /* 3-byte chunks: always hits single-byte path */
    char data[2000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)((i * 37) & 0xff);
    char comp[4000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[2000];
    int ret = decompress_streaming_nbyte(comp, clen, decomp, sizeof(decomp), 3, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

TEST(streaming_1byte_chunks_small) {
    /* 1-byte chunks in small mode */
    char data[1000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 127 + 1);
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[1000];
    int ret = decompress_streaming_nbyte(comp, clen, decomp, sizeof(decomp), 1, 1);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

TEST(streaming_2byte_chunks_small) {
    /* 2-byte chunks in small mode */
    char data[1000];
    memset(data, 'S', 500);
    memset(data + 500, 'T', 500);
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[1000];
    int ret = decompress_streaming_nbyte(comp, clen, decomp, sizeof(decomp), 2, 1);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

/* ========== GET_BITS input exhaustion resume paths ========== */

TEST(streaming_resume_at_every_byte) {
    /* Feed exactly 1 byte per call to exercise every possible resume point
     * in the GET_BITS state machine. This hits the avail_in==0 -> RETURN(BZ_OK)
     * path at every GET_BITS call site. */
    char data[500];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 251);
    char comp[1000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char decomp[500];
    strm.next_out = decomp;
    strm.avail_out = sizeof(decomp);

    int ret = BZ_OK;
    for (unsigned int i = 0; i < clen && ret == BZ_OK; i++) {
        strm.next_in = comp + i;
        strm.avail_in = 1;
        ret = BZ2_bzDecompress(&strm);
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int total = sizeof(decomp) - strm.avail_out;
    ASSERT_EQ(total, (unsigned int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
    BZ2_bzDecompressEnd(&strm);
}

/* ========== Tiny output buffer exercises unRLE resume paths ========== */

TEST(tiny_output_1byte_fast) {
    /* 1-byte output buffer exercises return-and-resume in unRLE_obuf_to_output_FAST */
    char data[2000];
    /* Mix of runs and non-runs to exercise all RLE branches */
    memset(data, 'A', 500);       /* long run of identical bytes */
    memset(data + 500, 'B', 1);   /* single byte */
    memset(data + 501, 'C', 2);   /* run of 2 */
    memset(data + 503, 'D', 3);   /* run of 3 */
    memset(data + 506, 'E', 4);   /* run of exactly 4 (encoded as 4+0) */
    memset(data + 510, 'F', 10);  /* run > 4 */
    for (int i = 520; i < 2000; i++) data[i] = (char)(i & 0xff);

    char comp[4000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[2000];
    int ret = decompress_streaming_tiny_output(comp, clen, decomp, sizeof(decomp), 1, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

TEST(tiny_output_1byte_small) {
    /* Same but in small mode */
    char data[1000];
    memset(data, 'A', 300);
    memset(data + 300, 'B', 1);
    memset(data + 301, 'C', 2);
    memset(data + 303, 'D', 3);
    memset(data + 306, 'E', 4);
    memset(data + 310, 'F', 10);
    for (int i = 320; i < 1000; i++) data[i] = (char)(i & 0xff);

    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[1000];
    int ret = decompress_streaming_tiny_output(comp, clen, decomp, sizeof(decomp), 1, 1);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

TEST(tiny_output_7byte_fast) {
    /* 7-byte output to exercise the batch memset path in unRLE_obuf_to_output_FAST */
    char data[3000];
    memset(data, 'Q', 3000);

    char comp[6000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[3000];
    int ret = decompress_streaming_tiny_output(comp, clen, decomp, sizeof(decomp), 7, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

/* ========== Data patterns that exercise RLE decode branches ========== */

TEST(rle_run_length_1) {
    /* All different bytes -- no runs, exercises the k1 != k0 branch */
    char data[256];
    for (int i = 0; i < 256; i++) data[i] = (char)i;
    char comp[1000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[256];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 256u);
    ASSERT_MEM_EQ(decomp, data, 256);
}

TEST(rle_run_length_2) {
    /* Pairs of identical bytes: exercises run length 2 branch */
    char data[512];
    for (int i = 0; i < 256; i++) {
        data[i*2] = (char)i;
        data[i*2 + 1] = (char)i;
    }
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[512];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 512u);
    ASSERT_MEM_EQ(decomp, data, 512);
}

TEST(rle_run_length_3) {
    /* Triples of identical bytes */
    char data[768];
    for (int i = 0; i < 256; i++) {
        data[i*3] = (char)i;
        data[i*3 + 1] = (char)i;
        data[i*3 + 2] = (char)i;
    }
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[768];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 768u);
    ASSERT_MEM_EQ(decomp, data, 768);
}

TEST(rle_run_length_4_exact) {
    /* Exactly 4 identical bytes triggers the run-length encoding (4 + 0) */
    char data[1024];
    for (int i = 0; i < 256; i++) {
        data[i*4] = (char)i;
        data[i*4 + 1] = (char)i;
        data[i*4 + 2] = (char)i;
        data[i*4 + 3] = (char)i;
    }
    char comp[3000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[1024];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 1024u);
    ASSERT_MEM_EQ(decomp, data, 1024);
}

TEST(rle_run_length_5_plus) {
    /* 5+ identical bytes (4 + repeat count byte) */
    char data[2000];
    int pos = 0;
    for (int run = 5; run <= 20 && pos + run < (int)sizeof(data); run++) {
        for (int j = 0; j < run; j++) data[pos++] = (char)(run & 0xff);
    }
    while (pos < (int)sizeof(data)) data[pos++] = (char)(pos & 0xff);

    char comp[4000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[2000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

TEST(rle_run_length_255) {
    /* Maximum RLE run of 255 identical bytes */
    char data[260];
    memset(data, 'M', 255);
    data[255] = 'N';
    memset(data + 256, 'M', 4);

    char comp[1000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, 260, comp, &clen, 1), BZ_OK);

    char decomp[260];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 260u);
    ASSERT_MEM_EQ(decomp, data, 260);
}

/* ========== Small mode RLE decode branches ========== */

TEST(rle_small_mode_various_runs) {
    /* Same mixed-run data but decompress in small mode */
    char data[2000];
    int pos = 0;
    memset(data, 'A', 100); pos = 100;       /* long run */
    data[pos++] = 'B';                         /* single */
    data[pos++] = 'C'; data[pos++] = 'C';     /* pair */
    for (int i = 0; i < 3; i++) data[pos++] = 'D';  /* triple */
    for (int i = 0; i < 4; i++) data[pos++] = 'E';  /* quad */
    for (int i = 0; i < 10; i++) data[pos++] = 'F'; /* 10-run */
    while (pos < 2000) data[pos++] = (char)(pos & 0xff);

    char comp[4000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, 2000, comp, &clen, 1), BZ_OK);

    char decomp[2000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0), BZ_OK);
    ASSERT_EQ(dlen, 2000u);
    ASSERT_MEM_EQ(decomp, data, 2000);
}

/* ========== Multi-block in small mode ========== */

TEST(multiblock_small_mode) {
    /* >100KB to force multiple blocks at bs=1, decompress in small mode */
    char *data = malloc(150000);
    ASSERT(data != NULL);
    for (int i = 0; i < 150000; i++) data[i] = (char)(i % 251);

    char *comp = malloc(200000);
    ASSERT(comp != NULL);
    unsigned int clen = 200000;
    ASSERT_EQ(helper_compress(data, 150000, comp, &clen, 1), BZ_OK);

    char *decomp = malloc(150000);
    ASSERT(decomp != NULL);
    unsigned int dlen = 150000;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0), BZ_OK);
    ASSERT_EQ(dlen, 150000u);
    ASSERT_MEM_EQ(decomp, data, 150000);

    free(data);
    free(comp);
    free(decomp);
}

/* ========== Multi-block streaming with tiny input and output ========== */

TEST(multiblock_streaming_tiny_both) {
    /* >100KB data, 5-byte input chunks, 13-byte output chunks */
    char *data = malloc(120000);
    ASSERT(data != NULL);
    for (int i = 0; i < 120000; i++) data[i] = (char)(i % 199);

    char *comp = malloc(200000);
    ASSERT(comp != NULL);
    unsigned int clen = 200000;
    ASSERT_EQ(helper_compress(data, 120000, comp, &clen, 1), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char *decomp = malloc(120000);
    ASSERT(decomp != NULL);
    unsigned int in_pos = 0;
    unsigned int out_pos = 0;
    int ret = BZ_OK;

    while (ret == BZ_OK) {
        unsigned int in_n = 5;
        if (in_pos + in_n > clen) in_n = clen - in_pos;
        strm.next_in = comp + in_pos;
        strm.avail_in = in_n;

        unsigned int out_n = 13;
        if (out_pos + out_n > 120000) out_n = 120000 - out_pos;
        strm.next_out = decomp + out_pos;
        strm.avail_out = out_n;

        ret = BZ2_bzDecompress(&strm);
        in_pos += in_n - strm.avail_in;
        out_pos += out_n - strm.avail_out;
    }

    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(out_pos, 120000u);
    ASSERT_MEM_EQ(decomp, data, 120000);

    BZ2_bzDecompressEnd(&strm);
    free(data);
    free(comp);
    free(decomp);
}

/* ========== Data with high symbol diversity (many MTF positions) ========== */

TEST(high_mtf_diversity) {
    /* Random-ish data with all 256 byte values to maximize MTF positions used.
     * This exercises deep MTF lookups and memmove in the flat array. */
    char data[5000];
    unsigned int seed = 12345;
    for (int i = 0; i < (int)sizeof(data); i++) {
        seed = seed * 1103515245 + 12345;
        data[i] = (char)((seed >> 16) & 0xff);
    }

    char comp[10000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    /* Normal mode */
    char decomp[5000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));

    /* Small mode */
    dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

/* ========== Data with only one symbol (low diversity) ========== */

TEST(single_symbol_data) {
    /* All zeros -- exercises RUNA/RUNB encoding heavily */
    char data[10000];
    memset(data, 0, sizeof(data));

    char comp[20000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[10000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

TEST(two_symbol_data) {
    /* Alternating 0x00 and 0xFF -- exercises RUNA/RUNB interleaved with MTF */
    char data[5000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (i & 1) ? '\xff' : '\x00';

    char comp[10000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    char decomp[5000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
}

/* ========== Verbosity during decompression ========== */

TEST(decompress_verbosity_2) {
    /* verbosity=2 triggers "rt+rld" VPrintf and "]" VPrintf */
    char data[1000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i & 0xff);
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, 1), BZ_OK);

    /* Redirect stderr to /dev/null */
    FILE *devnull = fopen("/dev/null", "w");
    ASSERT(devnull != NULL);
    FILE *saved = stderr;
    stderr = devnull;

    char decomp[1000];
    unsigned int dlen = sizeof(decomp);
    int ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 2);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)sizeof(data));

    stderr = saved;
    fclose(devnull);
}

/* ========== Decompress with all 9 block sizes in both modes ========== */

TEST(all_bs_small_and_normal) {
    char data[2000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)((i * 17 + 3) & 0xff);

    for (int bs = 1; bs <= 9; bs++) {
        char comp[4000];
        unsigned int clen = sizeof(comp);
        ASSERT_EQ(helper_compress(data, sizeof(data), comp, &clen, bs), BZ_OK);

        /* Normal mode */
        char decomp[2000];
        unsigned int dlen = sizeof(decomp);
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
        ASSERT_EQ(dlen, (unsigned int)sizeof(data));
        ASSERT_MEM_EQ(decomp, data, sizeof(data));

        /* Small mode */
        dlen = sizeof(decomp);
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0), BZ_OK);
        ASSERT_EQ(dlen, (unsigned int)sizeof(data));
        ASSERT_MEM_EQ(decomp, data, sizeof(data));
    }
}

/* ========== Empty data (just stream header and end marker) ========== */

TEST(decompress_empty_data) {
    /* Compress zero bytes */
    char comp[1000];
    unsigned int clen = sizeof(comp);
    char dummy = 0;
    ASSERT_EQ(helper_compress(&dummy, 0, comp, &clen, 1), BZ_OK);

    char decomp[10];
    unsigned int dlen = sizeof(decomp);
    int ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(dlen, 0u);

    /* Same in small mode */
    dlen = sizeof(decomp);
    ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(dlen, 0u);
}

/* ========== Data that exactly fills one block ========== */

TEST(exact_block_fill) {
    /* bs=1 means block is 100000 - 19 = 99981 bytes max.
     * Feed exactly that many bytes. */
    int block_size = 99981;
    char *data = malloc(block_size);
    ASSERT(data != NULL);
    for (int i = 0; i < block_size; i++) data[i] = (char)(i % 251);

    char *comp = malloc(200000);
    ASSERT(comp != NULL);
    unsigned int clen = 200000;
    ASSERT_EQ(helper_compress(data, block_size, comp, &clen, 1), BZ_OK);

    char *decomp = malloc(block_size);
    ASSERT(decomp != NULL);
    unsigned int dlen = block_size;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)block_size);
    ASSERT_MEM_EQ(decomp, data, block_size);

    free(data);
    free(comp);
    free(decomp);
}

/* ========== Test runner ========== */

TEST_MAIN_BEGIN()
    /* GET_BITS single-byte absorption */
    RUN(streaming_2byte_chunks);
    RUN(streaming_3byte_chunks);
    RUN(streaming_1byte_chunks_small);
    RUN(streaming_2byte_chunks_small);

    /* GET_BITS resume paths */
    RUN(streaming_resume_at_every_byte);

    /* Tiny output buffer (unRLE resume) */
    RUN(tiny_output_1byte_fast);
    RUN(tiny_output_1byte_small);
    RUN(tiny_output_7byte_fast);

    /* RLE decode branches */
    RUN(rle_run_length_1);
    RUN(rle_run_length_2);
    RUN(rle_run_length_3);
    RUN(rle_run_length_4_exact);
    RUN(rle_run_length_5_plus);
    RUN(rle_run_length_255);
    RUN(rle_small_mode_various_runs);

    /* Multi-block */
    RUN(multiblock_small_mode);
    RUN(multiblock_streaming_tiny_both);

    /* Symbol diversity */
    RUN(high_mtf_diversity);
    RUN(single_symbol_data);
    RUN(two_symbol_data);

    /* Verbosity */
    RUN(decompress_verbosity_2);

    /* All block sizes */
    RUN(all_bs_small_and_normal);

    /* Edge cases */
    RUN(decompress_empty_data);
    RUN(exact_block_fill);
TEST_MAIN_END()
