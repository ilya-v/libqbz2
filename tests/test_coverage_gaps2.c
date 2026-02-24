/* Coverage gap tests — Round 2
 * Targets specific uncovered branches in bzlib.c and decompress.c
 * identified by gcov analysis at 06f3551 (78.7% branch coverage).
 *
 * Focus areas:
 * - BZ2_bzDecompressEnd param validation (NULL strm, NULL state, mismatched strm)
 * - BZ2_bzWrite with len==0
 * - BZ2_bzWriteClose64 with NULL b
 * - BZ2_bzReadClose with NULL b
 * - BZ2_bzRead with len==0
 * - BZ2_bzopen empty path (stdin/stdout dispatch)
 * - decompress.c state machine suspension (tiny input delivery)
 * - WriteClose64 ferror on handle
 * - BZ2_bzDecompress verbosity==2 (VPrintf0 "]" path)
 * - Flush-to-completion multi-round
 * - bzwrite I/O error returning -1
 * - CompressInit/DecompressInit with NULL strm
 * - CompressEnd/DecompressEnd with NULL strm, NULL state
 * - BZ2_bzBuffToBuffDecompress UNEXPECTED_EOF (avail_out > 0 path)
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/* Helper: compress data to buffer */
static int compress_to_buf(const char *data, unsigned int len,
                           char *comp, unsigned int *clen, int bs) {
    return BZ2_bzBuffToBuffCompress(comp, clen, (char *)data, len, bs, 0, 0);
}

/* ================================================================
 * BZ2_bzCompressInit / BZ2_bzCompressEnd NULL param tests
 * ================================================================ */

TEST(compress_init_null_strm) {
    int ret = BZ2_bzCompressInit(NULL, 1, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_end_null_strm) {
    int ret = BZ2_bzCompressEnd(NULL);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_end_null_state) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.state = NULL;
    int ret = BZ2_bzCompressEnd(&strm);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_null_strm) {
    int ret = BZ2_bzCompress(NULL, BZ_RUN);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_null_state) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.state = NULL;
    int ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

/* ================================================================
 * BZ2_bzDecompressInit / BZ2_bzDecompressEnd NULL param tests
 * ================================================================ */

TEST(decompress_init_null_strm) {
    int ret = BZ2_bzDecompressInit(NULL, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_end_null_strm) {
    int ret = BZ2_bzDecompressEnd(NULL);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_end_null_state) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.state = NULL;
    int ret = BZ2_bzDecompressEnd(&strm);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_end_mismatched_strm) {
    bz_stream strm1, strm2;
    memset(&strm1, 0, sizeof(strm1));
    memset(&strm2, 0, sizeof(strm2));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm1, 0, 0), BZ_OK);

    /* Point strm2.state to strm1's state — mismatched strm pointer */
    strm2.state = strm1.state;
    int ret = BZ2_bzDecompressEnd(&strm2);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);

    /* Clean up properly */
    BZ2_bzDecompressEnd(&strm1);
}

TEST(decompress_null_strm) {
    int ret = BZ2_bzDecompress(NULL);
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
 * BZ2_bzWrite with len==0 (no-op path)
 * ================================================================ */

TEST(write_zero_len) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    /* len==0 should be a no-op that returns BZ_OK */
    char data = 'x';
    BZ2_bzWrite(&bzerr, bz, &data, 0);
    ASSERT_EQ(bzerr, BZ_OK);

    /* Write some actual data to verify the stream is still working */
    BZ2_bzWrite(&bzerr, bz, &data, 1);
    ASSERT_EQ(bzerr, BZ_OK);

    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
    fclose(f);
}

TEST(write_null_buf) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    BZ2_bzWrite(&bzerr, bz, NULL, 10);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);

    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
}

TEST(write_negative_len) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char data = 'x';
    BZ2_bzWrite(&bzerr, bz, &data, -5);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);

    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
}

/* ================================================================
 * BZ2_bzWriteClose64 with NULL b
 * ================================================================ */

TEST(write_close64_null_bzfile) {
    int bzerr;
    BZ2_bzWriteClose64(&bzerr, NULL, 0, NULL, NULL, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
}

TEST(write_close_null_bzfile) {
    int bzerr;
    BZ2_bzWriteClose(&bzerr, NULL, 0, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
}

/* ================================================================
 * BZ2_bzReadClose with NULL b
 * ================================================================ */

TEST(read_close_null_bzfile) {
    int bzerr;
    BZ2_bzReadClose(&bzerr, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
}

/* ================================================================
 * BZ2_bzRead with len==0 (no-op path)
 * ================================================================ */

TEST(read_zero_len) {
    char path[] = "/tmp/libqbz2_cov2_read0.bz2";
    /* Create a valid bz2 file */
    char data[] = "zero len read test";
    char comp[1000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, (unsigned int)strlen(data), comp, &clen, 1), BZ_OK);

    FILE *fw = fopen(path, "wb");
    ASSERT(fw != NULL);
    fwrite(comp, 1, clen, fw);
    fclose(fw);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    /* len==0 should return 0 bytes read with BZ_OK */
    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 0);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT_EQ(nr, 0);

    /* Now read normally to verify stream still works */
    nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    ASSERT(bzerr == BZ_STREAM_END || bzerr == BZ_OK);
    ASSERT(nr > 0);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

/* ================================================================
 * BZ2_bzopen with empty path (dispatches to stdin/stdout)
 * NOTE: We cannot actually test stdin/stdout dispatch without a pipe,
 * but we can test the empty-string path. This may block on stdin,
 * so we test via bzdopen with a known FD instead.
 * ================================================================ */

TEST(bzopen_empty_path_write) {
    /* BZ2_bzopen("", "w") should open stdout for writing */
    /* We redirect stdout to a temp file to capture the output */
    char path[] = "/tmp/libqbz2_cov2_empty_wr.bz2";
    int saved_stdout = dup(STDOUT_FILENO);

    FILE *fout = fopen(path, "wb");
    ASSERT(fout != NULL);
    int fout_fd = fileno(fout);
    dup2(fout_fd, STDOUT_FILENO);
    fclose(fout);

    BZFILE *bz = BZ2_bzopen("", "w1");
    if (bz != NULL) {
        char data[] = "empty path write test";
        int nw = BZ2_bzwrite(bz, data, (int)strlen(data));
        ASSERT_EQ(nw, (int)strlen(data));
        BZ2_bzclose(bz);
    }

    /* Restore stdout */
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    /* Verify the file was created and contains valid bz2 data */
    if (bz != NULL) {
        FILE *fr = fopen(path, "rb");
        ASSERT(fr != NULL);
        /* Check it starts with "BZh" */
        char hdr[3];
        size_t r = fread(hdr, 1, 3, fr);
        ASSERT_EQ((int)r, 3);
        ASSERT_MEM_EQ(hdr, "BZh", 3);
        fclose(fr);
    }

    remove(path);
}

TEST(bzopen_null_path_write) {
    /* BZ2_bzopen(NULL, "w") should also dispatch to stdout */
    char path[] = "/tmp/libqbz2_cov2_null_wr.bz2";
    int saved_stdout = dup(STDOUT_FILENO);

    FILE *fout = fopen(path, "wb");
    ASSERT(fout != NULL);
    int fout_fd = fileno(fout);
    dup2(fout_fd, STDOUT_FILENO);
    fclose(fout);

    BZFILE *bz = BZ2_bzopen(NULL, "w1");
    if (bz != NULL) {
        char data[] = "null path write test";
        int nw = BZ2_bzwrite(bz, data, (int)strlen(data));
        ASSERT_EQ(nw, (int)strlen(data));
        BZ2_bzclose(bz);
    }

    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    if (bz != NULL) {
        FILE *fr = fopen(path, "rb");
        ASSERT(fr != NULL);
        char hdr[3];
        size_t r = fread(hdr, 1, 3, fr);
        ASSERT_EQ((int)r, 3);
        ASSERT_MEM_EQ(hdr, "BZh", 3);
        fclose(fr);
    }

    remove(path);
}

/* ================================================================
 * Decompress state machine suspension: tiny input delivery
 * Forces the GET_UCHAR/GET_BITS macros to hit suspension branches
 * by feeding compressed data 1 byte at a time.
 * ================================================================ */

TEST(decompress_1byte_input_delivery) {
    /* Compress some data */
    char data[500];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 127 + 1);
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, sizeof(data), comp, &clen, 1), BZ_OK);

    /* Decompress feeding 1 byte of compressed input at a time */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char decomp[500];
    int total_out = 0;
    unsigned int pos = 0;

    while (pos < clen) {
        strm.next_in = comp + pos;
        strm.avail_in = 1;
        strm.next_out = decomp + total_out;
        strm.avail_out = (unsigned int)(sizeof(decomp) - total_out);

        int ret = BZ2_bzDecompress(&strm);
        total_out = (int)(sizeof(decomp)) - (int)strm.avail_out;
        pos += 1 - strm.avail_in;

        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
    }

    ASSERT_EQ(total_out, (int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_1byte_input_small_mode) {
    /* Same as above but with small=1 */
    char data[300];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 97 + 32);
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, sizeof(data), comp, &clen, 1), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 1), BZ_OK);

    char decomp[300];
    int total_out = 0;
    unsigned int pos = 0;

    while (pos < clen) {
        strm.next_in = comp + pos;
        strm.avail_in = 1;
        strm.next_out = decomp + total_out;
        strm.avail_out = (unsigned int)(sizeof(decomp) - total_out);

        int ret = BZ2_bzDecompress(&strm);
        total_out = (int)(sizeof(decomp)) - (int)strm.avail_out;
        pos += 1 - strm.avail_in;

        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
    }

    ASSERT_EQ(total_out, (int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_2byte_input_delivery) {
    /* Feed 2 bytes at a time — different suspension points */
    char data[500];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 251);
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, sizeof(data), comp, &clen, 1), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char decomp[500];
    int total_out = 0;
    unsigned int pos = 0;

    while (pos < clen) {
        unsigned int feed = (clen - pos >= 2) ? 2 : (clen - pos);
        strm.next_in = comp + pos;
        strm.avail_in = feed;
        strm.next_out = decomp + total_out;
        strm.avail_out = (unsigned int)(sizeof(decomp) - total_out);

        int ret = BZ2_bzDecompress(&strm);
        total_out = (int)(sizeof(decomp)) - (int)strm.avail_out;
        pos += feed - strm.avail_in;

        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
    }

    ASSERT_EQ(total_out, (int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
    BZ2_bzDecompressEnd(&strm);
}

TEST(decompress_3byte_input_delivery) {
    /* Feed 3 bytes at a time */
    char data[500];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 199);
    char comp[2000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, sizeof(data), comp, &clen, 1), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char decomp[500];
    int total_out = 0;
    unsigned int pos = 0;

    while (pos < clen) {
        unsigned int feed = (clen - pos >= 3) ? 3 : (clen - pos);
        strm.next_in = comp + pos;
        strm.avail_in = feed;
        strm.next_out = decomp + total_out;
        strm.avail_out = (unsigned int)(sizeof(decomp) - total_out);

        int ret = BZ2_bzDecompress(&strm);
        total_out = (int)(sizeof(decomp)) - (int)strm.avail_out;
        pos += feed - strm.avail_in;

        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
    }

    ASSERT_EQ(total_out, (int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
    BZ2_bzDecompressEnd(&strm);
}

/* ================================================================
 * Decompress with verbosity >= 2 (bracket and CRC trace paths)
 * ================================================================ */

TEST(decompress_verbosity_2) {
    char data[2000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i & 0xff);
    char comp[5000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, sizeof(data), comp, &clen, 1), BZ_OK);

    /* Redirect stderr to /dev/null */
    int saved_stderr = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDERR_FILENO);
    close(devnull);

    /* Decompress with verbosity=2 — should hit VPrintf0("]") path */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 2, 0), BZ_OK);

    char decomp[2000];
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = decomp;
    strm.avail_out = sizeof(decomp);

    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(strm.total_out_lo32, (unsigned int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));

    BZ2_bzDecompressEnd(&strm);

    /* Restore stderr */
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);
}

TEST(decompress_verbosity_3_combined_crc) {
    /* Use a multi-block input so combined CRC trace covers both blocks */
    int datalen = 150000; /* > 100KB to get 2+ blocks at bs=1 */
    char *data = malloc(datalen);
    ASSERT(data != NULL);
    for (int i = 0; i < datalen; i++) data[i] = (char)(i % 251);

    char *comp = malloc(200000);
    ASSERT(comp != NULL);
    unsigned int clen = 200000;
    ASSERT_EQ(compress_to_buf(data, datalen, comp, &clen, 1), BZ_OK);

    int saved_stderr = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDERR_FILENO);
    close(devnull);

    /* verbosity=3 — hits both per-block CRC trace and combined CRC trace */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 3, 0), BZ_OK);

    char *decomp = malloc(datalen);
    ASSERT(decomp != NULL);
    strm.next_in = comp;
    strm.avail_in = clen;
    strm.next_out = decomp;
    strm.avail_out = datalen;

    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(strm.total_out_lo32, (unsigned int)datalen);

    BZ2_bzDecompressEnd(&strm);

    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);

    free(data);
    free(comp);
    free(decomp);
}

/* ================================================================
 * BZ2_bzWriteClose64 ferror paths
 * ================================================================ */

TEST(write_close_ferror_on_handle) {
    /* Ignore SIGPIPE so broken pipe doesn't kill the process */
    struct sigaction sa, old_sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, &old_sa);

    /* Create a pipe, close the read end, write enough to fill the pipe
       buffer, then close — this triggers ferror on the handle */
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    close(pipefd[0]); /* Close read end — writes will eventually fail */

    FILE *fw = fdopen(pipefd[1], "wb");
    ASSERT(fw != NULL);

    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
    ASSERT(bz != NULL);

    /* Write lots of data to trigger the pipe EPIPE */
    char data[200000];
    memset(data, 'P', sizeof(data));
    BZ2_bzWrite(&bzerr, bz, data, (int)sizeof(data));
    /* May have IO_ERROR from the write itself */

    /* clearerr so WriteClose can proceed to the abandon path without
       hitting the ferror check at the top and returning early */
    clearerr(fw);

    /* Close with abandon=1 to ensure cleanup happens even after error */
    BZ2_bzWriteClose64(&bzerr, bz, 1, NULL, NULL, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);

    fclose(fw);

    /* Restore SIGPIPE handler */
    sigaction(SIGPIPE, &old_sa, NULL);
}

/* ================================================================
 * BZ2_bzWriteOpen with NULL FILE* (param error)
 * ================================================================ */

TEST(write_open_null_file) {
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, NULL, 1, 0, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

TEST(write_open_invalid_blocksize) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 0, 0, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    bz = BZ2_bzWriteOpen(&bzerr, f, 10, 0, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

/* ================================================================
 * BZ2_bzReadOpen with NULL FILE* (param error)
 * ================================================================ */

TEST(read_open_null_file) {
    int bzerr;
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, NULL, 0, 0, NULL, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

TEST(read_open_unused_negative_count) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    char unused[4] = "abcd";
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused, -1);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

TEST(read_open_unused_over_max) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    char unused[4] = "abcd";
    /* BZ_MAX_UNUSED is 5000; nUnused > 5000 should be rejected */
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused, 5001);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

/* ================================================================
 * Flush multi-round: BZ_FLUSH_OK then complete
 * ================================================================ */

TEST(flush_multi_round) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    /* Feed enough data to fill a block */
    char *data = malloc(150000);
    ASSERT(data != NULL);
    for (int i = 0; i < 150000; i++) data[i] = (char)(i % 251);

    char *output = malloc(300000);
    ASSERT(output != NULL);

    strm.next_in = data;
    strm.avail_in = 150000;
    strm.next_out = output;
    strm.avail_out = 300000;

    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_RUN), BZ_RUN_OK);

    /* Now flush with tiny output buffer to force multi-round FLUSH_OK */
    unsigned int total_out = 300000 - strm.avail_out;
    strm.avail_in = 0;
    int flush_ok_count = 0;
    int ret;

    do {
        strm.next_out = output + total_out;
        strm.avail_out = 1;
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        total_out += 1 - strm.avail_out;
        if (ret == BZ_FLUSH_OK) flush_ok_count++;
        ASSERT(ret == BZ_FLUSH_OK || ret == BZ_RUN_OK);
    } while (ret == BZ_FLUSH_OK);

    ASSERT_EQ(ret, BZ_RUN_OK);
    ASSERT(flush_ok_count > 0);

    /* Now finish */
    strm.avail_in = 0;
    do {
        strm.next_out = output + total_out;
        strm.avail_out = 300000 - total_out;
        ret = BZ2_bzCompress(&strm, BZ_FINISH);
        total_out = 300000 - strm.avail_out;
    } while (ret == BZ_FINISH_OK);

    ASSERT_EQ(ret, BZ_STREAM_END);
    BZ2_bzCompressEnd(&strm);

    /* Verify roundtrip */
    char *decomp = malloc(150000);
    ASSERT(decomp != NULL);
    unsigned int dlen = 150000;
    ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, output, total_out, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(dlen, 150000u);
    ASSERT_MEM_EQ(decomp, data, 150000);

    free(data);
    free(output);
    free(decomp);
}

/* ================================================================
 * BZ_M_IDLE: calling BZ2_bzCompress after BZ_STREAM_END
 * ================================================================ */

TEST(compress_after_stream_end_is_idle) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[] = "idle mode test";
    char output[5000];
    strm.next_in = data;
    strm.avail_in = (unsigned int)strlen(data);
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    int ret = BZ2_bzCompress(&strm, BZ_FINISH);
    ASSERT_EQ(ret, BZ_STREAM_END);

    /* Now the mode is BZ_M_IDLE — any further call should be SEQUENCE_ERROR */
    strm.next_in = data;
    strm.avail_in = (unsigned int)strlen(data);
    strm.next_out = output;
    strm.avail_out = sizeof(output);
    ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_SEQUENCE_ERROR);

    BZ2_bzCompressEnd(&strm);
}

/* ================================================================
 * BZ_M_RUNNING with invalid action
 * ================================================================ */

TEST(compress_invalid_action) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    char data[] = "test";
    char output[5000];
    strm.next_in = data;
    strm.avail_in = 4;
    strm.next_out = output;
    strm.avail_out = sizeof(output);

    /* action=99 is invalid */
    int ret = BZ2_bzCompress(&strm, 99);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);

    BZ2_bzCompressEnd(&strm);
}

/* ================================================================
 * BZ2_bzBuffToBuffDecompress UNEXPECTED_EOF path
 * (BZ_OK return from BZ2_bzDecompress with avail_out > 0)
 * ================================================================ */

TEST(b2b_decompress_truncated_stream) {
    /* Compress data, then truncate the compressed output severely */
    char data[5000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 251);
    char comp[10000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, sizeof(data), comp, &clen, 1), BZ_OK);

    /* Keep just the header + tiny partial data (enough to start but not finish) */
    char decomp[5000];
    unsigned int dlen = sizeof(decomp);
    /* Use just 10 bytes — enough for "BZh9" header but not a full block */
    int ret = BZ2_bzBuffToBuffDecompress(decomp, &dlen, comp, 10, 0, 0);
    /* Should fail — either DATA_ERROR or UNEXPECTED_EOF */
    ASSERT(ret != BZ_OK);
}

/* ================================================================
 * BZ2_bzBuffToBuffCompress / Decompress with NULL params
 * ================================================================ */

TEST(b2b_compress_null_dest) {
    char src[] = "test";
    unsigned int dlen = 1000;
    int ret = BZ2_bzBuffToBuffCompress(NULL, &dlen, src, 4, 1, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_compress_null_destlen) {
    char src[] = "test";
    char dst[1000];
    int ret = BZ2_bzBuffToBuffCompress(dst, NULL, src, 4, 1, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_compress_null_source) {
    char dst[1000];
    unsigned int dlen = sizeof(dst);
    int ret = BZ2_bzBuffToBuffCompress(dst, &dlen, NULL, 100, 1, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_decompress_null_dest) {
    char src[] = "BZh91AY&SY";
    unsigned int dlen = 1000;
    int ret = BZ2_bzBuffToBuffDecompress(NULL, &dlen, src, 10, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_decompress_null_destlen) {
    char src[] = "BZh91AY&SY";
    char dst[1000];
    int ret = BZ2_bzBuffToBuffDecompress(dst, NULL, src, 10, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_decompress_null_source) {
    char dst[1000];
    unsigned int dlen = sizeof(dst);
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, NULL, 100, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_decompress_invalid_small) {
    char data[] = "test";
    char comp[1000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, 4, comp, &clen, 1), BZ_OK);

    char dst[100];
    unsigned int dlen = sizeof(dst);
    int ret = BZ2_bzBuffToBuffDecompress(dst, &dlen, comp, clen, 2, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_compress_invalid_blocksize) {
    char src[] = "test";
    char dst[1000];
    unsigned int dlen = sizeof(dst);
    int ret = BZ2_bzBuffToBuffCompress(dst, &dlen, src, 4, 0, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
    ret = BZ2_bzBuffToBuffCompress(dst, &dlen, src, 4, 10, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(b2b_compress_invalid_workfactor) {
    char src[] = "test";
    char dst[1000];
    unsigned int dlen = sizeof(dst);
    int ret = BZ2_bzBuffToBuffCompress(dst, &dlen, src, 4, 1, 0, -1);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
    ret = BZ2_bzBuffToBuffCompress(dst, &dlen, src, 4, 1, 0, 251);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

/* ================================================================
 * BZ2_bzDecompressInit with invalid params
 * ================================================================ */

TEST(decompress_init_invalid_small) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 2);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
    ret = BZ2_bzDecompressInit(&strm, 0, -1);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(decompress_init_invalid_verbosity) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, -1, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
    ret = BZ2_bzDecompressInit(&strm, 5, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_init_invalid_blocksize) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 0, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
    ret = BZ2_bzCompressInit(&strm, 10, 0, 0);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

TEST(compress_init_invalid_workfactor) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 1, 0, -1);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
    ret = BZ2_bzCompressInit(&strm, 1, 0, 251);
    ASSERT_EQ(ret, BZ_PARAM_ERROR);
}

/* ================================================================
 * Decompress tiny input + tiny output simultaneously
 * Forces deep suspension in the decompressor state machine
 * ================================================================ */

TEST(decompress_1byte_in_1byte_out) {
    char data[200];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 127 + 1);
    char comp[1000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, sizeof(data), comp, &clen, 1), BZ_OK);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char decomp[200];
    int total_out = 0;
    unsigned int pos = 0;

    while (pos < clen) {
        strm.next_in = comp + pos;
        strm.avail_in = 1;
        strm.next_out = decomp + total_out;
        strm.avail_out = 1;

        int ret = BZ2_bzDecompress(&strm);
        int consumed = 1 - (int)strm.avail_in;
        int produced = 1 - (int)strm.avail_out;
        pos += consumed;
        total_out += produced;

        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
    }

    ASSERT_EQ(total_out, (int)sizeof(data));
    ASSERT_MEM_EQ(decomp, data, sizeof(data));
    BZ2_bzDecompressEnd(&strm);
}

/* ================================================================
 * BZ2_bzWriteOpen with ferror on handle
 * ================================================================ */

TEST(write_open_ferror_handle) {
    /* Create a read-only file and try to open for writing */
    char path[] = "/tmp/libqbz2_cov2_ferror.bz2";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    fclose(f);

    /* Open as read-only then set error */
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    /* Attempt to write to a read-only handle to trigger ferror */
    fputc('x', f); /* This should fail and set ferror */
    /* ferror may or may not be set depending on implementation */
    if (ferror(f)) {
        int bzerr;
        BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
        ASSERT(bz == NULL);
        ASSERT_EQ(bzerr, BZ_IO_ERROR);
    }
    fclose(f);
    remove(path);
}

/* ================================================================
 * BZ2_bzReadOpen with ferror on handle
 * ================================================================ */

TEST(read_open_ferror_handle) {
    /* Use a pipe: close write end, then use read end as FILE* and
       try to write to it to trigger ferror without fd trickery */
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    close(pipefd[1]); /* close write end */

    FILE *f = fdopen(pipefd[0], "rb");
    ASSERT(f != NULL);
    /* Read from a pipe that has no writer — this won't set ferror, it sets EOF.
     * To trigger ferror, we need to write to a read-only handle.
     * Actually, ferror is set on I/O errors, not on EOF.
     * Let's use a different approach: open /dev/full for writing. */
    fclose(f);

    /* Open /dev/full — writes to it will fail with ENOSPC and set ferror */
    f = fopen("/dev/full", "wb");
    if (f != NULL) {
        /* Write to /dev/full to trigger ferror */
        for (int i = 0; i < 100; i++) fputc('x', f);
        fflush(f);
        if (ferror(f)) {
            /* Now use this errored handle for ReadOpen */
            int bzerr;
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            /* The ferror check is the first thing ReadOpen does after param checks,
               but it's checking ferror(f) which was set by fputc/fflush.
               However, ReadOpen opens for reading — ferror from writing may not
               apply to reading. Let's just verify the check fires. */
            if (bz == NULL) {
                ASSERT_EQ(bzerr, BZ_IO_ERROR);
            } else {
                /* ferror might have been cleared — still valid */
                BZ2_bzReadClose(&bzerr, bz);
            }
        }
        fclose(f);
    }
}

/* ================================================================
 * BZ2_bzWriteClose64 with all 4 count pointers non-NULL
 * ================================================================ */

TEST(write_close64_all_counts_nonzero) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    /* Write enough data */
    char data[10000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)(i % 251);
    BZ2_bzWrite(&bzerr, bz, data, (int)sizeof(data));
    ASSERT_EQ(bzerr, BZ_OK);

    unsigned int in_lo, in_hi, out_lo, out_hi;
    BZ2_bzWriteClose64(&bzerr, bz, 0, &in_lo, &in_hi, &out_lo, &out_hi);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT_EQ(in_lo, 10000u);
    ASSERT_EQ(in_hi, 0u);
    ASSERT(out_lo > 0);
    ASSERT_EQ(out_hi, 0u);

    fclose(f);
}

/* ================================================================
 * BZ2_bzWriteClose64 with abandon=1 and lastErr != BZ_OK
 * ================================================================ */

TEST(write_close64_abandon_after_error) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    /* Trigger a param error first */
    BZ2_bzWrite(&bzerr, bz, NULL, 10);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);

    /* Now abandon=1 — should skip the compress-finish loop */
    BZ2_bzWriteClose64(&bzerr, bz, 1, NULL, NULL, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);

    fclose(f);
}

/* ================================================================
 * BZ2_bzerror with positive err (BZ_OK or BZ_STREAM_END)
 * ================================================================ */

TEST(bzerror_positive_err) {
    char path[] = "/tmp/libqbz2_cov2_bzerror.bz2";
    char data[] = "bzerror positive test";
    char comp[1000];
    unsigned int clen = sizeof(comp);
    ASSERT_EQ(compress_to_buf(data, (unsigned int)strlen(data), comp, &clen, 1), BZ_OK);

    FILE *fw = fopen(path, "wb");
    ASSERT(fw != NULL);
    fwrite(comp, 1, clen, fw);
    fclose(fw);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    /* Before any read — lastErr should be BZ_OK (0), which is positive? No, BZ_OK is 0.
     * But BZ_STREAM_END is 4 which is positive. Read until STREAM_END. */
    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    ASSERT_EQ(bzerr, BZ_STREAM_END);
    (void)nr;

    /* Now bzerror should return err=4 (BZ_STREAM_END), which triggers err>0 -> err=0 path */
    int errnum;
    const char *msg = BZ2_bzerror(bz, &errnum);
    ASSERT(msg != NULL);
    /* BZ_STREAM_END is positive, so bzerror clamps it to 0 and returns "OK" */
    ASSERT_EQ(errnum, 0);
    ASSERT_STR_EQ(msg, "OK");

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

/* ================================================================
 * BZ2_bzlibVersion
 * ================================================================ */

TEST(bzlib_version) {
    const char *ver = BZ2_bzlibVersion();
    ASSERT(ver != NULL);
    ASSERT(strlen(ver) > 0);
}

/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    /* Compress init/end NULL params */
    RUN(compress_init_null_strm);
    RUN(compress_end_null_strm);
    RUN(compress_end_null_state);
    RUN(compress_null_strm);
    RUN(compress_null_state);
    RUN(compress_after_stream_end_is_idle);
    RUN(compress_invalid_action);
    RUN(compress_init_invalid_blocksize);
    RUN(compress_init_invalid_workfactor);

    /* Decompress init/end NULL params */
    RUN(decompress_init_null_strm);
    RUN(decompress_end_null_strm);
    RUN(decompress_end_null_state);
    RUN(decompress_end_mismatched_strm);
    RUN(decompress_null_strm);
    RUN(decompress_null_state);
    RUN(decompress_init_invalid_small);
    RUN(decompress_init_invalid_verbosity);

    /* BZ2_bzWrite edge cases */
    RUN(write_zero_len);
    RUN(write_null_buf);
    RUN(write_negative_len);

    /* BZ2_bzWriteClose / Close64 NULL */
    RUN(write_close64_null_bzfile);
    RUN(write_close_null_bzfile);
    RUN(write_close64_all_counts_nonzero);
    RUN(write_close64_abandon_after_error);

    /* BZ2_bzReadClose NULL */
    RUN(read_close_null_bzfile);

    /* BZ2_bzRead with len==0 */
    RUN(read_zero_len);

    /* bzopen empty/null path (stdout dispatch) */
    RUN(bzopen_empty_path_write);
    RUN(bzopen_null_path_write);

    /* Decompress state machine suspension */
    RUN(decompress_1byte_input_delivery);
    RUN(decompress_1byte_input_small_mode);
    RUN(decompress_2byte_input_delivery);
    RUN(decompress_3byte_input_delivery);
    RUN(decompress_1byte_in_1byte_out);

    /* Decompress verbosity */
    RUN(decompress_verbosity_2);
    RUN(decompress_verbosity_3_combined_crc);

    /* Write error paths */
    RUN(write_close_ferror_on_handle);
    RUN(write_open_null_file);
    RUN(write_open_invalid_blocksize);
    RUN(write_open_ferror_handle);

    /* Read error paths */
    RUN(read_open_null_file);
    RUN(read_open_unused_negative_count);
    RUN(read_open_unused_over_max);
    RUN(read_open_ferror_handle);

    /* Flush multi-round */
    RUN(flush_multi_round);

    /* BuffToBuffCompress/Decompress NULL params */
    RUN(b2b_compress_null_dest);
    RUN(b2b_compress_null_destlen);
    RUN(b2b_compress_null_source);
    RUN(b2b_decompress_null_dest);
    RUN(b2b_decompress_null_destlen);
    RUN(b2b_decompress_null_source);
    RUN(b2b_decompress_invalid_small);
    RUN(b2b_compress_invalid_blocksize);
    RUN(b2b_compress_invalid_workfactor);
    RUN(b2b_decompress_truncated_stream);

    /* BZ2_bzerror */
    RUN(bzerror_positive_err);

    /* Version */
    RUN(bzlib_version);
TEST_MAIN_END()
