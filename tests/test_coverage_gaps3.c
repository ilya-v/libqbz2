/*
 * Coverage gap tests round 3
 * Targets decompress.c GET_BITS suspension branches via byte-at-a-time
 * decompression, and bzlib.c FILE* API edge cases (IO errors, NULL bzerror,
 * bzopen mode parsing).
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

/* Helper: compress data (not a test, so no ASSERT macros) */
static int compress_helper(const char *src, unsigned int srcLen,
                           char *dest, unsigned int *destLen, int bs)
{
    return BZ2_bzBuffToBuffCompress(dest, destLen, (char*)src, srcLen, bs, 0, 30);
}

/* Helper: create a small valid bzip2 stream (aborts on failure) */
static char *make_bz2(unsigned int *outLen, int bs)
{
    const char *msg = "Hello, this is test data for byte-at-a-time decompression. "
                      "We need enough data to exercise multiple GET_BITS states. "
                      "AABBCCDDEE repeated padding to make the stream longer. "
                      "More text to ensure we have a reasonable-size bitstream.";
    unsigned int srcLen = (unsigned int)strlen(msg);
    unsigned int dLen = srcLen + 600;
    char *dest = malloc(dLen);
    if (!dest) abort();
    int ret = compress_helper(msg, srcLen, dest, &dLen, bs);
    if (ret != BZ_OK) { free(dest); abort(); }
    *outLen = dLen;
    return dest;
}

/* Decompress feeding N bytes at a time. Returns BZ_STREAM_END on success. */
static int decompress_n_bytes(const char *comp, unsigned int compLen,
                              char *decomp, unsigned int decompLen,
                              unsigned int chunkSize, int small)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, small);
    if (ret != BZ_OK) return ret;

    strm.next_out = decomp;
    strm.avail_out = decompLen;

    unsigned int pos = 0;
    while (pos < compLen) {
        unsigned int chunk = (compLen - pos >= chunkSize) ? chunkSize : (compLen - pos);
        strm.next_in = (char*)comp + pos;
        strm.avail_in = chunk;
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_OK) { BZ2_bzDecompressEnd(&strm); return ret; }
        pos += chunk;
    }
    BZ2_bzDecompressEnd(&strm);
    return ret;
}

/* --- Byte-at-a-time decompression tests --- */

TEST(decompress_1_byte)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    int ret = decompress_n_bytes(c, cLen, d, sizeof(d), 1, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    free(c);
}

TEST(decompress_2_bytes)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    int ret = decompress_n_bytes(c, cLen, d, sizeof(d), 2, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    free(c);
}

TEST(decompress_3_bytes)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    int ret = decompress_n_bytes(c, cLen, d, sizeof(d), 3, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    free(c);
}

TEST(decompress_5_bytes)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    int ret = decompress_n_bytes(c, cLen, d, sizeof(d), 5, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    free(c);
}

TEST(decompress_7_bytes)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    int ret = decompress_n_bytes(c, cLen, d, sizeof(d), 7, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    free(c);
}

TEST(decompress_1_byte_small)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    int ret = decompress_n_bytes(c, cLen, d, sizeof(d), 1, 1);
    ASSERT_EQ(ret, BZ_STREAM_END);
    free(c);
}

TEST(decompress_1_byte_bs9)
{
    char src[4096];
    for (int i = 0; i < (int)sizeof(src); i++) src[i] = (char)((i*7+13)%256);
    unsigned int cLen = sizeof(src) + 1000;
    char *c = malloc(cLen);
    ASSERT(c != NULL);
    int ret = compress_helper(src, sizeof(src), c, &cLen, 9);
    ASSERT_EQ(ret, BZ_OK);
    char d[8192];
    ret = decompress_n_bytes(c, cLen, d, sizeof(d), 1, 0);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT(memcmp(d, src, sizeof(src)) == 0);
    free(c);
}

TEST(decompress_alternating_1_4)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    strm.next_out = d;
    strm.avail_out = sizeof(d);
    unsigned int pos = 0;
    int toggle = 0;
    while (pos < cLen) {
        unsigned int sz = (toggle++ % 2 == 0) ? 1 : 4;
        unsigned int chunk = (cLen - pos >= sz) ? sz : (cLen - pos);
        strm.next_in = c + pos;
        strm.avail_in = chunk;
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
        pos += chunk;
    }
    ASSERT_EQ(ret, BZ_STREAM_END);
    BZ2_bzDecompressEnd(&strm);
    free(c);
}

TEST(decompress_tiny_output)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    unsigned int opos = 0;
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    strm.next_in = c;
    strm.avail_in = cLen;
    while (1) {
        strm.next_out = d + opos;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
        if (strm.avail_out == 0) opos++;
        ASSERT(opos < sizeof(d));
    }
    BZ2_bzDecompressEnd(&strm);
    free(c);
}

TEST(decompress_1in_1out)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    unsigned int ipos = 0, opos = 0;
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    int iter = 0;
    while (1) {
        if (strm.avail_in == 0 && ipos < cLen) {
            strm.next_in = c + ipos;
            strm.avail_in = 1;
            ipos++;
        }
        strm.next_out = d + opos;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        if (ret == BZ_STREAM_END) break;
        ASSERT_EQ(ret, BZ_OK);
        if (strm.avail_out == 0) { opos++; ASSERT(opos < sizeof(d)); }
        ASSERT(++iter < 200000);
    }
    BZ2_bzDecompressEnd(&strm);
    free(c);
}

/* --- Verbose mode tests --- */

TEST(decompress_verbose2)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    unsigned int dLen = sizeof(d);
    int ret = BZ2_bzBuffToBuffDecompress(d, &dLen, c, cLen, 0, 2);
    ASSERT_EQ(ret, BZ_OK);
    free(c);
}

TEST(decompress_verbose3)
{
    unsigned int cLen;
    char *c = make_bz2(&cLen, 1);
    char d[2048];
    unsigned int dLen = sizeof(d);
    int ret = BZ2_bzBuffToBuffDecompress(d, &dLen, c, cLen, 0, 3);
    ASSERT_EQ(ret, BZ_OK);
    free(c);
}

TEST(compress_verbose3)
{
    const char *data = "Test data for verbose compression output";
    unsigned int dLen = 600;
    char dest[600];
    int ret = BZ2_bzBuffToBuffCompress(dest, &dLen, (char*)data,
                                       (unsigned int)strlen(data), 1, 3, 30);
    ASSERT_EQ(ret, BZ_OK);
}

/* --- FILE* API edge cases --- */

TEST(bzopen_small_mode)
{
    char path[] = "/tmp/test_bzs_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    BZ2_bzwrite(bz, (void*)"test", 4);
    BZ2_bzclose(bz);
    bz = BZ2_bzopen(path, "rs");
    ASSERT(bz != NULL);
    char buf[64];
    int n = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT(n == 4);
    BZ2_bzclose(bz);
    unlink(path);
    close(fd);
}

TEST(bzdopen_read)
{
    char path[] = "/tmp/test_bdr_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    BZ2_bzwrite(bz, (void*)"bzdopen", 7);
    BZ2_bzclose(bz);
    int rfd = open(path, O_RDONLY);
    ASSERT(rfd >= 0);
    bz = BZ2_bzdopen(rfd, "r");
    ASSERT(bz != NULL);
    char buf[64];
    int n = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT_EQ(n, 7);
    BZ2_bzclose(bz);
    unlink(path);
    close(fd);
}

TEST(bzdopen_write)
{
    char path[] = "/tmp/test_bdw_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    int wfd = open(path, O_WRONLY | O_TRUNC);
    ASSERT(wfd >= 0);
    BZFILE *bz = BZ2_bzdopen(wfd, "w1");
    ASSERT(bz != NULL);
    BZ2_bzwrite(bz, (void*)"bzdw", 4);
    BZ2_bzclose(bz);
    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[64];
    int n = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT_EQ(n, 4);
    BZ2_bzclose(bz);
    unlink(path);
    close(fd);
}

TEST(bzopen_null_mode)
{
    BZFILE *bz = BZ2_bzopen("/tmp/nonexistent", NULL);
    ASSERT(bz == NULL);
}

TEST(bzopen_invalid_path)
{
    BZFILE *bz = BZ2_bzopen("/tmp/no_such_dir_xyz123/file.bz2", "r");
    ASSERT(bz == NULL);
}

TEST(bzread_null_bzerror)
{
    char path[] = "/tmp/test_nbe_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"null err", 8);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    BZFILE *rbz = BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 0);
    ASSERT(rbz != NULL);
    char buf[64];
    int n = BZ2_bzRead(NULL, rbz, buf, sizeof(buf));
    ASSERT(n > 0);
    BZ2_bzReadClose(NULL, rbz);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(bzwrite_null_bzerror)
{
    char path[] = "/tmp/test_nbw_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(NULL, f, 1, 0, 30);
    ASSERT(bz != NULL);
    BZ2_bzWrite(NULL, bz, (void*)"null bzerror", 12);
    BZ2_bzWriteClose(NULL, bz, 0, NULL, NULL);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(bzreadgetunused_null_bzerror)
{
    char path[] = "/tmp/test_gtu_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"unused", 6);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(rbz != NULL);
    char buf[64];
    BZ2_bzRead(&bzerr, rbz, buf, sizeof(buf));
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(NULL, rbz, &unused, &nUnused);
    BZ2_bzReadClose(&bzerr, rbz);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeclose64_all_counts)
{
    char path[] = "/tmp/test_wc6_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 30);
    ASSERT(bz != NULL);
    BZ2_bzWrite(&bzerr, bz, (void*)"count data", 10);
    ASSERT_EQ(bzerr, BZ_OK);
    unsigned int ilo, ihi, olo, ohi;
    BZ2_bzWriteClose64(&bzerr, bz, 0, &ilo, &ihi, &olo, &ohi);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT(ilo > 0);
    ASSERT(olo > 0);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeclose_io_error)
{
    struct sigaction sa, old;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, &old);
    int pfd[2];
    ASSERT_EQ(pipe(pfd), 0);
    close(pfd[0]);
    FILE *fw = fdopen(pfd[1], "wb");
    ASSERT(fw != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 30);
    ASSERT(bz != NULL);
    BZ2_bzWrite(&bzerr, bz, (void*)"pipe data", 9);
    if (bzerr == BZ_OK) {
        BZ2_bzWriteClose64(&bzerr, bz, 0, NULL, NULL, NULL, NULL);
        if (bzerr == BZ_IO_ERROR) {
            clearerr(fw);
            BZ2_bzWriteClose64(&bzerr, bz, 1, NULL, NULL, NULL, NULL);
        }
    }
    fclose(fw);
    sigaction(SIGPIPE, &old, NULL);
}

TEST(bzclose_null)
{
    BZ2_bzclose(NULL);
}

TEST(bzerror_read_write)
{
    char path[] = "/tmp/test_bze_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    int errnum;
    const char *s = BZ2_bzerror(bz, &errnum);
    ASSERT(s != NULL);
    ASSERT_EQ(errnum, BZ_OK);
    BZ2_bzwrite(bz, (void*)"err", 3);
    BZ2_bzclose(bz);
    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[64];
    BZ2_bzread(bz, buf, sizeof(buf));
    BZ2_bzread(bz, buf, sizeof(buf)); /* returns 0 at STREAM_END */
    BZ2_bzclose(bz);
    unlink(path);
    close(fd);
}

TEST(bzwrite_to_read_handle)
{
    char path[] = "/tmp/test_wtr_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"x", 1);
    BZ2_bzclose(bz);
    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    int n = BZ2_bzwrite(bz, (void*)"y", 1);
    ASSERT_EQ(n, -1);
    BZ2_bzclose(bz);
    unlink(path);
    close(fd);
}

TEST(bzread_from_write_handle)
{
    char path[] = "/tmp/test_rfw_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    char buf[64];
    int n = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT_EQ(n, -1);
    BZ2_bzclose(bz);
    unlink(path);
    close(fd);
}

TEST(bzopen_empty_path_write)
{
    char path[] = "/tmp/test_epw_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    int saved = dup(STDOUT_FILENO);
    ASSERT(saved >= 0);
    int rfd = open(path, O_WRONLY | O_TRUNC);
    ASSERT(rfd >= 0);
    dup2(rfd, STDOUT_FILENO);
    close(rfd);
    BZFILE *bz = BZ2_bzopen("", "w1");
    ASSERT(bz != NULL);
    BZ2_bzwrite(bz, (void*)"stdout", 6);
    BZ2_bzclose(bz);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[64];
    int n = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT_EQ(n, 6);
    BZ2_bzclose(bz);
    unlink(path);
    close(fd);
}

TEST(bzreadopen_with_unused)
{
    char path[] = "/tmp/test_rwu_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"unused test", 11);
    BZ2_bzclose(bz);
    char fbuf[8192];
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    size_t flen = fread(fbuf, 1, sizeof(fbuf), f);
    fclose(f);
    ASSERT(flen > 4);
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    fseek(f, 4, SEEK_SET);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, fbuf, 4);
    ASSERT(rbz != NULL);
    char buf[64];
    int n = BZ2_bzRead(&bzerr, rbz, buf, sizeof(buf));
    ASSERT(n > 0);
    BZ2_bzReadClose(&bzerr, rbz);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(bzreadclose_on_write)
{
    char path[] = "/tmp/test_rcw_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 30);
    ASSERT(bz != NULL);
    BZ2_bzReadClose(&bzerr, bz);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);
    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(bzread_on_write)
{
    char path[] = "/tmp/test_row_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 30);
    ASSERT(bz != NULL);
    char buf[64];
    int n = BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
    ASSERT_EQ(n, 0);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);
    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(bzwrite_on_read)
{
    char path[] = "/tmp/test_wor_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"d", 1);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(rbz != NULL);
    BZ2_bzWrite(&bzerr, rbz, (void*)"x", 1);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);
    BZ2_bzReadClose(&bzerr, rbz);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(writeclose_on_read)
{
    char path[] = "/tmp/test_wcr_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    BZFILE *bz = BZ2_bzopen(path, "w1");
    BZ2_bzwrite(bz, (void*)"d", 1);
    BZ2_bzclose(bz);
    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(rbz != NULL);
    BZ2_bzWriteClose64(&bzerr, rbz, 0, NULL, NULL, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);
    BZ2_bzReadClose(&bzerr, rbz);
    fclose(f);
    unlink(path);
    close(fd);
}

TEST(bzread_concat_streams)
{
    char path[] = "/tmp/test_cat_XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    char c1[600], c2[600];
    unsigned int c1l = 600, c2l = 600;
    compress_helper("first", 5, c1, &c1l, 1);
    compress_helper("second", 6, c2, &c2l, 1);
    fwrite(c1, 1, c1l, f);
    fwrite(c2, 1, c2l, f);
    fclose(f);
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(rbz != NULL);
    char buf[256];
    int total = 0, streams = 0;
    while (1) {
        int n = BZ2_bzRead(&bzerr, rbz, buf + total, (int)(sizeof(buf)-total));
        if (bzerr == BZ_STREAM_END) {
            total += n;
            streams++;
            void *unused; int nU;
            BZ2_bzReadGetUnused(&bzerr, rbz, &unused, &nU);
            /* Save unused data before closing — unused points into rbz */
            char savedUnused[BZ_MAX_UNUSED];
            if (nU > 0) memcpy(savedUnused, unused, nU);
            BZ2_bzReadClose(&bzerr, rbz);
            rbz = NULL;
            if (nU == 0 && feof(f)) break;
            rbz = BZ2_bzReadOpen(&bzerr, f, 0, 0, savedUnused, nU);
            if (!rbz) break;
        } else if (bzerr == BZ_OK) {
            total += n;
        } else break;
    }
    if (rbz) BZ2_bzReadClose(&bzerr, rbz);
    fclose(f);
    ASSERT_EQ(streams, 2);
    ASSERT_EQ(total, 11);
    unlink(path);
    close(fd);
}

/* --- Compress streaming --- */

TEST(compress_1_byte_at_a_time)
{
    const char *src = "Test data for one-byte-at-a-time compression streaming. "
                      "AAAA BBBB CCCC padding text to make it bigger.";
    unsigned int sLen = (unsigned int)strlen(src);
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 1, 0, 30);
    ASSERT_EQ(ret, BZ_OK);
    char comp[4096];
    strm.next_out = comp;
    strm.avail_out = sizeof(comp);
    for (unsigned int i = 0; i < sLen; i++) {
        strm.next_in = (char*)src + i;
        strm.avail_in = 1;
        ret = BZ2_bzCompress(&strm, BZ_RUN);
        ASSERT_EQ(ret, BZ_RUN_OK);
    }
    strm.avail_in = 0;
    do { ret = BZ2_bzCompress(&strm, BZ_FINISH); } while (ret == BZ_FINISH_OK);
    ASSERT_EQ(ret, BZ_STREAM_END);
    unsigned int cLen = sizeof(comp) - strm.avail_out;
    BZ2_bzCompressEnd(&strm);
    char dec[1024];
    unsigned int dLen = sizeof(dec);
    ret = BZ2_bzBuffToBuffDecompress(dec, &dLen, comp, cLen, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(dLen, sLen);
}

TEST(compress_flush_between_chunks)
{
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzCompressInit(&strm, 1, 0, 30);
    ASSERT_EQ(ret, BZ_OK);
    char comp[8192];
    strm.next_out = comp;
    strm.avail_out = sizeof(comp);
    strm.next_in = (char*)"First chunk.";
    strm.avail_in = 12;
    ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_RUN_OK);
    strm.avail_in = 0;
    do { ret = BZ2_bzCompress(&strm, BZ_FLUSH); } while (ret == BZ_FLUSH_OK);
    ASSERT_EQ(ret, BZ_RUN_OK);
    strm.next_in = (char*)"Second chunk.";
    strm.avail_in = 13;
    ret = BZ2_bzCompress(&strm, BZ_RUN);
    ASSERT_EQ(ret, BZ_RUN_OK);
    strm.avail_in = 0;
    do { ret = BZ2_bzCompress(&strm, BZ_FINISH); } while (ret == BZ_FINISH_OK);
    ASSERT_EQ(ret, BZ_STREAM_END);
    BZ2_bzCompressEnd(&strm);
}

TEST(decompress_exact_output)
{
    const char *src = "Exact output size test!";
    unsigned int sLen = (unsigned int)strlen(src);
    unsigned int cLen = sLen + 600;
    char *c = malloc(cLen);
    ASSERT(c != NULL);
    int ret = compress_helper(src, sLen, c, &cLen, 1);
    ASSERT_EQ(ret, BZ_OK);
    char *d = malloc(sLen);
    ASSERT(d != NULL);
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    ret = BZ2_bzDecompressInit(&strm, 0, 0);
    ASSERT_EQ(ret, BZ_OK);
    strm.next_in = c;
    strm.avail_in = cLen;
    strm.next_out = d;
    strm.avail_out = sLen;
    ret = BZ2_bzDecompress(&strm);
    ASSERT_EQ(ret, BZ_STREAM_END);
    ASSERT_EQ(strm.avail_out, 0u);
    BZ2_bzDecompressEnd(&strm);
    free(c);
    free(d);
}

TEST_MAIN_BEGIN()
    /* Byte-at-a-time decompression */
    RUN(decompress_1_byte);
    RUN(decompress_2_bytes);
    RUN(decompress_3_bytes);
    RUN(decompress_5_bytes);
    RUN(decompress_7_bytes);
    RUN(decompress_1_byte_small);
    RUN(decompress_1_byte_bs9);
    RUN(decompress_alternating_1_4);
    RUN(decompress_tiny_output);
    RUN(decompress_1in_1out);
    /* Verbose */
    RUN(decompress_verbose2);
    RUN(decompress_verbose3);
    RUN(compress_verbose3);
    /* FILE* API */
    RUN(bzopen_small_mode);
    RUN(bzdopen_read);
    RUN(bzdopen_write);
    RUN(bzopen_null_mode);
    RUN(bzopen_invalid_path);
    RUN(bzread_null_bzerror);
    RUN(bzwrite_null_bzerror);
    RUN(bzreadgetunused_null_bzerror);
    RUN(writeclose64_all_counts);
    RUN(writeclose_io_error);
    RUN(bzclose_null);
    RUN(bzerror_read_write);
    RUN(bzwrite_to_read_handle);
    RUN(bzread_from_write_handle);
    RUN(bzopen_empty_path_write);
    RUN(bzreadopen_with_unused);
    RUN(bzreadclose_on_write);
    RUN(bzread_on_write);
    RUN(bzwrite_on_read);
    RUN(writeclose_on_read);
    RUN(bzread_concat_streams);
    /* Compress streaming */
    RUN(compress_1_byte_at_a_time);
    RUN(compress_flush_between_chunks);
    RUN(decompress_exact_output);
TEST_MAIN_END()
