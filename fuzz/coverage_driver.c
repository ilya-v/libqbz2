/* coverage_driver.c — Run fuzz corpus through library APIs for coverage measurement.
 * Reads files from command line args and exercises compress, decompress,
 * streaming, and buffer-to-buffer APIs.
 * Build with gcov: gcc --coverage -O0 -g -Iinclude coverage_driver.c libqbz2_cov.a -o coverage_driver */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0 || len > 4 * 1024 * 1024) { fclose(f); *size = 0; return NULL; }
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    *size = fread(buf, 1, (size_t)len, f);
    fclose(f);
    return buf;
}

static void exercise_compress(const unsigned char *data, size_t size) {
    for (int bs = 1; bs <= 9; bs += 4) {  /* block sizes 1, 5, 9 */
        for (int wf = 0; wf <= 250; wf += 250) {  /* work factors 0, 250 */
            unsigned int dest_len = (unsigned int)(size + size / 100 + 700);
            if (dest_len < 700) dest_len = 700;
            char *dest = malloc(dest_len);
            if (!dest) continue;
            BZ2_bzBuffToBuffCompress(dest, &dest_len, (char *)data,
                                     (unsigned int)size, bs, 0, wf);
            free(dest);
        }
    }
}

static void exercise_decompress(const unsigned char *data, size_t size) {
    for (int small = 0; small <= 1; small++) {
        unsigned int dest_len = 4 * 1024 * 1024;
        char *dest = malloc(dest_len);
        if (!dest) continue;
        BZ2_bzBuffToBuffDecompress(dest, &dest_len, (char *)data,
                                   (unsigned int)size, small, 0);
        free(dest);
    }
}

static void exercise_streaming_compress(const unsigned char *data, size_t size) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzCompressInit(&strm, 5, 0, 0) != BZ_OK) return;

    size_t out_cap = size + size / 100 + 700;
    if (out_cap < 700) out_cap = 700;
    char *out = malloc(out_cap);
    if (!out) { BZ2_bzCompressEnd(&strm); return; }

    strm.next_in = (char *)data;
    strm.avail_in = (unsigned int)size;
    strm.next_out = out;
    strm.avail_out = (unsigned int)out_cap;

    int action = (size == 0) ? BZ_FINISH : BZ_RUN;
    int ret;
    do {
        if (strm.avail_in == 0 && action == BZ_RUN) action = BZ_FINISH;
        ret = BZ2_bzCompress(&strm, action);
        if (ret == BZ_STREAM_END) break;
        if (ret != BZ_RUN_OK && ret != BZ_FINISH_OK) break;
    } while (1);

    BZ2_bzCompressEnd(&strm);
    free(out);
}

static void exercise_streaming_decompress(const unsigned char *data, size_t size) {
    for (int small = 0; small <= 1; small++) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        if (BZ2_bzDecompressInit(&strm, 0, small) != BZ_OK) continue;

        unsigned int out_cap = 4 * 1024 * 1024;
        char *out = malloc(out_cap);
        if (!out) { BZ2_bzDecompressEnd(&strm); continue; }

        strm.next_in = (char *)data;
        strm.avail_in = (unsigned int)size;
        strm.next_out = out;
        strm.avail_out = out_cap;

        int ret;
        do {
            ret = BZ2_bzDecompress(&strm);
            if (ret == BZ_STREAM_END || ret != BZ_OK) break;
        } while (strm.avail_in > 0 || strm.avail_out == 0);

        BZ2_bzDecompressEnd(&strm);
        free(out);
    }
}

static void exercise_fileio_write_read(const unsigned char *data, size_t size) {
    /* Write compressed data via FILE* API, then read it back */
    for (int bs = 1; bs <= 9; bs += 4) {
        char tmppath[256];
        snprintf(tmppath, sizeof(tmppath), "/tmp/cov_bz2_test_%d.bz2", bs);

        /* Write */
        FILE *fw = fopen(tmppath, "wb");
        if (!fw) continue;
        int bzerr;
        BZFILE *bzfw = BZ2_bzWriteOpen(&bzerr, fw, bs, 0, 0);
        if (bzfw && bzerr == BZ_OK) {
            BZ2_bzWrite(&bzerr, bzfw, (void *)data, (int)size);
            unsigned int in_lo, in_hi, out_lo, out_hi;
            BZ2_bzWriteClose64(&bzerr, bzfw, 0, &in_lo, &in_hi, &out_lo, &out_hi);
        } else if (bzfw) {
            BZ2_bzWriteClose(&bzerr, bzfw, 1, NULL, NULL);
        }
        fclose(fw);

        /* Read back */
        FILE *fr = fopen(tmppath, "rb");
        if (!fr) continue;
        BZFILE *bzfr = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
        if (bzfr && bzerr == BZ_OK) {
            char readbuf[8192];
            while (bzerr == BZ_OK) {
                int nread = BZ2_bzRead(&bzerr, bzfr, readbuf, sizeof(readbuf));
                (void)nread;
                if (bzerr == BZ_STREAM_END) break;
            }
            void *unused;
            int nUnused;
            BZ2_bzReadGetUnused(&bzerr, bzfr, &unused, &nUnused);
            BZ2_bzReadClose(&bzerr, bzfr);
        } else if (bzfr) {
            BZ2_bzReadClose(&bzerr, bzfr);
        }
        fclose(fr);
        remove(tmppath);
    }
}

static void exercise_bzopen(const unsigned char *data, size_t size) {
    /* Test bzopen/bzwrite/bzclose and bzopen/bzread/bzclose */
    const char *tmppath = "/tmp/cov_bzopen_test.bz2";

    BZFILE *bz = BZ2_bzopen(tmppath, "wb9");
    if (bz) {
        BZ2_bzwrite(bz, (void *)data, (int)(size > 100000 ? 100000 : size));
        BZ2_bzclose(bz);
    }

    bz = BZ2_bzopen(tmppath, "rb");
    if (bz) {
        char buf[8192];
        int n;
        do {
            n = BZ2_bzread(bz, buf, sizeof(buf));
        } while (n > 0);
        int errnum;
        const char *errmsg = BZ2_bzerror(bz, &errnum);
        (void)errmsg;
        BZ2_bzclose(bz);
    }
    remove(tmppath);

    /* Test error paths — close if any unexpectedly succeed */
    bz = BZ2_bzopen(NULL, "rb");
    if (bz) BZ2_bzclose(bz);
    bz = BZ2_bzopen(tmppath, NULL);
    if (bz) BZ2_bzclose(bz);
    bz = BZ2_bzopen(tmppath, "xyz");  /* invalid mode */
    if (bz) BZ2_bzclose(bz);
    BZ2_bzflush(NULL);

    /* Test BZ2_bzWriteClose (non-64 variant) */
    FILE *fw = fopen(tmppath, "wb");
    if (fw) {
        int bzerr;
        BZFILE *bzfw = BZ2_bzWriteOpen(&bzerr, fw, 5, 0, 0);
        if (bzfw) {
            BZ2_bzWrite(&bzerr, bzfw, (void *)"hello", 5);
            unsigned int nbin, nbout;
            BZ2_bzWriteClose(&bzerr, bzfw, 0, &nbin, &nbout);
        }
        fclose(fw);
        remove(tmppath);
    }
}

static void exercise_param_errors(void) {
    /* Parameter validation paths — clean up after any successful init */
    bz_stream strm;
    int ret;

    BZ2_bzCompressInit(NULL, 5, 0, 0);

    memset(&strm, 0, sizeof(strm));
    ret = BZ2_bzCompressInit(&strm, 0, 0, 0);   /* invalid blockSize */
    if (ret == BZ_OK) BZ2_bzCompressEnd(&strm);

    memset(&strm, 0, sizeof(strm));
    ret = BZ2_bzCompressInit(&strm, 10, 0, 0);  /* invalid blockSize */
    if (ret == BZ_OK) BZ2_bzCompressEnd(&strm);

    memset(&strm, 0, sizeof(strm));
    ret = BZ2_bzCompressInit(&strm, 5, -1, 0);  /* invalid verbosity */
    if (ret == BZ_OK) BZ2_bzCompressEnd(&strm);

    memset(&strm, 0, sizeof(strm));
    ret = BZ2_bzCompressInit(&strm, 5, 0, -1);  /* invalid workFactor */
    if (ret == BZ_OK) BZ2_bzCompressEnd(&strm);

    BZ2_bzDecompressInit(NULL, 0, 0);

    memset(&strm, 0, sizeof(strm));
    ret = BZ2_bzDecompressInit(&strm, -1, 0);   /* invalid verbosity */
    if (ret == BZ_OK) BZ2_bzDecompressEnd(&strm);

    memset(&strm, 0, sizeof(strm));
    ret = BZ2_bzDecompressInit(&strm, 0, 2);    /* invalid small */
    if (ret == BZ_OK) BZ2_bzDecompressEnd(&strm);

    BZ2_bzCompress(NULL, BZ_RUN);
    BZ2_bzDecompress(NULL);
    BZ2_bzCompressEnd(NULL);
    BZ2_bzDecompressEnd(NULL);
    BZ2_bzBuffToBuffCompress(NULL, NULL, NULL, 0, 5, 0, 0);
    BZ2_bzBuffToBuffDecompress(NULL, NULL, NULL, 0, 0, 0);
    BZ2_bzlibVersion();
}

int main(int argc, char **argv) {
    exercise_param_errors();

    for (int i = 1; i < argc; i++) {
        size_t size;
        unsigned char *data = read_file(argv[i], &size);
        if (!data) continue;

        /* Try as uncompressed input (compress it) */
        exercise_compress(data, size);
        exercise_streaming_compress(data, size);

        /* Try as compressed input (decompress it) */
        exercise_decompress(data, size);
        exercise_streaming_decompress(data, size);

        /* Exercise FILE* API */
        exercise_fileio_write_read(data, size);
        exercise_bzopen(data, size);

        free(data);
    }
    return 0;
}
