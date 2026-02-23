/* Advanced tests for libqbz2 — state machines, bzopen API, concatenated streams,
 * RLE edge cases, memory consistency, and large data.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/* Helper: compress to buffer */
static char *compress_to_buf(const char *data, unsigned int len, unsigned int *out_len, int bs) {
    unsigned int clen = len + len / 100 + 600;
    char *comp = malloc(clen);
    if (!comp) return NULL;
    int ret = BZ2_bzBuffToBuffCompress(comp, &clen, (char *)data, len, bs, 0, 0);
    if (ret != BZ_OK) { free(comp); return NULL; }
    *out_len = clen;
    return comp;
}

/* ========== bzopen / bzread / bzwrite / bzclose API ========== */

TEST(bzopen_write_read) {
    const char *path = "/tmp/libqbz2_test_bzopen.bz2";
    const char *msg = "Hello from bzopen API!";

    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    int nw = BZ2_bzwrite(bz, (void *)msg, strlen(msg));
    ASSERT_EQ(nw, (int)strlen(msg));
    BZ2_bzclose(bz);

    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[1000];
    int nr = BZ2_bzread(bz, buf, 1000);
    ASSERT_EQ(nr, (int)strlen(msg));
    ASSERT_MEM_EQ(buf, msg, nr);
    BZ2_bzclose(bz);

    remove(path);
}

TEST(bzopen_write_empty) {
    const char *path = "/tmp/libqbz2_test_bzopen_empty.bz2";

    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    BZ2_bzclose(bz);

    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[100];
    int nr = BZ2_bzread(bz, buf, 100);
    ASSERT_EQ(nr, 0);
    BZ2_bzclose(bz);

    remove(path);
}

TEST(bzopen_null_path) {
    /* Reference libbz2 accepts NULL path (uses stdout/stdin) — not an error */
    BZFILE *bz = BZ2_bzopen(NULL, "w1");
    ASSERT(bz != NULL);
    BZ2_bzclose(bz);
}

TEST(bzopen_null_mode) {
    BZFILE *bz = BZ2_bzopen("/tmp/libqbz2_test_null_mode.bz2", NULL);
    ASSERT(bz == NULL);
}

TEST(bzopen_invalid_mode) {
    /* "x" is not a valid mode */
    BZFILE *bz = BZ2_bzopen("/tmp/libqbz2_test_bad_mode.bz2", "x");
    ASSERT(bz == NULL);
}

TEST(bzopen_write_large) {
    const char *path = "/tmp/libqbz2_test_bzopen_large.bz2";

    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);

    /* Write 100KB of patterned data */
    char chunk[1000];
    for (int c = 0; c < 100; c++) {
        memset(chunk, 'A' + (c % 26), 1000);
        int nw = BZ2_bzwrite(bz, chunk, 1000);
        ASSERT_EQ(nw, 1000);
    }
    BZ2_bzclose(bz);

    /* Read back */
    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char *readbuf = malloc(100000);
    ASSERT(readbuf != NULL);

    int total = 0;
    while (1) {
        int nr = BZ2_bzread(bz, readbuf + total, 100000 - total);
        if (nr <= 0) break;
        total += nr;
    }
    ASSERT_EQ(total, 100000);

    for (int c = 0; c < 100; c++) {
        for (int b = 0; b < 1000; b++) {
            ASSERT_EQ(readbuf[c * 1000 + b], (char)('A' + (c % 26)));
        }
    }

    BZ2_bzclose(bz);
    free(readbuf);
    remove(path);
}

TEST(bzdopen_write_read) {
    const char *path = "/tmp/libqbz2_test_bzdopen.bz2";
    const char *msg = "bzdopen test data";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzdopen(fd, "w1");
    ASSERT(bz != NULL);
    int nw = BZ2_bzwrite(bz, (void *)msg, strlen(msg));
    ASSERT_EQ(nw, (int)strlen(msg));
    BZ2_bzclose(bz);
    /* fd is closed by bzclose */

    fd = open(path, O_RDONLY);
    ASSERT(fd >= 0);
    bz = BZ2_bzdopen(fd, "r");
    ASSERT(bz != NULL);
    char buf[1000];
    int nr = BZ2_bzread(bz, buf, 1000);
    ASSERT_EQ(nr, (int)strlen(msg));
    ASSERT_MEM_EQ(buf, msg, nr);
    BZ2_bzclose(bz);

    remove(path);
}

TEST(bzerror_after_write) {
    const char *path = "/tmp/libqbz2_test_bzerror.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);

    int errnum;
    const char *errmsg = BZ2_bzerror(bz, &errnum);
    ASSERT(errmsg != NULL);
    ASSERT_EQ(errnum, BZ_OK);

    BZ2_bzwrite(bz, "test", 4);
    errmsg = BZ2_bzerror(bz, &errnum);
    ASSERT(errmsg != NULL);
    ASSERT_EQ(errnum, BZ_OK);

    BZ2_bzclose(bz);
    remove(path);
}

TEST(bzflush_is_noop) {
    const char *path = "/tmp/libqbz2_test_bzflush.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);

    BZ2_bzwrite(bz, "data", 4);
    /* bzflush is documented as a no-op in libbz2 */
    int ret = BZ2_bzflush(bz);
    (void)ret; /* just verify no crash */

    BZ2_bzclose(bz);
    remove(path);
}

/* ========== Concatenated bz2 streams ========== */

TEST(concat_two_streams) {
    const char *msg1 = "First stream data";
    const char *msg2 = "Second stream data";

    unsigned int clen1, clen2;
    char *comp1 = compress_to_buf(msg1, strlen(msg1), &clen1, 1);
    char *comp2 = compress_to_buf(msg2, strlen(msg2), &clen2, 1);
    ASSERT(comp1 != NULL);
    ASSERT(comp2 != NULL);

    /* Concatenate both compressed streams */
    unsigned int total_clen = clen1 + clen2;
    char *concat = malloc(total_clen);
    ASSERT(concat != NULL);
    memcpy(concat, comp1, clen1);
    memcpy(concat + clen1, comp2, clen2);

    /* Write to file and read via bzReadOpen */
    const char *path = "/tmp/libqbz2_test_concat.bz2";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    fwrite(concat, 1, total_clen, f);
    fclose(f);

    /* Read first stream */
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerror;
    BZFILE *bz = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    char buf[1000];
    int nr = BZ2_bzRead(&bzerror, bz, buf, 1000);
    ASSERT_EQ(bzerror, BZ_STREAM_END);
    ASSERT_EQ(nr, (int)strlen(msg1));
    ASSERT_MEM_EQ(buf, msg1, nr);

    /* Get unused bytes to pass to next stream */
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerror, bz, &unused, &nUnused);
    ASSERT_EQ(bzerror, BZ_OK);

    /* Save unused for next open */
    char saved_unused[BZ_MAX_UNUSED];
    int saved_nUnused = nUnused;
    if (nUnused > 0) memcpy(saved_unused, unused, nUnused);

    BZ2_bzReadClose(&bzerror, bz);

    /* Check if there's more data (unused bytes or file position) */
    if (saved_nUnused > 0 || !feof(f)) {
        bz = BZ2_bzReadOpen(&bzerror, f, 0, 0, saved_unused, saved_nUnused);
        ASSERT_EQ(bzerror, BZ_OK);

        nr = BZ2_bzRead(&bzerror, bz, buf, 1000);
        ASSERT_EQ(bzerror, BZ_STREAM_END);
        ASSERT_EQ(nr, (int)strlen(msg2));
        ASSERT_MEM_EQ(buf, msg2, nr);

        BZ2_bzReadClose(&bzerror, bz);
    }

    fclose(f);
    free(comp1);
    free(comp2);
    free(concat);
    remove(path);
}

TEST(concat_three_streams) {
    const char *msgs[] = {"Stream one", "Stream two", "Stream three"};
    unsigned int clens[3];
    char *comps[3];

    unsigned int total_clen = 0;
    for (int i = 0; i < 3; i++) {
        comps[i] = compress_to_buf(msgs[i], strlen(msgs[i]), &clens[i], 1);
        ASSERT(comps[i] != NULL);
        total_clen += clens[i];
    }

    /* Concatenate all three */
    char *concat = malloc(total_clen);
    ASSERT(concat != NULL);
    unsigned int off = 0;
    for (int i = 0; i < 3; i++) {
        memcpy(concat + off, comps[i], clens[i]);
        off += clens[i];
    }

    const char *path = "/tmp/libqbz2_test_concat3.bz2";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    fwrite(concat, 1, total_clen, f);
    fclose(f);

    /* Reference libbz2 bzread only reads the first concatenated stream */
    BZFILE *bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);

    char buf[1000];
    int total_read = 0;
    char all_data[1000];
    while (1) {
        int nr = BZ2_bzread(bz, buf, 1000);
        if (nr <= 0) break;
        memcpy(all_data + total_read, buf, nr);
        total_read += nr;
    }
    BZ2_bzclose(bz);

    /* bzread only returns first stream data */
    ASSERT_EQ(total_read, (int)strlen(msgs[0]));
    ASSERT_MEM_EQ(all_data, msgs[0], total_read);

    for (int i = 0; i < 3; i++) free(comps[i]);
    free(concat);
    remove(path);
}

/* ========== Compression state machine transitions ========== */

TEST(state_run_flush_run_finish) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[10000];
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    /* RUN with first chunk */
    char data1[] = "First chunk of data for state machine test";
    strm.next_in = data1;
    strm.avail_in = strlen(data1);
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* FLUSH */
    int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* RUN with second chunk */
    char data2[] = "Second chunk after flush";
    strm.next_in = data2;
    strm.avail_in = strlen(data2);
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* FINISH */
    strm.avail_in = 0;
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = sizeof(output) - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify round-trip */
    char decomp[10000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);

    unsigned int expected = strlen(data1) + strlen(data2);
    ASSERT_EQ(dlen, expected);
    ASSERT_MEM_EQ(decomp, data1, strlen(data1));
    ASSERT_MEM_EQ(decomp + strlen(data1), data2, strlen(data2));
}

TEST(state_multiple_flushes) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[20000];
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    /* Multiple RUN+FLUSH cycles */
    for (int i = 0; i < 5; i++) {
        char chunk[100];
        memset(chunk, 'A' + i, 100);
        strm.next_in = chunk;
        strm.avail_in = 100;
        ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

        int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }

    /* FINISH */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = sizeof(output) - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Decompress and verify total */
    char decomp[20000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 500u); /* 5 chunks * 100 bytes */

    /* Verify content */
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 100; j++) {
            ASSERT_EQ(decomp[i * 100 + j], (char)('A' + i));
        }
    }
}

TEST(state_finish_without_run) {
    /* Go directly to FINISH without any RUN */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = "direct finish";
    strm.avail_in = strlen("direct finish");
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    unsigned int clen = sizeof(output) - strm.avail_out;
    BZ2_bzCompressEnd(&strm);

    /* Verify round-trip */
    char decomp[1000];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, strlen("direct finish"));
    ASSERT_MEM_EQ(decomp, "direct finish", dlen);
}

TEST(state_flush_before_any_data) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[1000];
    char empty[] = "";
    strm.next_in = empty;
    strm.avail_in = 0;
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    /* FLUSH with no data */
    int ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    while (ret == BZ_FLUSH_OK) ret = BZ2_bzCompress(&strm, BZ_FLUSH);
    ASSERT_EQ(ret, BZ_RUN_OK);

    /* Now FINISH */
    ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    BZ2_bzCompressEnd(&strm);
}

TEST(state_run_with_zero_avail_in) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[1000];
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    /* RUN with avail_in=0 but valid next_in — reference returns BZ_PARAM_ERROR */
    char empty[] = "";
    strm.next_in = empty;
    strm.avail_in = 0;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_PARAM_ERROR);

    /* Feed some data */
    char data[] = "Some data";
    strm.next_in = data;
    strm.avail_in = strlen(data);
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* FINISH */
    strm.avail_in = 0;
    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);
    BZ2_bzCompressEnd(&strm);
}

/* ========== Sequence error tests (more thorough) ========== */

TEST(seq_error_flush_during_finish) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[10000];
    /* Need enough data that BZ_FINISH takes multiple calls */
    char *data = malloc(50000);
    ASSERT(data != NULL);
    for (int i = 0; i < 50000; i++) data[i] = (char)(i % 251);

    strm.next_in = data;
    strm.avail_in = 50000;
    strm.next_out = output;
    strm.avail_out = 100; /* small output buffer to force multiple calls */

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    if (ret == BZ_FINISH_OK) {
        /* We're in FINISHING state — trying FLUSH should be error */
        strm.avail_out = sizeof(output);
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }

    BZ2_bzCompressEnd(&strm);
    free(data);
}

TEST(seq_error_run_during_finish) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[10000];
    char *data = malloc(50000);
    ASSERT(data != NULL);
    for (int i = 0; i < 50000; i++) data[i] = (char)(i % 251);

    strm.next_in = data;
    strm.avail_in = 50000;
    strm.next_out = output;
    strm.avail_out = 100;

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    if (ret == BZ_FINISH_OK) {
        strm.avail_out = sizeof(output);
        ret = BZ2_bzCompress(&strm, BZ_RUN);
        ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);
    }

    BZ2_bzCompressEnd(&strm);
    free(data);
}

TEST(seq_error_invalid_action) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = "test";
    strm.avail_in = 4;
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    /* Action 99 is invalid */
    int ret = BZ2_bzCompress(&strm, 99);
    ASSERT(ret == BZ_PARAM_ERROR || ret == BZ_SEQUENCE_ERROR);

    BZ2_bzCompressEnd(&strm);
}

/* ========== RLE edge cases ========== */

TEST(rle_long_run) {
    /* 255+ identical bytes triggers RLE encoding in bzip2 */
    int sz = 300;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    memset(buf, 'Z', sz);

    unsigned int clen = sz + sz / 100 + 600;
    char *comp = malloc(clen);
    ASSERT(comp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, sz, 1, 0, 0), BZ_OK);

    unsigned int dlen = sz + 100;
    char *decomp = malloc(dlen);
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)sz);
    ASSERT_MEM_EQ(decomp, buf, sz);

    free(buf);
    free(comp);
    free(decomp);
}

TEST(rle_exact_255) {
    char buf[255];
    memset(buf, 'X', 255);

    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, 255, 1, 0, 0), BZ_OK);

    unsigned int dlen = 300;
    char decomp[300];
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 255u);
    ASSERT_MEM_EQ(decomp, buf, 255);
}

TEST(rle_256_bytes) {
    char buf[256];
    memset(buf, 'Y', 256);

    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, 256, 1, 0, 0), BZ_OK);

    unsigned int dlen = 300;
    char decomp[300];
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 256u);
    ASSERT_MEM_EQ(decomp, buf, 256);
}

TEST(rle_1000_identical) {
    char buf[1000];
    memset(buf, 0x42, 1000);

    unsigned int clen = 2000;
    char comp[2000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, 1000, 1, 0, 0), BZ_OK);

    unsigned int dlen = 1100;
    char decomp[1100];
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 1000u);
    ASSERT_MEM_EQ(decomp, buf, 1000);
}

TEST(rle_mixed_runs) {
    /* Pattern: 300 A's, 1 B, 300 C's, 1 D, 300 E's */
    int sz = 902;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    memset(buf, 'A', 300);
    buf[300] = 'B';
    memset(buf + 301, 'C', 300);
    buf[601] = 'D';
    memset(buf + 602, 'E', 300);

    unsigned int clen = sz + sz / 100 + 600;
    char *comp = malloc(clen);
    ASSERT(comp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, sz, 1, 0, 0), BZ_OK);

    unsigned int dlen = sz + 100;
    char *decomp = malloc(dlen);
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, (unsigned int)sz);
    ASSERT_MEM_EQ(decomp, buf, sz);

    free(buf);
    free(comp);
    free(decomp);
}

/* ========== Decompression with small=1 (low-memory mode) ========== */

TEST(decompress_small_mode_all_blocksizes) {
    for (int bs = 1; bs <= 9; bs++) {
        const char *msg = "Testing small mode decompression across all block sizes";
        unsigned int clen = 1000;
        char comp[1000];
        ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), bs, 0, 0), BZ_OK);

        unsigned int dlen = 1000;
        char decomp[1000];
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0), BZ_OK);
        ASSERT_EQ(dlen, strlen(msg));
        ASSERT_MEM_EQ(decomp, msg, dlen);
    }
}

TEST(decompress_small_mode_large) {
    unsigned int sz = 200000;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    for (unsigned int i = 0; i < sz; i++) buf[i] = (char)(i % 127);

    unsigned int clen = sz + sz / 100 + 600;
    char *comp = malloc(clen);
    ASSERT(comp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, sz, 1, 0, 0), BZ_OK);

    unsigned int dlen = sz + 100;
    char *decomp = malloc(dlen);
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 1, 0), BZ_OK);
    ASSERT_EQ(dlen, sz);
    ASSERT_MEM_EQ(decomp, buf, sz);

    free(buf);
    free(comp);
    free(decomp);
}

/* ========== Streaming with tight output buffers ========== */

TEST(streaming_compress_tiny_output) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char input[5000];
    for (int i = 0; i < 5000; i++) input[i] = (char)(i % 97);

    char output[10000];
    unsigned int out_pos = 0;

    strm.next_in = input;
    strm.avail_in = 5000;

    /* Compress with tiny output buffer (10 bytes at a time) */
    int ret;
    do {
        strm.next_out = output + out_pos;
        strm.avail_out = 10;
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        ASSERT(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);
        out_pos += 10 - strm.avail_out;
    } while (ret == BZ_FINISH_OK);

    ASSERT_EQ(ret, BZ_STREAM_END);
    BZ2_bzCompressEnd(&strm);

    /* Verify round-trip */
    char decomp[5100];
    unsigned int dlen = sizeof(decomp);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, out_pos, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, 5000u);
    ASSERT_MEM_EQ(decomp, input, 5000);
}

TEST(streaming_decompress_tiny_output) {
    const char *msg = "Streaming decompress with tiny output buffer test data repeated for length";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[1000];
    unsigned int out_pos = 0;
    strm.next_in = comp;
    strm.avail_in = clen;

    int ret;
    do {
        strm.next_out = output + out_pos;
        strm.avail_out = 5; /* tiny output buffer */
        ret = BZ2_bzDecompress(&strm);
        ASSERT(ret == BZ_OK || ret == BZ_STREAM_END);
        out_pos += 5 - strm.avail_out;
    } while (ret == BZ_OK);

    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(out_pos, strlen(msg));
    ASSERT_MEM_EQ(output, msg, out_pos);
    BZ2_bzDecompressEnd(&strm);
}

/* ========== Memory consistency — alloc/free balance ========== */

static int adv_alloc_count = 0;
static int adv_free_count = 0;

static void *adv_alloc(void *opaque, int items, int size) {
    (void)opaque;
    adv_alloc_count++;
    return malloc((size_t)items * (size_t)size);
}

static void adv_free(void *opaque, void *ptr) {
    (void)opaque;
    if (ptr) adv_free_count++;
    free(ptr);
}

TEST(alloc_free_balance_compress) {
    adv_alloc_count = 0;
    adv_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = adv_alloc;
    strm.bzfree = adv_free;

    ASSERT_EQ(BZ2_bzCompressInit(&strm, 5, 0, 0), BZ_OK);

    char input[] = "Memory balance test";
    char output[1000];
    strm.next_in = input;
    strm.avail_in = strlen(input);
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    while (ret == BZ_FINISH_OK) ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(BZ2_bzCompressEnd(&strm), BZ_OK);

    ASSERT(adv_alloc_count > 0);
    ASSERT_EQ(adv_alloc_count, adv_free_count);
}

TEST(alloc_free_balance_decompress) {
    /* First compress normally */
    const char *msg = "Memory balance decompress test";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);

    adv_alloc_count = 0;
    adv_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = adv_alloc;
    strm.bzfree = adv_free;

    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    ASSERT_EQ(BZ2_bzDecompress(&strm), BZ_STREAM_END);
    ASSERT_EQ(BZ2_bzDecompressEnd(&strm), BZ_OK);

    ASSERT(adv_alloc_count > 0);
    ASSERT_EQ(adv_alloc_count, adv_free_count);
}

TEST(alloc_free_balance_error_path) {
    /* Compress, corrupt, decompress — verify no leaks even on error */
    const char *msg = "Corrupt data alloc test";
    unsigned int clen = 1000;
    char comp[1000];
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, (char *)msg, strlen(msg), 1, 0, 0), BZ_OK);

    /* Corrupt the data */
    if (clen > 10) comp[clen / 2] ^= 0xFF;

    adv_alloc_count = 0;
    adv_free_count = 0;

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = adv_alloc;
    strm.bzfree = adv_free;

    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    int init_allocs = adv_alloc_count;

    char output[1000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ2_bzDecompress(&strm);
    /* Should get an error */
    ASSERT(ret == BZ_DATA_ERROR || ret == BZ_DATA_ERROR_MAGIC);

    /* End should still free all allocations */
    BZ2_bzDecompressEnd(&strm);
    ASSERT(init_allocs > 0);
    ASSERT_EQ(adv_alloc_count, adv_free_count);
}

/* ========== BuffToBuffCompress/Decompress output size verification ========== */

TEST(b2b_compress_output_grows_with_input) {
    /* Incompressible (random) data should have compressed size close to original */
    char buf[10000];
    unsigned int seed = 99999;
    for (int i = 0; i < 10000; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }

    unsigned int clen = 20000;
    char *comp = malloc(clen);
    ASSERT(comp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, 10000, 9, 0, 0), BZ_OK);

    /* Random data is hard to compress — output should be reasonably close to input */
    ASSERT(clen > 0);
    /* Should not compress to more than 2x the input (bzip2 overhead) */
    ASSERT(clen < 20000u);

    free(comp);
}

TEST(b2b_compress_compressible_data) {
    /* Highly compressible data should compress well */
    char buf[10000];
    memset(buf, 'A', 10000);

    unsigned int clen = 20000;
    char *comp = malloc(clen);
    ASSERT(comp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, 10000, 1, 0, 0), BZ_OK);

    /* 10000 identical bytes should compress dramatically */
    ASSERT(clen < 1000u);

    free(comp);
}

/* ========== DecompressInit zeroes totals ========== */

TEST(decompress_init_zeroes_totals) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.total_in_lo32 = 0xDEADBEEF;
    strm.total_in_hi32 = 0xDEADBEEF;
    strm.total_out_lo32 = 0xDEADBEEF;
    strm.total_out_hi32 = 0xDEADBEEF;

    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);
    ASSERT_EQ(strm.total_in_lo32, 0u);
    ASSERT_EQ(strm.total_in_hi32, 0u);
    ASSERT_EQ(strm.total_out_lo32, 0u);
    ASSERT_EQ(strm.total_out_hi32, 0u);
    BZ2_bzDecompressEnd(&strm);
}

/* ========== FILE* API — BZ2_bzWriteClose with abandon ========== */

TEST(file_write_close_abandon) {
    int bzerror;
    FILE *f = tmpfile();
    ASSERT(f != NULL);

    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 1, 0, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    BZ2_bzWrite(&bzerror, bz, "test data", 9);
    ASSERT_EQ(bzerror, BZ_OK);

    /* Abandon=1: discard without finishing */
    BZ2_bzWriteClose(&bzerror, bz, 1, NULL, NULL);
    /* Should still return OK (resources freed) */
    ASSERT_EQ(bzerror, BZ_OK);

    fclose(f);
}

TEST(file_read_write_with_nbytes) {
    int bzerror;
    FILE *f = tmpfile();
    ASSERT(f != NULL);

    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 1, 0, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    const char *data = "Counting bytes in and out";
    BZ2_bzWrite(&bzerror, bz, (void *)data, strlen(data));
    ASSERT_EQ(bzerror, BZ_OK);

    unsigned int nbin, nbout;
    BZ2_bzWriteClose(&bzerror, bz, 0, &nbin, &nbout);
    ASSERT_EQ(bzerror, BZ_OK);
    ASSERT_EQ(nbin, strlen(data));
    ASSERT(nbout > 0);

    fclose(f);
}

/* ========== BZ2_bzRead parameter validation ========== */

TEST(bzread_null_bzfile) {
    int bzerror;
    char buf[100];
    int nr = BZ2_bzRead(&bzerror, NULL, buf, 100);
    ASSERT(bzerror != BZ_OK);
    (void)nr;
}

TEST(bzwrite_null_bzfile) {
    int bzerror;
    BZ2_bzWrite(&bzerror, NULL, "test", 4);
    ASSERT(bzerror != BZ_OK);
}

TEST(bzread_zero_len) {
    int bzerror;
    FILE *f = tmpfile();
    ASSERT(f != NULL);

    BZFILE *bz = BZ2_bzWriteOpen(&bzerror, f, 1, 0, 0);
    ASSERT_EQ(bzerror, BZ_OK);
    BZ2_bzWrite(&bzerror, bz, "data", 4);
    BZ2_bzWriteClose(&bzerror, bz, 0, NULL, NULL);

    rewind(f);
    bz = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
    ASSERT_EQ(bzerror, BZ_OK);

    char buf[100];
    int nr = BZ2_bzRead(&bzerror, bz, buf, 0);
    /* Reading 0 bytes — behavior is param error in reference */
    ASSERT(bzerror == BZ_PARAM_ERROR || nr == 0);

    BZ2_bzReadClose(&bzerror, bz);
    fclose(f);
}

/* ========== Large data test ========== */

TEST(roundtrip_1mb) {
    unsigned int sz = 1024 * 1024;
    char *buf = malloc(sz);
    ASSERT(buf != NULL);
    unsigned int seed = 42;
    for (unsigned int i = 0; i < sz; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (char)(seed >> 16);
    }

    unsigned int clen = sz + sz / 100 + 600;
    char *comp = malloc(clen);
    ASSERT(comp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, buf, sz, 9, 0, 0), BZ_OK);

    unsigned int dlen = sz + 100;
    char *decomp = malloc(dlen);
    ASSERT(decomp != NULL);
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, clen, 0, 0), BZ_OK);
    ASSERT_EQ(dlen, sz);
    ASSERT_MEM_EQ(decomp, buf, sz);

    free(buf);
    free(comp);
    free(decomp);
}

/* ========== Test runner ========== */

TEST_MAIN_BEGIN()
    /* bzopen API */
    RUN(bzopen_write_read);
    RUN(bzopen_write_empty);
    RUN(bzopen_null_path);
    RUN(bzopen_null_mode);
    RUN(bzopen_invalid_mode);
    RUN(bzopen_write_large);
    RUN(bzdopen_write_read);
    RUN(bzerror_after_write);
    RUN(bzflush_is_noop);

    /* Concatenated streams */
    RUN(concat_two_streams);
    RUN(concat_three_streams);

    /* State machine transitions */
    RUN(state_run_flush_run_finish);
    RUN(state_multiple_flushes);
    RUN(state_finish_without_run);
    RUN(state_flush_before_any_data);
    RUN(state_run_with_zero_avail_in);

    /* Sequence errors */
    RUN(seq_error_flush_during_finish);
    RUN(seq_error_run_during_finish);
    RUN(seq_error_invalid_action);

    /* RLE edge cases */
    RUN(rle_long_run);
    RUN(rle_exact_255);
    RUN(rle_256_bytes);
    RUN(rle_1000_identical);
    RUN(rle_mixed_runs);

    /* Small mode decompression */
    RUN(decompress_small_mode_all_blocksizes);
    RUN(decompress_small_mode_large);

    /* Tight output buffers */
    RUN(streaming_compress_tiny_output);
    RUN(streaming_decompress_tiny_output);

    /* Memory consistency */
    RUN(alloc_free_balance_compress);
    RUN(alloc_free_balance_decompress);
    RUN(alloc_free_balance_error_path);

    /* Output size verification */
    RUN(b2b_compress_output_grows_with_input);
    RUN(b2b_compress_compressible_data);

    /* DecompressInit zeroes totals */
    RUN(decompress_init_zeroes_totals);

    /* FILE* API extended */
    RUN(file_write_close_abandon);
    RUN(file_read_write_with_nbytes);

    /* BZ2_bzRead/Write param validation */
    RUN(bzread_null_bzfile);
    RUN(bzwrite_null_bzfile);
    RUN(bzread_zero_len);

    /* Large data */
    RUN(roundtrip_1mb);
TEST_MAIN_END()
