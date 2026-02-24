/*
 * test_coverage_gaps.c — Tests targeting branch coverage gaps in bzlib.c
 * and decompress.c, identified from coverage-76996f.md.
 *
 * Targets:
 *   - Randomised block decompression (both FAST and SMALL paths)
 *   - BZ2_bzread after STREAM_END (returns 0)
 *   - BZ2_bzclose retry path (write close error -> abandon retry)
 *   - BZ2_bzerror with various error states
 *   - Verbose mode (verbosity > 0)
 *   - bzopen empty/NULL path (stdin/stdout redirect)
 *   - BZ2_bzflush no-op behavior
 *   - BZ2_bzReadGetUnused after successful read
 *   - BZ2_bzWriteClose with non-NULL byte count pointers
 *   - BZ2_bzWriteClose with abandon=1
 *   - BZ2_bzRead with len=0
 *   - BZ2_bzWrite with len=0
 */
#include "test_framework.h"
#include "bzlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/* Helper: compress data, return compressed buffer. Caller frees. */
static unsigned int helper_compress(const char *src, unsigned int srcLen,
                                    char **out, int bs) {
    unsigned int destLen = srcLen + srcLen / 100 + 600 + 10000;
    *out = malloc(destLen);
    if (!*out) return 0;
    int ret = BZ2_bzBuffToBuffCompress(*out, &destLen,
                                      (char*)src, srcLen, bs, 0, 0);
    if (ret != BZ_OK) { free(*out); *out = NULL; return 0; }
    return destLen;
}

/*
 * Helper: find the randomisation bit position in a bz2 stream.
 * The block header is: 0x314159265359 (48 bits), then 4 bytes CRC (32 bits),
 * then 1 bit randomised flag. That's 48+32 = 80 bits from the block header start.
 * The stream header is: 'B','Z','h',<level> = 4 bytes = 32 bits.
 * For a single-block stream, the block header immediately follows.
 * So the randomised bit is at bit offset 32 + 48 + 32 = 112 from stream start,
 * which is byte 14, bit 0 (MSB-first).
 */
static void flip_randomised_bit(char *bz2data, unsigned int len) {
    /* The bit at stream offset 112 (byte 14, MSB bit 0) */
    if (len > 14) {
        bz2data[14] ^= 0x80;  /* flip the MSB of byte 14 */
    }
}

/* --- Randomised block decompression tests --- */
/*
 * The randomisation table BZ2_rNums starts with 619, 720, 127, ...
 * The first byte that gets XORed with 1 is output byte #619 (0-indexed).
 * So we need at least ~700 bytes of decompressed output for the
 * randomisation to have any observable effect on the data/CRC.
 * We use 2000-byte inputs (block size 1 = 100KB, so single block).
 */

TEST(randomised_block_fast_mode) {
    /* Compress 2000 bytes, flip the randomisation bit, decompress with small=0.
     * This exercises the blockRandomised path in unRLE_obuf_to_output_FAST.
     * The CRC will mismatch because randomisation XORs certain output bytes. */
    char src[2000];
    for (int i = 0; i < (int)sizeof(src); i++) src[i] = (char)((i * 7 + 3) % 256);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 14);

    /* Flip the randomised bit */
    flip_randomised_bit(compressed, compLen);

    /* Decompress — should get BZ_DATA_ERROR (CRC mismatch after randomised decode) */
    char dest[3000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR);
    free(compressed);
}

TEST(randomised_block_small_mode) {
    /* Same test with small=1 to exercise unRLE_obuf_to_output_SMALL randomised path */
    char src[2000];
    for (int i = 0; i < (int)sizeof(src); i++) src[i] = (char)((i * 11 + 5) % 256);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 14);

    flip_randomised_bit(compressed, compLen);

    char dest[3000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR);
    free(compressed);
}

TEST(randomised_block_streaming_fast) {
    /* Streaming decompress with randomised bit — exercises avail_out=0 resume
     * in the randomised path of unRLE_obuf_to_output_FAST */
    char src[2000];
    for (int i = 0; i < (int)sizeof(src); i++) src[i] = (char)(i % 128);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 14);
    flip_randomised_bit(compressed, compLen);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 0); /* small=0 */
    ASSERT_EQ(ret, BZ_OK);

    strm.next_in = compressed;
    strm.avail_in = compLen;

    char out[3000];
    unsigned int total = 0;
    /* Feed 1 byte at a time to exercise the avail_out=0 resume path */
    while (1) {
        strm.next_out = out + total;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        if (ret != BZ_OK) break;
        total += (1 - strm.avail_out);
        if (total >= sizeof(out) - 1) break;
    }
    /* Should eventually get BZ_DATA_ERROR from CRC mismatch */
    ASSERT_EQ(ret, BZ_DATA_ERROR);
    BZ2_bzDecompressEnd(&strm);
    free(compressed);
}

TEST(randomised_block_streaming_small) {
    /* Same streaming test with small=1 */
    char src[2000];
    for (int i = 0; i < (int)sizeof(src); i++) src[i] = (char)(i % 128);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 14);
    flip_randomised_bit(compressed, compLen);

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    int ret = BZ2_bzDecompressInit(&strm, 0, 1); /* small=1 */
    ASSERT_EQ(ret, BZ_OK);

    strm.next_in = compressed;
    strm.avail_in = compLen;

    char out[3000];
    unsigned int total = 0;
    while (1) {
        strm.next_out = out + total;
        strm.avail_out = 1;
        ret = BZ2_bzDecompress(&strm);
        if (ret != BZ_OK) break;
        total += (1 - strm.avail_out);
        if (total >= sizeof(out) - 1) break;
    }
    ASSERT_EQ(ret, BZ_DATA_ERROR);
    BZ2_bzDecompressEnd(&strm);
    free(compressed);
}

TEST(randomised_block_alternating_fast) {
    /* Alternating data that doesn't compress well via initial RLE,
     * ensuring the internal block is large enough to trigger randomisation.
     * Exercises the randomised RLE run-length paths in FAST mode. */
    char src[2000];
    for (int i = 0; i < (int)sizeof(src); i++) src[i] = (char)((i * 13 + 17) % 251);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 14);
    flip_randomised_bit(compressed, compLen);

    char dest[3000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 0, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR);
    free(compressed);
}

TEST(randomised_block_alternating_small) {
    /* Same with small mode */
    char src[2000];
    for (int i = 0; i < (int)sizeof(src); i++) src[i] = (char)((i * 17 + 31) % 251);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 14);
    flip_randomised_bit(compressed, compLen);

    char dest[3000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 1, 0);
    ASSERT_EQ(ret, BZ_DATA_ERROR);
    free(compressed);
}

/* --- bzread after STREAM_END --- */

TEST(bzread_after_stream_end) {
    /* Write a small file, read it completely, then read again — should return 0 */
    const char *path = "/tmp/libqbz2_test_bzread_eof.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    const char *data = "test data for eof check";
    int nw = BZ2_bzwrite(bz, (void*)data, (int)strlen(data));
    ASSERT_EQ(nw, (int)strlen(data));
    BZ2_bzclose(bz);

    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[200];
    int nr = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT_EQ(nr, (int)strlen(data));
    /* Now read again after stream end — should return 0 */
    nr = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT_EQ(nr, 0);
    BZ2_bzclose(bz);
    unlink(path);
}

/* --- BZ2_bzerror tests --- */

TEST(bzerror_after_successful_write) {
    const char *path = "/tmp/libqbz2_test_bzerror.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    int errnum;
    const char *msg = BZ2_bzerror(bz, &errnum);
    ASSERT_EQ(errnum, BZ_OK);
    ASSERT_STR_EQ(msg, "OK");
    BZ2_bzclose(bz);
    unlink(path);
}

TEST(bzerror_after_successful_read) {
    const char *path = "/tmp/libqbz2_test_bzerror_r.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    BZ2_bzwrite(bz, (void*)"hello", 5);
    BZ2_bzclose(bz);

    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    int errnum;
    const char *msg = BZ2_bzerror(bz, &errnum);
    ASSERT_EQ(errnum, BZ_OK);
    ASSERT_STR_EQ(msg, "OK");

    char buf[100];
    BZ2_bzread(bz, buf, sizeof(buf));
    /* After STREAM_END, bzerror clamps positive errors (like BZ_STREAM_END=4)
     * to 0, so errnum is 0 and msg is "OK" */
    msg = BZ2_bzerror(bz, &errnum);
    ASSERT_EQ(errnum, 0);
    ASSERT_STR_EQ(msg, "OK");
    BZ2_bzclose(bz);
    unlink(path);
}

/* --- BZ2_bzWrite with len=0 --- */

TEST(bzwrite_len_zero) {
    const char *path = "/tmp/libqbz2_test_bzwrite0.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    int nw = BZ2_bzwrite(bz, (void*)"data", 0);
    /* bzwrite with len=0 should return 0 (nothing written, no error) */
    ASSERT_EQ(nw, 0);
    BZ2_bzclose(bz);
    unlink(path);
}

/* --- BZ2_bzRead with len=0 --- */

TEST(bzread_len_zero) {
    const char *path = "/tmp/libqbz2_test_bzread0.bz2";
    BZFILE *bz = BZ2_bzopen(path, "w1");
    ASSERT(bz != NULL);
    BZ2_bzwrite(bz, (void*)"hello", 5);
    BZ2_bzclose(bz);

    bz = BZ2_bzopen(path, "r");
    ASSERT(bz != NULL);
    char buf[10];
    int nr = BZ2_bzread(bz, buf, 0);
    ASSERT_EQ(nr, 0);
    BZ2_bzclose(bz);
    unlink(path);
}

/* --- Verbose mode --- */

TEST(compress_verbose_mode) {
    /* Compress with verbosity=1 — exercises VPrintf paths */
    char src[200];
    for (int i = 0; i < 200; i++) src[i] = (char)(i % 50);
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, sizeof(src), 1, 1, 0);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT(destLen > 0);
}

TEST(decompress_verbose_mode) {
    /* Compress then decompress with verbosity=1 */
    char src[200];
    for (int i = 0; i < 200; i++) src[i] = (char)(i % 50);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    char dest[300];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 0, 1);
    ASSERT_EQ(ret, BZ_OK);
    ASSERT_EQ(destLen, sizeof(src));
    ASSERT_MEM_EQ(dest, src, sizeof(src));
    free(compressed);
}

TEST(compress_verbose_high) {
    /* Compress with verbosity=4 (maximum) — exercises all VPrintf levels */
    char src[100];
    for (int i = 0; i < 100; i++) src[i] = (char)(i % 30);
    char dest[1000];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffCompress(dest, &destLen, src, sizeof(src), 1, 4, 0);
    ASSERT_EQ(ret, BZ_OK);
}

TEST(decompress_verbose_high) {
    /* Decompress with verbosity=4 */
    char src[100];
    for (int i = 0; i < 100; i++) src[i] = (char)(i % 30);
    char *compressed = NULL;
    unsigned int compLen = helper_compress(src, sizeof(src), &compressed, 1);
    ASSERT(compLen > 0);

    char dest[200];
    unsigned int destLen = sizeof(dest);
    int ret = BZ2_bzBuffToBuffDecompress(dest, &destLen, compressed, compLen, 0, 4);
    ASSERT_EQ(ret, BZ_OK);
    free(compressed);
}

/* --- BZ2_bzWriteClose64 with byte count pointers --- */

TEST(write_close64_all_counts) {
    const char *path = "/tmp/libqbz2_test_wc64.bz2";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);
    ASSERT_EQ(bzerr, BZ_OK);

    char data[1000];
    for (int i = 0; i < 1000; i++) data[i] = (char)(i % 100);
    BZ2_bzWrite(&bzerr, bz, data, sizeof(data));
    ASSERT_EQ(bzerr, BZ_OK);

    unsigned int in_lo, in_hi, out_lo, out_hi;
    BZ2_bzWriteClose64(&bzerr, bz, 0, &in_lo, &in_hi, &out_lo, &out_hi);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT_EQ(in_lo, sizeof(data));
    ASSERT_EQ(in_hi, 0);
    ASSERT(out_lo > 0); /* Some compressed output */
    ASSERT_EQ(out_hi, 0);
    fclose(f);
    unlink(path);
}

/* --- BZ2_bzWriteClose with abandon --- */

TEST(write_close_abandon) {
    const char *path = "/tmp/libqbz2_test_abandon.bz2";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    int bzerr;
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char data[500];
    memset(data, 'X', sizeof(data));
    BZ2_bzWrite(&bzerr, bz, data, sizeof(data));
    ASSERT_EQ(bzerr, BZ_OK);

    /* Abandon — should not finalize the stream */
    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    ASSERT_EQ(bzerr, BZ_OK);
    fclose(f);
    unlink(path);
}

/* --- BZ2_bzdopen tests --- */

TEST(bzdopen_write_read) {
    const char *path = "/tmp/libqbz2_test_bzdopen.bz2";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT(fd >= 0);

    BZFILE *bz = BZ2_bzdopen(fd, "w1");
    ASSERT(bz != NULL);
    int nw = BZ2_bzwrite(bz, (void*)"bzdopen test", 12);
    ASSERT_EQ(nw, 12);
    BZ2_bzclose(bz);
    /* fd is closed by bzclose */

    fd = open(path, O_RDONLY);
    ASSERT(fd >= 0);
    bz = BZ2_bzdopen(fd, "r");
    ASSERT(bz != NULL);
    char buf[50];
    int nr = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT_EQ(nr, 12);
    ASSERT_MEM_EQ(buf, "bzdopen test", 12);
    BZ2_bzclose(bz);
    unlink(path);
}

/* --- BZ2_bzReadGetUnused after full read --- */

TEST(read_get_unused_after_stream_end) {
    const char *path = "/tmp/libqbz2_test_unused.bz2";
    int bzerr;

    /* Write a file */
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);
    BZ2_bzWrite(&bzerr, bz, (void*)"unused test data", 16);
    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    fclose(f);

    /* Read back */
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    char buf[100];
    int nr = BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
    ASSERT_EQ(bzerr, BZ_STREAM_END);
    ASSERT_EQ(nr, 16);

    /* Now get unused bytes */
    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
    ASSERT_EQ(bzerr, BZ_OK);
    ASSERT(nUnused >= 0);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    unlink(path);
}

/* --- BZ2_bzReadOpen with unused bytes --- */

TEST(read_open_with_unused) {
    /* Compress some data to a file */
    const char *path = "/tmp/libqbz2_test_with_unused.bz2";
    int bzerr;
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);
    BZ2_bzWrite(&bzerr, bz, (void*)"test", 4);
    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    fclose(f);

    /* Read back, first time to get unused bytes, second time to use them */
    f = fopen(path, "rb");
    ASSERT(f != NULL);
    bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);
    char buf[100];
    BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
    ASSERT_EQ(bzerr, BZ_STREAM_END);

    void *unused;
    int nUnused;
    BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
    ASSERT_EQ(bzerr, BZ_OK);

    /* Save unused for re-open */
    char savedUnused[5120];
    int savedN = nUnused;
    if (nUnused > 0) memcpy(savedUnused, unused, nUnused);

    BZ2_bzReadClose(&bzerr, bz);

    /* Re-open with unused bytes — tests the nUnused > 0 path in BZ2_bzReadOpen */
    if (savedN > 0) {
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, savedUnused, savedN);
        ASSERT(bz != NULL);
        BZ2_bzReadClose(&bzerr, bz);
    }
    fclose(f);
    unlink(path);
}

/* --- BZ2_bzReadClose on a write handle (sequence error) --- */

TEST(read_close_on_write_handle) {
    const char *path = "/tmp/libqbz2_test_rcow.bz2";
    int bzerr;
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    /* Try to ReadClose a write handle — should get SEQUENCE_ERROR */
    BZ2_bzReadClose(&bzerr, bz);
    ASSERT_EQ(bzerr, BZ_SEQUENCE_ERROR);

    /* Clean up — the handle is still open, need to close properly */
    /* BZ2_bzReadClose didn't close it due to sequence error, so
     * the stream is still alive. Use bzWriteClose to clean up. */
    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
    unlink(path);
}

/* --- BZ2_bzWrite on a read handle (sequence error) --- */

TEST(bzwrite_on_read_handle) {
    const char *path = "/tmp/libqbz2_test_wor.bz2";
    int bzerr;
    /* Create a valid bz2 file first */
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    BZ2_bzWrite(&bzerr, bz, (void*)"data", 4);
    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    fclose(f);

    /* Open for reading */
    f = fopen(path, "rb");
    bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    ASSERT(bz != NULL);

    /* Try to write — should fail */
    int nw = BZ2_bzwrite(bz, (void*)"bad", 3);
    ASSERT_EQ(nw, -1);

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
    unlink(path);
}

/* --- BZ2_bzRead on a write handle (sequence error) --- */

TEST(bzread_on_write_handle) {
    const char *path = "/tmp/libqbz2_test_row.bz2";
    int bzerr;
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    ASSERT(bz != NULL);

    char buf[10];
    int nr = BZ2_bzread(bz, buf, sizeof(buf));
    ASSERT_EQ(nr, -1);

    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
    unlink(path);
}

TEST_MAIN_BEGIN()
    /* Randomised block decompression */
    RUN(randomised_block_fast_mode);
    RUN(randomised_block_small_mode);
    RUN(randomised_block_streaming_fast);
    RUN(randomised_block_streaming_small);
    RUN(randomised_block_alternating_fast);
    RUN(randomised_block_alternating_small);

    /* bzread/bzwrite edge cases */
    RUN(bzread_after_stream_end);
    RUN(bzwrite_len_zero);
    RUN(bzread_len_zero);

    /* bzerror */
    RUN(bzerror_after_successful_write);
    RUN(bzerror_after_successful_read);

    /* Verbose mode */
    RUN(compress_verbose_mode);
    RUN(decompress_verbose_mode);
    RUN(compress_verbose_high);
    RUN(decompress_verbose_high);

    /* WriteClose variants */
    RUN(write_close64_all_counts);
    RUN(write_close_abandon);

    /* bzdopen */
    RUN(bzdopen_write_read);

    /* ReadGetUnused */
    RUN(read_get_unused_after_stream_end);
    RUN(read_open_with_unused);

    /* Sequence errors */
    RUN(read_close_on_write_handle);
    RUN(bzwrite_on_read_handle);
    RUN(bzread_on_write_handle);
TEST_MAIN_END()
