/* coverage_malloc_inject_driver.c — Coverage driver with inline malloc/stdio
 * interception to trigger FILE* API error paths.
 *
 * Build: gcc --coverage -O0 -g -I../include coverage_malloc_inject_driver.c \
 *            libqbz2_cov.a -o coverage_malloc_inject -lm -ldl
 *
 * The interceptors override libc malloc/fwrite/fread/ferror via dlsym(RTLD_NEXT)
 * and are controlled by inject_activate()/inject_deactivate() with per-test
 * configuration of which call number to fail.
 *
 * This exercises:
 * - BZ2_bzWriteOpen malloc failure (line 956)
 * - BZ2_bzWriteOpen BZ2_bzCompressInit failure (line 971) via OOM in init
 * - BZ2_bzReadOpen malloc failure (line 1137)
 * - BZ2_bzReadOpen BZ2_bzDecompressInit failure (line 1157) via OOM in init
 * - BZ2_bzWrite fwrite failure (line 1017) via fwrite injection
 * - BZ2_bzWriteClose64 fwrite failure (line 1081)
 * - BZ2_bzRead ferror on handle (line 1215-1216) via ferror injection
 * - BZ2_bzRead ferror after fread (line 1221-1222) via ferror injection
 * - BZ2_bzBuffToBuffDecompress internal failure (line 1324)
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bzlib.h"

/* ========== Inline interceptors ========== */

static int inject_active = 0;
static int inject_malloc_fail_at = 0;
static int inject_fwrite_fail_at = 0;
static int inject_ferror_immediate = 0; /* ferror always returns 1 */
static int inject_ferror_after_fread = 0; /* ferror returns 1 after N fread calls */
static int inject_malloc_count = 0;
static int inject_fwrite_count = 0;
static int inject_fread_count = 0;

static void inject_activate_malloc(int fail_at) {
    inject_malloc_fail_at = fail_at;
    inject_fwrite_fail_at = 0;
    inject_ferror_immediate = 0;
    inject_ferror_after_fread = 0;
    inject_malloc_count = 0;
    inject_fwrite_count = 0;
    inject_fread_count = 0;
    inject_active = 1;
}

static void inject_activate_fwrite(int fail_at) {
    inject_malloc_fail_at = 0;
    inject_fwrite_fail_at = fail_at;
    inject_ferror_immediate = 0;
    inject_ferror_after_fread = 0;
    inject_malloc_count = 0;
    inject_fwrite_count = 0;
    inject_fread_count = 0;
    inject_active = 1;
}

static void inject_activate_ferror(int immediate, int after_fread) {
    inject_malloc_fail_at = 0;
    inject_fwrite_fail_at = 0;
    inject_ferror_immediate = immediate;
    inject_ferror_after_fread = after_fread;
    inject_malloc_count = 0;
    inject_fwrite_count = 0;
    inject_fread_count = 0;
    inject_active = 1;
}

static void inject_deactivate(void) {
    inject_active = 0;
}

void *malloc(size_t size) {
    static void *(*real_malloc)(size_t) = NULL;
    if (!real_malloc) real_malloc = dlsym(RTLD_NEXT, "malloc");

    if (inject_active && inject_malloc_fail_at > 0) {
        inject_malloc_count++;
        if (inject_malloc_count == inject_malloc_fail_at) {
            return NULL;
        }
    }
    return real_malloc(size);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    static size_t (*real_fwrite)(const void *, size_t, size_t, FILE *) = NULL;
    if (!real_fwrite) real_fwrite = dlsym(RTLD_NEXT, "fwrite");

    if (inject_active && inject_fwrite_fail_at > 0) {
        inject_fwrite_count++;
        if (inject_fwrite_count >= inject_fwrite_fail_at) {
            return 0;  /* Simulate fwrite failure */
        }
    }
    return real_fwrite(ptr, size, nmemb, stream);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    static size_t (*real_fread)(void *, size_t, size_t, FILE *) = NULL;
    if (!real_fread) real_fread = dlsym(RTLD_NEXT, "fread");

    if (inject_active) {
        inject_fread_count++;
    }
    return real_fread(ptr, size, nmemb, stream);
}

int ferror(FILE *stream) {
    static int (*real_ferror)(FILE *) = NULL;
    if (!real_ferror) real_ferror = dlsym(RTLD_NEXT, "ferror");

    if (inject_active) {
        /* Immediate ferror: always returns 1 */
        if (inject_ferror_immediate)
            return 1;
        /* Delayed ferror: returns 1 after N fread calls */
        if (inject_ferror_after_fread > 0 && inject_fread_count >= inject_ferror_after_fread)
            return 1;
    }
    return real_ferror(stream);
}

/* ========== Test data ========== */

static const char test_data[] =
    "AAABBBCCCDDDEEEFFFGGGHHHIIIJJJKKKLLLMMMNNN"
    "The quick brown fox jumps over the lazy dog.\n";

static char bz2buf[8192];
static unsigned int bz2len;

static int prepare_bz2(void) {
    bz2len = sizeof(bz2buf);
    return BZ2_bzBuffToBuffCompress(bz2buf, &bz2len,
                                    (char *)test_data, sizeof(test_data) - 1,
                                    1, 0, 0);
}

/* ========== Tests ========== */

/* Test BZ2_bzWriteOpen with malloc failure at allocation 1 (bzFile struct) */
static void test_writeopen_malloc_fail(void) {
    int bzerr;
    FILE *f = tmpfile();
    if (!f) return;

    inject_activate_malloc(1);  /* malloc fails at call 1 */
    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    if (bz) BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    inject_deactivate();

    fclose(f);
}

/* Test BZ2_bzWriteOpen with BZ2_bzCompressInit failure via OOM at malloc 2+ */
static void test_writeopen_init_fail(void) {
    for (int n = 2; n <= 6; n++) {
        int bzerr;
        FILE *f = tmpfile();
        if (!f) continue;
        inject_activate_malloc(n);
        BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
        if (bz) BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
        inject_deactivate();
        fclose(f);
    }
}

/* Test BZ2_bzReadOpen with malloc failure at allocation 1 */
static void test_readopen_malloc_fail(void) {
    int bzerr;
    FILE *f = tmpfile();
    if (!f) return;
    fwrite(bz2buf, 1, bz2len, f);
    rewind(f);

    inject_activate_malloc(1);
    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    if (bz) BZ2_bzReadClose(&bzerr, bz);
    inject_deactivate();

    fclose(f);
}

/* Test BZ2_bzReadOpen with BZ2_bzDecompressInit failure via OOM at malloc 2+ */
static void test_readopen_init_fail(void) {
    for (int n = 2; n <= 5; n++) {
        int bzerr;
        FILE *f = tmpfile();
        if (!f) continue;
        fwrite(bz2buf, 1, bz2len, f);
        rewind(f);
        inject_activate_malloc(n);
        BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
        if (bz) BZ2_bzReadClose(&bzerr, bz);
        inject_deactivate();
        fclose(f);
    }
}

/* Test BZ2_bzWrite fwrite failure — triggers line 1017.
 * Uses diverse data (not all-identical) to force compressed output during BZ_RUN. */
static void test_bzwrite_fwrite_fail(void) {
    int bzerr;
    FILE *f = tmpfile();
    if (!f) return;

    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    if (!bz) { fclose(f); return; }

    /* Activate fwrite injection — fail on first fwrite inside BZ2_bzWrite */
    inject_activate_fwrite(1);

    /* Use diverse data to ensure compressor produces output during BZ_RUN */
    char bigbuf[200000];
    for (int i = 0; i < (int)sizeof(bigbuf); i++)
        bigbuf[i] = (char)(i * 37 + i / 131);
    BZ2_bzWrite(&bzerr, bz, bigbuf, sizeof(bigbuf));

    inject_deactivate();

    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
}

/* Test BZ2_bzWriteClose64 fwrite failure — triggers line 1081 */
static void test_writeclose_fwrite_fail(void) {
    int bzerr;
    FILE *f = tmpfile();
    if (!f) return;

    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    if (!bz) { fclose(f); return; }

    /* Write some data first */
    BZ2_bzWrite(&bzerr, bz, (void *)test_data, sizeof(test_data) - 1);

    /* Activate fwrite injection for the close flush */
    inject_activate_fwrite(1);

    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);

    inject_deactivate();
    fclose(f);
}

/* Test BZ2_bzWrite with ferror on handle — triggers line 997.
 * ferror(bzf->handle) is checked before any I/O. */
static void test_bzwrite_ferror(void) {
    int bzerr;
    FILE *f = tmpfile();
    if (!f) return;

    BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    if (!bz) { fclose(f); return; }

    /* Immediate ferror: ferror returns 1 on any check */
    inject_activate_ferror(1, 0);
    BZ2_bzWrite(&bzerr, bz, (void *)test_data, sizeof(test_data) - 1);
    inject_deactivate();

    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    fclose(f);
}

/* Test BZ2_bzRead with ferror before fread — triggers lines 1215-1216 */
static void test_bzread_ferror_immediate(void) {
    int bzerr;
    char buf[4096];
    FILE *f = tmpfile();
    if (!f) return;
    fwrite(bz2buf, 1, bz2len, f);
    rewind(f);

    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    if (!bz) { fclose(f); return; }

    /* Immediate ferror: triggers line 1215 check before fread */
    inject_activate_ferror(1, 0);
    BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
    inject_deactivate();

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
}

/* Test BZ2_bzRead with ferror after fread — triggers line 1222 */
static void test_bzread_ferror_after_fread(void) {
    int bzerr;
    char buf[4096];
    FILE *f = tmpfile();
    if (!f) return;
    fwrite(bz2buf, 1, bz2len, f);
    rewind(f);

    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
    if (!bz) { fclose(f); return; }

    /* Delayed ferror: ferror returns 1 after 1 fread call */
    inject_activate_ferror(0, 1);
    BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
    inject_deactivate();

    BZ2_bzReadClose(&bzerr, bz);
    fclose(f);
}

/* Test BZ2_bzBuffToBuffDecompress internal failure — line 1324 */
static void test_b2b_decompress_internal_fail(void) {
    char dest[4096];
    unsigned int dlen;

    /* Corrupt the bz2 data to cause decompression error */
    char corrupt[8192];
    memcpy(corrupt, bz2buf, bz2len);
    /* Corrupt bytes after the header to cause DATA_ERROR */
    if (bz2len > 10) {
        for (int i = 8; i < 12 && i < (int)bz2len; i++)
            corrupt[i] ^= 0xFF;
    }

    dlen = sizeof(dest);
    BZ2_bzBuffToBuffDecompress(dest, &dlen, corrupt, bz2len, 0, 0);
}

/* Test BZ2_bzBuffToBuffCompress OOM — malloc failure during B2B compress */
static void test_b2b_compress_oom(void) {
    char dest[8192];
    unsigned int dlen;

    for (int n = 1; n <= 6; n++) {
        dlen = sizeof(dest);
        inject_activate_malloc(n);
        BZ2_bzBuffToBuffCompress(dest, &dlen,
                                 (char *)test_data, sizeof(test_data) - 1,
                                 1, 0, 0);
        inject_deactivate();
    }
}

/* Test BZ2_bzBuffToBuffDecompress OOM — malloc failure during B2B decompress */
static void test_b2b_decompress_oom(void) {
    char dest[4096];
    unsigned int dlen;

    for (int n = 1; n <= 5; n++) {
        dlen = sizeof(dest);
        inject_activate_malloc(n);
        BZ2_bzBuffToBuffDecompress(dest, &dlen, bz2buf, bz2len, 0, 0);
        inject_deactivate();
    }
}

int main(void) {
    if (prepare_bz2() != BZ_OK) {
        fprintf(stderr, "Failed to prepare bz2 data\n");
        return 1;
    }

    printf("test_writeopen_malloc_fail...\n");
    test_writeopen_malloc_fail();

    printf("test_writeopen_init_fail...\n");
    test_writeopen_init_fail();

    printf("test_readopen_malloc_fail...\n");
    test_readopen_malloc_fail();

    printf("test_readopen_init_fail...\n");
    test_readopen_init_fail();

    printf("test_bzwrite_fwrite_fail...\n");
    test_bzwrite_fwrite_fail();

    printf("test_writeclose_fwrite_fail...\n");
    test_writeclose_fwrite_fail();

    printf("test_bzwrite_ferror...\n");
    test_bzwrite_ferror();

    printf("test_bzread_ferror_immediate...\n");
    test_bzread_ferror_immediate();

    printf("test_bzread_ferror_after_fread...\n");
    test_bzread_ferror_after_fread();

    printf("test_b2b_decompress_internal_fail...\n");
    test_b2b_decompress_internal_fail();

    printf("test_b2b_compress_oom...\n");
    test_b2b_compress_oom();

    printf("test_b2b_decompress_oom...\n");
    test_b2b_decompress_oom();

    printf("Done.\n");
    return 0;
}
