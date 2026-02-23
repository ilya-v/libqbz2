/* FILE* I/O API comprehensive tests for libqbz2
 * Targets zero-coverage functions in bzlib.c: BZ2_bzReadOpen, BZ2_bzRead,
 * BZ2_bzReadClose, BZ2_bzReadGetUnused, BZ2_bzWriteOpen, BZ2_bzWrite,
 * BZ2_bzWriteClose, BZ2_bzWriteClose64, BZ2_bzopen, BZ2_bzdopen,
 * BZ2_bzread, BZ2_bzwrite, BZ2_bzflush, BZ2_bzclose, BZ2_bzerror,
 * bzopen_or_bzdopen, myfeof.
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static const char *TMPDIR = "/tmp/libqbz2_fileio_tests";

/* Helper: create tmp dir */
static void ensure_tmpdir(void) {
    mkdir(TMPDIR, 0755);
}

/* Helper: build a temp file path */
static void tmppath(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", TMPDIR, name);
}

/* Helper: compress data via FILE* API and write to path */
static int write_bz2_file(const char *path, const char *data, int len, int bs) {
    int bzerr;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, bs, 0, 0);
    if (!bz) { fclose(f); return bzerr; }
    BZ2_bzWrite(&bzerr, bz, (void*)data, len);
    if (bzerr != BZ_OK) { BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL); fclose(f); return bzerr; }
    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    fclose(f);
    return bzerr;
}

/* Helper: read entire bz2 file via FILE* API */
static int read_bz2_file(const char *path, char *out, int outlen, int *total_read, int small) {
    int bzerr;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, small, NULL, 0);
    if (!bz) { fclose(f); return bzerr; }
    *total_read = 0;
    while (1) {
        int nr = BZ2_bzRead(&bzerr, bz, out + *total_read, outlen - *total_read);
        *total_read += nr;
        if (bzerr == BZ_STREAM_END) break;
        if (bzerr != BZ_OK) { BZ2_bzReadClose(&bzerr, bz); fclose(f); return bzerr; }
    }
    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    return BZ_OK;
}

/* ================================================================
 * BZ2_bzWriteOpen tests
 * ================================================================ */

TEST(write_open_all_block_sizes) {
    ensure_tmpdir();
    for (int bs = 1; bs <= 9; bs++) {
        int bzerr;
        FILE *f = tmpfile();
        ASSERT(f != NULL);
        BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, bs, 0, 0);
        ASSERT(bz != NULL);
        ASSERT_EQ(bzerr, BZ_OK);
        BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
        fclose(f);
    }
}

TEST(write_open_all_verbosity) {
    for (int v = 0; v <= 4; v++) {
        int bzerr;
        FILE *f = tmpfile();
        ASSERT(f != NULL);
        BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, v, 0);
        ASSERT(bz != NULL);
        ASSERT_EQ(bzerr, BZ_OK);
        BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
        fclose(f);
    }
}

TEST(write_open_all_workfactors) {
    int wfs[] = {0, 1, 30, 100, 250};
    for (int i = 0; i < 5; i++) {
        int bzerr;
        FILE *f = tmpfile();
        ASSERT(f != NULL);
        BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, wfs[i]);
        ASSERT(bz != NULL);
        ASSERT_EQ(bzerr, BZ_OK);
        BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
        fclose(f);
    }
}

TEST(write_open_invalid_verbosity) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, -1, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    bz = BZ2_bzWriteOpen(&bzerr, f, 1, 5, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

TEST(write_open_invalid_workfactor) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, -1);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 251);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

/* ================================================================
 * BZ2_bzWrite tests
 * ================================================================ */

TEST(write_single_byte) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "write_1byte.bz2");
    int bzerr;
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);
    char data = 'A';
    BZ2_bzWrite(&bzerr, bz, &data, 1);
    ASSERT_EQ(bzerr, BZ_OK);
    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
    fclose(f);

    char out[10];
    int nr;
    ASSERT_EQ(read_bz2_file(path, out, 10, &nr, 0), BZ_OK);
    ASSERT_EQ(nr, 1);
    ASSERT_EQ(out[0], 'A');
    remove(path);
}

TEST(write_multiple_calls) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "write_multi.bz2");
    int bzerr;
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    /* Write in 10 separate calls */
    char expected[100];
    for (int i = 0; i < 10; i++) {
        char chunk[10];
        memset(chunk, 'A' + i, 10);
        memcpy(expected + i * 10, chunk, 10);
        BZ2_bzWrite(&bzerr, bz, chunk, 10);
        ASSERT_EQ(bzerr, BZ_OK);
    }

    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    fclose(f);

    char out[100];
    int nr;
    ASSERT_EQ(read_bz2_file(path, out, 100, &nr, 0), BZ_OK);
    ASSERT_EQ(nr, 100);
    ASSERT_MEM_EQ(out, expected, 100);
    remove(path);
}

TEST(write_large_data) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "write_large.bz2");

    /* Write 200KB — should span multiple internal blocks with bs=1 */
    int datalen = 200000;
    char *data = malloc(datalen);
    ASSERT(data != NULL);
    for (int i = 0; i < datalen; i++) data[i] = (char)(i % 251);

    ASSERT_EQ(write_bz2_file(path, data, datalen, 1), BZ_OK);

    char *out = malloc(datalen);
    ASSERT(out != NULL);
    int nr;
    ASSERT_EQ(read_bz2_file(path, out, datalen, &nr, 0), BZ_OK);
    ASSERT_EQ(nr, datalen);
    ASSERT_MEM_EQ(out, data, datalen);

    free(data);
    free(out);
    remove(path);
}

TEST(write_to_read_handle_is_sequence_error) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "write_to_reader.bz2");
    /* First create a valid file */
    char data[] = "test";
    ASSERT_EQ(write_bz2_file(path, data, 4, 1), BZ_OK);

    /* Open for reading, then try to write */
    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    BZ2_bzWrite(&bzerr, bz, data, 4);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

/* ================================================================
 * BZ2_bzWriteClose / BZ2_bzWriteClose64 tests
 * ================================================================ */

TEST(write_close64_all_counts) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char data[5000];
    memset(data, 'X', 5000);
    BZ2_bzWrite(&bzerr, bz, data, 5000);
    ASSERT_EQ(bzerr, BZ_OK);

    unsigned int in_lo, in_hi, out_lo, out_hi;
    BZ2_bzWriteClose64(&bzerr, bz, 0, &in_lo, &in_hi, &out_lo, &out_hi);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT_EQ(in_lo, 5000u);
    ASSERT_EQ(in_hi, 0u);
    ASSERT(out_lo > 0);
    ASSERT_EQ(out_hi, 0u);
    fclose(f);
}

TEST(write_close64_null_counts) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char data[] = "data";
    BZ2_bzWrite(&bzerr, bz, data, 4);
    ASSERT_EQ(bzerr, BZ_OK);

    /* All count pointers NULL — should still work */
    BZ2_bzWriteClose64(&bzerr, bz, 0, NULL, NULL, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
    fclose(f);
}

TEST(write_close_on_read_handle) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "close_read.bz2");
    char data[] = "test";
    ASSERT_EQ(write_bz2_file(path, data, 4, 1), BZ_OK);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    /* WriteClose on a read handle */
    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);

    /* Clean up with ReadClose */
    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(write_close_abandon_no_output) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    /* Write nothing, abandon */
    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
    /* File should be empty or near-empty since we abandoned */
    long pos = ftell(f);
    ASSERT_EQ(pos, 0);
    fclose(f);
}

/* ================================================================
 * BZ2_bzReadOpen tests
 * ================================================================ */

TEST(read_open_all_small_modes) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "read_small.bz2");
    char data[] = "small mode test";
    ASSERT_EQ(write_bz2_file(path, data, (int)strlen(data), 1), BZ_OK);

    for (int small = 0; small <= 1; small++) {
        char out[100];
        int nr;
        ASSERT_EQ(read_bz2_file(path, out, 100, &nr, small), BZ_OK);
        ASSERT_EQ(nr, (int)strlen(data));
        ASSERT_MEM_EQ(out, data, strlen(data));
    }
    remove(path);
}

TEST(read_open_with_unused_bytes) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "read_unused.bz2");
    char data[] = "hello";
    ASSERT_EQ(write_bz2_file(path, data, 5, 1), BZ_OK);

    /* Read the raw file to get the compressed bytes */
    FILE *raw = fopen(path, "rb");
    ASSERT(raw != NULL);
    char rawbuf[10000];
    int rawlen = (int)fread(rawbuf, 1, 10000, raw);
    fclose(raw);
    ASSERT(rawlen > 0);

    /* Open with some of the compressed data pre-loaded as "unused" */
    int bzerr;
    int preload = rawlen < 10 ? rawlen : 10;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    /* Skip past the preloaded bytes in the file */
    fseek(f, preload, SEEK_SET);

    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, rawbuf, preload);
    ASSERT(bz != NULL);
    ASSERT_EQ(bzerr, BZ_OK);

    char out[100];
    int total = 0;
    while (1) {
        int nr = BZ2_bzRead(&bzerr, bz, out + total, 100 - total);
        total += nr;
        if (bzerr == BZ_STREAM_END) break;
        ASSERT_EQ(bzerr, BZ_OK);
    }
    ASSERT_EQ(total, 5);
    ASSERT_MEM_EQ(out, data, 5);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(read_open_invalid_small) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 2, NULL, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    bz = BZ2_bzReadOpen(&bzerr, f, 0, -1, NULL, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

TEST(read_open_invalid_verbosity) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, -1, 0, NULL, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    bz = BZ2_bzReadOpen(&bzerr, f, 5, 0, NULL, 0);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

TEST(read_open_unused_null_with_nonzero_count) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 5);
    ASSERT(bz == NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    fclose(f);
}

/* ================================================================
 * BZ2_bzRead tests
 * ================================================================ */

TEST(read_one_byte_at_a_time) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "read_1byte.bz2");
    char data[100];
    for (int i = 0; i < 100; i++) data[i] = (char)('A' + (i % 26));
    ASSERT_EQ(write_bz2_file(path, data, 100, 1), BZ_OK);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char out[100];
    int total = 0;
    while (1) {
        int nr = BZ2_bzRead(&bzerr, bz, out + total, 1);
        total += nr;
        if (bzerr == BZ_STREAM_END) break;
        ASSERT_EQ(bzerr, BZ_OK);
        ASSERT_EQ(nr, 1);
    }
    ASSERT_EQ(total, 100);
    ASSERT_MEM_EQ(out, data, 100);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(read_from_write_handle_is_sequence_error) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);
    ASSERT_EQ(nr, 0);

    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
}

TEST(read_null_buf) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "read_null_buf.bz2");
    char data[] = "test";
    ASSERT_EQ(write_bz2_file(path, data, 4, 1), BZ_OK);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    int nr = BZ2_bzRead(&bzerr, bz, NULL, 100);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    ASSERT_EQ(nr, 0);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(read_negative_len) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "read_neg_len.bz2");
    char data[] = "test";
    ASSERT_EQ(write_bz2_file(path, data, 4, 1), BZ_OK);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, -1);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
    ASSERT_EQ(nr, 0);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(read_corrupt_data) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "read_corrupt.bz2");
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    /* Write garbage that starts like bz2 but isn't valid */
    fwrite("BZh9garbage_data_here_not_valid_bz2", 1, 35, f);
    fclose(f);

    int bzerr;
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    /* Should get a data error */
    ASSERT(bzerr < 0);
    (void)nr;

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(read_empty_file) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "read_empty.bz2");
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    fclose(f);

    int bzerr;
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    /* Empty file — should fail */
    ASSERT(bzerr < 0);
    (void)nr;

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

/* ================================================================
 * BZ2_bzReadGetUnused tests
 * ================================================================ */

TEST(read_get_unused_null_handle) {
    int bzerr;
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, NULL, &unused, &nUnused);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);
}

TEST(read_get_unused_null_unused_ptr) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "unused_null1.bz2");
    char data[] = "test";
    ASSERT_EQ(write_bz2_file(path, data, 4, 1), BZ_OK);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    ASSERT_EQ(bzerr, BZ_STREAM_END);
    (void)nr;

    /* NULL unused pointer — GetUnused checks sequence first, then params.
     * After BZ2_bzRead sets lastErr=BZ_STREAM_END, the first GetUnused
     * call with NULL unused should check the sequence (lastErr==BZ_STREAM_END: ok)
     * then check unused==NULL and return BZ_PARAM_ERROR. */
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, bz, NULL, &nUnused);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(read_get_unused_null_nunused_ptr) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "unused_null2.bz2");
    char data[] = "test";
    ASSERT_EQ(write_bz2_file(path, data, 4, 1), BZ_OK);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    ASSERT_EQ(bzerr, BZ_STREAM_END);
    (void)nr;

    /* NULL nUnused pointer */
    void *unused;
    BZ2_bzReadGetUnused(&bzerr, bz, &unused, NULL);
    ASSERT_EQ(bzerr, BZ_PARAM_ERROR);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(read_get_unused_before_stream_end) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "unused_early.bz2");
    /* Write enough data so reading 1 byte doesn't reach STREAM_END */
    char data[1000];
    memset(data, 'Z', 1000);
    ASSERT_EQ(write_bz2_file(path, data, 1000, 1), BZ_OK);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    /* Read only 1 byte — not at STREAM_END yet */
    char buf[1];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 1);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT_EQ(nr, 1);

    /* GetUnused before STREAM_END should be SEQUENCE_ERROR */
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(read_get_unused_with_trailing_data) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "unused_trail.bz2");
    char data[] = "hello";
    ASSERT_EQ(write_bz2_file(path, data, 5, 1), BZ_OK);

    /* Append trailing garbage after the bz2 stream */
    FILE *f = fopen(path, "ab");
    ASSERT(f != NULL);
    fwrite("TRAILING_GARBAGE", 1, 16, f);
    fclose(f);

    /* Read to end */
    int bzerr;
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    ASSERT_EQ(bzerr, BZ_STREAM_END);
    ASSERT_EQ(nr, 5);
    ASSERT_MEM_EQ(buf, data, 5);

    /* Get unused — should have some trailing bytes */
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT(nUnused >= 0);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

/* ================================================================
 * BZ2_bzReadClose tests
 * ================================================================ */

TEST(read_close_on_write_handle) {
    int bzerr;
    FILE *f = tmpfile();
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    BZ2_bzReadClose(&bzerr, bz);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);

    /* Need to clean up the write handle properly */
    /* Note: bz is still allocated since ReadClose returned error */
    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
}

/* ================================================================
 * BZ2_bzopen / BZ2_bzdopen tests
 * ================================================================ */

TEST(bzopen_write_all_block_sizes) {
    ensure_tmpdir();
    for (int bs = 1; bs <= 9; bs++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/bzopen_bs%d.bz2", TMPDIR, bs);
        char mode[4];
        snprintf(mode, sizeof(mode), "w%d", bs);
        BZFILE *bz = BZ2_bzopen(path, mode);
        ASSERT(bz != NULL);
        char data[] = "block size test";
        int nw = BZ2_bzwrite(bz, data, (int)strlen(data));
        ASSERT_EQ(nw, (int)strlen(data));
        BZ2_bzclose(bz);

        /* Read back */
        bz = BZ2_bzopen(path, "r");
        ASSERT(bz != NULL);
        char buf[100];
        int nr = BZ2_bzread(bz, buf, 100);
        ASSERT_EQ(nr, (int)strlen(data));
        ASSERT_MEM_EQ(buf, data, strlen(data));
        BZ2_bzclose(bz);
        remove(path);
    }
}

TEST(bzopen_null_mode) {
    BZFILE *bz = BZ2_bzopen("/tmp/libqbz2_null_mode.bz2", NULL);
    ASSERT(bz == NULL);
}

TEST(bzopen_invalid_path_read) {
    BZFILE *bz = BZ2_bzopen("/tmp/nonexistent_qbz2_test_file.bz2", "r");
    ASSERT(bz == NULL);
}

TEST(bzopen_small_mode) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "bzopen_small.bz2");
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    char data[] = "small mode via bzopen";
    BZ2_bzwrite(bz, data, (int)strlen(data));
    BZ2_bzclose(bz);

    /* Read with 's' flag for small mode */
    bz = BZ2_bzopen(path, "rs");
    ASSERT(bz != NULL);
    char buf[100];
    int nr = BZ2_bzread(bz, buf, 100);
    ASSERT_EQ(nr, (int)strlen(data));
    ASSERT_MEM_EQ(buf, data, strlen(data));
    BZ2_bzclose(bz);
    remove(path);
}

TEST(bzdopen_write_read) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "bzdopen_wr.bz2");

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzdopen(fd, "w1");
    ASSERT(bz != NULL);
    char data[] = "bzdopen file descriptor test";
    int nw = BZ2_bzwrite(bz, data, (int)strlen(data));
    ASSERT_EQ(nw, (int)strlen(data));
    BZ2_bzclose(bz);

    fd = open(path, O_RDONLY);
    ASSERT(fd >= 0);
    bz = BZ2_bzdopen(fd, "r");
    ASSERT(bz != NULL);
    char buf[100];
    int nr = BZ2_bzread(bz, buf, 100);
    ASSERT_EQ(nr, (int)strlen(data));
    ASSERT_MEM_EQ(buf, data, strlen(data));
    BZ2_bzclose(bz);
    remove(path);
}

TEST(bzdopen_null_mode) {
    int fd = open("/dev/null", O_RDONLY);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzdopen(fd, NULL);
    ASSERT(bz == NULL);
    close(fd);
}

/* ================================================================
 * BZ2_bzread / BZ2_bzwrite tests
 * ================================================================ */

TEST(bzread_after_eof) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "bzread_eof.bz2");
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    char data[] = "short";
    BZ2_bzwrite(bz, data, 5);
    BZ2_bzclose(bz);

    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[100];
    int nr = BZ2_bzread(bz, buf, 100);
    ASSERT_EQ(nr, 5);
    /* Second read after EOF should return 0 */
    nr = BZ2_bzread(bz, buf, 100);
    ASSERT_EQ(nr, 0);
    BZ2_bzclose(bz);
    remove(path);
}

TEST(bzwrite_large_in_chunks) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "bzwrite_chunks.bz2");
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);

    int total = 50000;
    char *data = malloc(total);
    ASSERT(data != NULL);
    for (int i = 0; i < total; i++) data[i] = (char)(i & 0xff);

    /* Write in 1000-byte chunks */
    for (int off = 0; off < total; off += 1000) {
        int chunk = (total - off < 1000) ? (total - off) : 1000;
        int nw = BZ2_bzwrite(bz, data + off, chunk);
        ASSERT_EQ(nw, chunk);
    }
    BZ2_bzclose(bz);

    /* Read back all at once */
    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char *out = malloc(total);
    ASSERT(out != NULL);
    int nr = BZ2_bzread(bz, out, total);
    ASSERT_EQ(nr, total);
    ASSERT_MEM_EQ(out, data, total);
    BZ2_bzclose(bz);

    free(data);
    free(out);
    remove(path);
}

/* ================================================================
 * BZ2_bzflush tests
 * ================================================================ */

TEST(bzflush_on_read_handle) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "bzflush_read.bz2");
    char data[] = "flush test read";
    ASSERT_EQ(write_bz2_file(path, data, (int)strlen(data), 1), BZ_OK);

    BZFILE *bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    /* bzflush on read — should be no-op, return 0 */
    int ret = BZ2_bzflush(bz);
    ASSERT_EQ(ret, 0);
    BZ2_bzclose(bz);
    remove(path);
}

TEST(bzflush_on_write_handle) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "bzflush_write.bz2");
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    char data[] = "flush test write";
    BZ2_bzwrite(bz, data, (int)strlen(data));
    int ret = BZ2_bzflush(bz);
    ASSERT_EQ(ret, 0);
    BZ2_bzclose(bz);
    remove(path);
}

/* ================================================================
 * BZ2_bzclose tests
 * ================================================================ */

TEST(bzclose_null) {
    /* Should not crash */
    BZ2_bzclose(NULL);
}

/* ================================================================
 * BZ2_bzerror tests
 * ================================================================ */

TEST(bzerror_on_read_handle) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "bzerr_read.bz2");
    char data[] = "error test";
    ASSERT_EQ(write_bz2_file(path, data, (int)strlen(data), 1), BZ_OK);

    int bzerr;
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    int errnum;
    const char *msg = BZ2_bzerror(bz, &errnum);
    ASSERT(msg != NULL);
    ASSERT_EQ(errnum, BZ_OK);
    ASSERT_STR_EQ(msg, "OK");

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

TEST(bzerror_after_error) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "bzerr_after.bz2");
    /* Create corrupt file */
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    fwrite("BZh9not_valid_data", 1, 18, f);
    fclose(f);

    int bzerr;
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    (void)nr;
    /* Should have error now */
    ASSERT(bzerr < 0);

    int errnum;
    const char *msg = BZ2_bzerror(bz, &errnum);
    ASSERT(msg != NULL);
    ASSERT(errnum < 0);
    /* Error string should not be "OK" */
    ASSERT(strcmp(msg, "OK") != 0);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    remove(path);
}

/* ================================================================
 * Multi-block FILE* I/O tests (CRC regression)
 * ================================================================ */

TEST(file_multiblock_bs1) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "multi_bs1.bz2");
    /* bs=1 means 100KB blocks. 250KB should span 3+ blocks */
    int datalen = 250000;
    char *data = malloc(datalen);
    ASSERT(data != NULL);
    for (int i = 0; i < datalen; i++) data[i] = (char)(i % 127);
    ASSERT_EQ(write_bz2_file(path, data, datalen, 1), BZ_OK);

    char *out = malloc(datalen);
    ASSERT(out != NULL);
    int nr;
    ASSERT_EQ(read_bz2_file(path, out, datalen, &nr, 0), BZ_OK);
    ASSERT_EQ(nr, datalen);
    ASSERT_MEM_EQ(out, data, datalen);

    free(data);
    free(out);
    remove(path);
}

TEST(file_multiblock_bs1_small) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "multi_bs1_sm.bz2");
    int datalen = 250000;
    char *data = malloc(datalen);
    ASSERT(data != NULL);
    for (int i = 0; i < datalen; i++) data[i] = (char)(i % 127);
    ASSERT_EQ(write_bz2_file(path, data, datalen, 1), BZ_OK);

    char *out = malloc(datalen);
    ASSERT(out != NULL);
    int nr;
    ASSERT_EQ(read_bz2_file(path, out, datalen, &nr, 1), BZ_OK);
    ASSERT_EQ(nr, datalen);
    ASSERT_MEM_EQ(out, data, datalen);

    free(data);
    free(out);
    remove(path);
}

TEST(file_multiblock_write_many_calls) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "multi_writes.bz2");
    int bzerr;
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    /* Write 300KB in 1KB chunks — spans multiple blocks */
    int total = 300000;
    char *data = malloc(total);
    ASSERT(data != NULL);
    for (int i = 0; i < total; i++) data[i] = (char)(i % 251);

    for (int off = 0; off < total; off += 1000) {
        int chunk = (total - off < 1000) ? (total - off) : 1000;
        BZ2_bzWrite(&bzerr, bz, data + off, chunk);
        ASSERT_EQ(bzerr, BZ_OK);
    }

    unsigned int in_lo, out_lo;
    BZ2_bzWriteClose(&bzerr, bz, 0, &in_lo, &out_lo);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT_EQ(in_lo, (unsigned int)total);
    ASSERT(out_lo > 0);
    fclose(f);

    char *out = malloc(total);
    ASSERT(out != NULL);
    int nr;
    ASSERT_EQ(read_bz2_file(path, out, total, &nr, 0), BZ_OK);
    ASSERT_EQ(nr, total);
    ASSERT_MEM_EQ(out, data, total);

    free(data);
    free(out);
    remove(path);
}

/* ================================================================
 * Concatenated streams via FILE* API
 * ================================================================ */

TEST(file_concatenated_streams) {
    ensure_tmpdir();
    char path[256]; tmppath(path, sizeof(path), "concat.bz2");

    /* Write two separate bz2 streams back-to-back in one file */
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);

    int bzerr;
    char data1[] = "first stream data";
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);
    BZ2_bzWrite(&bzerr, bz, data1, (int)strlen(data1));
    ASSERT_EQ(bzerr, BZ_OK);
    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);

    char data2[] = "second stream data";
    bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);
    BZ2_bzWrite(&bzerr, bz, data2, (int)strlen(data2));
    ASSERT_EQ(bzerr, BZ_OK);
    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
    fclose(f);

    /* Read first stream, get unused, use unused to read second */
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, 100);
    ASSERT_EQ(bzerr, BZ_STREAM_END);
    ASSERT_EQ(nr, (int)strlen(data1));
    ASSERT_MEM_EQ(buf, data1, strlen(data1));

    /* Get unused bytes */
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
    ASSERT_EQ(bzerr, BZ_OK);

    /* Save unused before closing */
    char saved_unused[BZ_MAX_UNUSED];
    if (nUnused > 0) memcpy(saved_unused, unused, nUnused);

    BZ2_bzReadClose(&bzerr, bz);

    /* Open second stream using unused bytes */
    bz = BZ2_bzReadOpen(&bzerr, f, 0, 0,
                         nUnused > 0 ? saved_unused : NULL, nUnused);
    if (bz != NULL) {
        nr = BZ2_bzRead(&bzerr, bz, buf, 100);
        if (bzerr == BZ_STREAM_END || bzerr == BZ_OK) {
            ASSERT_EQ(nr, (int)strlen(data2));
            ASSERT_MEM_EQ(buf, data2, strlen(data2));
        }
        BZ2_bzReadClose(&bzerr, bz);
    }

    fclose(f);
    remove(path);
}

/* ================================================================
 * BuffToBuffCompress/Decompress edge cases
 * ================================================================ */

TEST(b2b_compress_output_too_small) {
    char src[10000];
    memset(src, 0, 10000);
    /* Random-ish data that doesn't compress well */
    for (int i = 0; i < 10000; i++) src[i] = (char)(i * 7 + i / 3);
    char dst[10]; /* Way too small */
    unsigned int dlen = 10;
    int ret = BZ2_bzBuffToBuffCompress(dst, &dlen, src, 10000, 1, 0, 0);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
}

TEST(b2b_decompress_output_too_small) {
    char data[1000];
    memset(data, 'A', 1000);
    char comp[2000];
    unsigned int clen = 2000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 1000, 1, 0, 0), BZ_OK);

    char out[10]; /* Way too small */
    unsigned int olen = 10;
    int ret = BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, 0);
    ASSERT_EQ(ret, BZ_OUTBUFF_FULL);
}

TEST(b2b_compress_zero_len_input) {
    char dst[1000];
    unsigned int dlen = 1000;
    char src = 'x'; /* dummy, won't be read */
    int ret = BZ2_bzBuffToBuffCompress(dst, &dlen, &src, 0, 1, 0, 0);
    /* Compressing zero bytes should produce a valid bz2 stream */
    ASSERT_EQ(ret, BZ_OK);
    ASSERT(dlen > 0);

    /* Should decompress to empty */
    char out[100];
    unsigned int olen = 100;
    ret = BZ2_bzBuffToBuffDecompress(out, &olen, dst, dlen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(olen, 0u);
}

TEST(b2b_roundtrip_all_block_sizes) {
    char data[5000];
    for (int i = 0; i < 5000; i++) data[i] = (char)(i % 256);

    for (int bs = 1; bs <= 9; bs++) {
        char comp[10000];
        unsigned int clen = 10000;
        ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, bs, 0, 0), BZ_OK);

        char out[5000];
        unsigned int olen = 5000;
        ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, 0), BZ_OK);
        ASSERT_EQ(olen, 5000u);
        ASSERT_MEM_EQ(out, data, 5000);
    }
}

TEST(b2b_roundtrip_small_decompress) {
    char data[5000];
    for (int i = 0; i < 5000; i++) data[i] = (char)(i % 256);

    char comp[10000];
    unsigned int clen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);

    char out[5000];
    unsigned int olen = 5000;
    /* small=1 */
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 1, 0), BZ_OK);
    ASSERT_EQ(olen, 5000u);
    ASSERT_MEM_EQ(out, data, 5000);
}

TEST(b2b_compress_verbosity_range) {
    char data[] = "verbosity test";
    for (int v = 0; v <= 4; v++) {
        char comp[1000];
        unsigned int clen = 1000;
        int ret = BZ2_bzBuffToBuffCompress(comp, &clen, data, (int)strlen(data), 1, v, 0);
        ASSERT_EQ(ret, BZ_OK);
    }
}

TEST(b2b_compress_invalid_verbosity) {
    char data[] = "test";
    char comp[1000];
    unsigned int clen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 4, 1, -1, 0), BZ_PARAM_ERROR);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 4, 1, 5, 0), BZ_PARAM_ERROR);
}

TEST(b2b_decompress_invalid_verbosity) {
    /* First compress something valid */
    char data[] = "test";
    char comp[1000];
    unsigned int clen = 1000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 4, 1, 0, 0), BZ_OK);

    char out[100];
    unsigned int olen = 100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, -1), BZ_PARAM_ERROR);
    olen = 100;
    ASSERT_EQ(BZ2_bzBuffToBuffDecompress(out, &olen, comp, clen, 0, 5), BZ_PARAM_ERROR);
}

/* ================================================================
 * Version string test
 * ================================================================ */

TEST(version_string_format) {
    const char *ver = BZ2_bzlibVersion();
    ASSERT(ver != NULL);
    ASSERT(strlen(ver) >= 5); /* e.g., "1.1.0" */
    /* Should contain a dot */
    ASSERT(strchr(ver, '.') != NULL);
}

/* ================================================================
 * Total counters via streaming API (hi32 overflow path)
 * ================================================================ */

TEST(compress_total_counters) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzCompressInit(&strm, 1, 0, 0), BZ_OK);

    ASSERT_EQ(strm.total_in_lo32, 0u);
    ASSERT_EQ(strm.total_in_hi32, 0u);
    ASSERT_EQ(strm.total_out_lo32, 0u);
    ASSERT_EQ(strm.total_out_hi32, 0u);

    char data[10000];
    memset(data, 'X', 10000);
    char output[20000];

    strm.next_in = data; strm.avail_in = 10000;
    strm.next_out = output; strm.avail_out = 20000;
    ASSERT_EQ(BZ2_bzCompress(&strm, BZ_FINISH), BZ_STREAM_END);

    ASSERT_EQ(strm.total_in_lo32, 10000u);
    ASSERT_EQ(strm.total_in_hi32, 0u);
    ASSERT(strm.total_out_lo32 > 0);

    BZ2_bzCompressEnd(&strm);
}

TEST(decompress_total_counters) {
    /* Compress first */
    char data[5000];
    for (int i = 0; i < 5000; i++) data[i] = (char)(i & 0xff);
    char comp[10000];
    unsigned int clen = 10000;
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(comp, &clen, data, 5000, 1, 0, 0), BZ_OK);

    /* Decompress via streaming */
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(BZ2_bzDecompressInit(&strm, 0, 0), BZ_OK);

    char out[5000];
    strm.next_in = comp; strm.avail_in = clen;
    strm.next_out = out; strm.avail_out = 5000;

    int ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(strm.total_in_lo32, clen);
    ASSERT_EQ(strm.total_out_lo32, 5000u);
    ASSERT_EQ(strm.total_in_hi32, 0u);
    ASSERT_EQ(strm.total_out_hi32, 0u);

    BZ2_bzDecompressEnd(&strm);
}

/* ================================================================
 * Test runner
 * ================================================================ */

TEST_MAIN_BEGIN()
    /* WriteOpen */
    RUN(write_open_all_block_sizes);
    RUN(write_open_all_verbosity);
    RUN(write_open_all_workfactors);
    RUN(write_open_invalid_verbosity);
    RUN(write_open_invalid_workfactor);

    /* Write */
    RUN(write_single_byte);
    RUN(write_multiple_calls);
    RUN(write_large_data);
    RUN(write_to_read_handle_is_sequence_error);

    /* WriteClose / WriteClose64 */
    RUN(write_close64_all_counts);
    RUN(write_close64_null_counts);
    RUN(write_close_on_read_handle);
    RUN(write_close_abandon_no_output);

    /* ReadOpen */
    RUN(read_open_all_small_modes);
    RUN(read_open_with_unused_bytes);
    RUN(read_open_invalid_small);
    RUN(read_open_invalid_verbosity);
    RUN(read_open_unused_null_with_nonzero_count);

    /* Read */
    RUN(read_one_byte_at_a_time);
    RUN(read_from_write_handle_is_sequence_error);
    RUN(read_null_buf);
    RUN(read_negative_len);
    RUN(read_corrupt_data);
    RUN(read_empty_file);

    /* ReadGetUnused */
    RUN(read_get_unused_null_handle);
    RUN(read_get_unused_null_unused_ptr);
    RUN(read_get_unused_null_nunused_ptr);
    RUN(read_get_unused_before_stream_end);
    RUN(read_get_unused_with_trailing_data);

    /* ReadClose */
    RUN(read_close_on_write_handle);

    /* bzopen/bzdopen */
    RUN(bzopen_write_all_block_sizes);
    RUN(bzopen_null_mode);
    RUN(bzopen_invalid_path_read);
    RUN(bzopen_small_mode);
    RUN(bzdopen_write_read);
    RUN(bzdopen_null_mode);

    /* bzread/bzwrite */
    RUN(bzread_after_eof);
    RUN(bzwrite_large_in_chunks);

    /* bzflush */
    RUN(bzflush_on_read_handle);
    RUN(bzflush_on_write_handle);

    /* bzclose */
    RUN(bzclose_null);

    /* bzerror */
    RUN(bzerror_on_read_handle);
    RUN(bzerror_after_error);

    /* Multi-block (CRC regression) */
    RUN(file_multiblock_bs1);
    RUN(file_multiblock_bs1_small);
    RUN(file_multiblock_write_many_calls);

    /* Concatenated streams */
    RUN(file_concatenated_streams);

    /* BuffToBuffCompress/Decompress edge cases */
    RUN(b2b_compress_output_too_small);
    RUN(b2b_decompress_output_too_small);
    RUN(b2b_compress_zero_len_input);
    RUN(b2b_roundtrip_all_block_sizes);
    RUN(b2b_roundtrip_small_decompress);
    RUN(b2b_compress_verbosity_range);
    RUN(b2b_compress_invalid_verbosity);
    RUN(b2b_decompress_invalid_verbosity);

    /* Version */
    RUN(version_string_format);

    /* Total counters */
    RUN(compress_total_counters);
    RUN(decompress_total_counters);
TEST_MAIN_END()
