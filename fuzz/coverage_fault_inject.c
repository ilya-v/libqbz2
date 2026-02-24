/* coverage_fault_inject.c — Fault injection coverage driver for FILE* API.
 *
 * Intercepts malloc, fwrite, fread, ferror via LD_PRELOAD to inject failures
 * at specific call sites. This covers the "never executed" error paths in
 * bzlib.c FILE* functions (BZ2_bzWriteOpen, BZ2_bzWrite, BZ2_bzWriteClose64,
 * BZ2_bzReadOpen, BZ2_bzRead, BZ2_bzReadClose, BZ2_bzReadGetUnused, bzopen).
 *
 * Build (shared interceptor):
 *   gcc -shared -fPIC -o fault_inject.so coverage_fault_inject_preload.c -ldl
 *
 * Build (driver):
 *   gcc --coverage -O0 -g -Iinclude coverage_fault_inject.c libqbz2_cov.a -o coverage_fault_inject
 *
 * Run:
 *   LD_PRELOAD=./fault_inject.so ./coverage_fault_inject
 *
 * OR (simpler approach without LD_PRELOAD):
 *   Use pipe()/close() tricks and tmpfile() tricks to trigger ferror/fwrite failures.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "bzlib.h"

static const char test_data[] =
    "The quick brown fox jumps over the lazy dog. "
    "Pack my box with five dozen liquor jugs. "
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n";

static char bz2buf[8192];
static unsigned int bz2len;

static int prepare_bz2(void) {
    bz2len = sizeof(bz2buf);
    return BZ2_bzBuffToBuffCompress(bz2buf, &bz2len,
                                    (char *)test_data, sizeof(test_data) - 1,
                                    1, 0, 0);
}

/* ---------- BZ2_bzBuffToBuffCompress parameter coverage ---------- */
static void exercise_b2b_compress_params(void) {
    char dest[4096];
    unsigned int dlen;

    /* NULL dest */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffCompress(NULL, &dlen, (char *)test_data, 10, 1, 0, 0);

    /* NULL destLen */
    BZ2_bzBuffToBuffCompress(dest, NULL, (char *)test_data, 10, 1, 0, 0);

    /* NULL source */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffCompress(dest, &dlen, NULL, 10, 1, 0, 0);

    /* blockSize100k out of range */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffCompress(dest, &dlen, (char *)test_data, 10, 0, 0, 0);
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffCompress(dest, &dlen, (char *)test_data, 10, 10, 0, 0);

    /* verbosity out of range */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffCompress(dest, &dlen, (char *)test_data, 10, 1, -1, 0);
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffCompress(dest, &dlen, (char *)test_data, 10, 1, 5, 0);

    /* workFactor out of range */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffCompress(dest, &dlen, (char *)test_data, 10, 1, 0, -1);
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffCompress(dest, &dlen, (char *)test_data, 10, 1, 0, 251);

    /* destLen too small (BZ_OUTBUFF_FULL) */
    dlen = 1;
    BZ2_bzBuffToBuffCompress(dest, &dlen, (char *)test_data, sizeof(test_data) - 1, 1, 0, 0);
}

/* ---------- BZ2_bzBuffToBuffDecompress parameter coverage ---------- */
static void exercise_b2b_decompress_params(void) {
    char dest[4096];
    unsigned int dlen;

    /* NULL dest */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffDecompress(NULL, &dlen, bz2buf, bz2len, 0, 0);

    /* NULL destLen */
    BZ2_bzBuffToBuffDecompress(dest, NULL, bz2buf, bz2len, 0, 0);

    /* NULL source */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffDecompress(dest, &dlen, NULL, bz2len, 0, 0);

    /* small out of range */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffDecompress(dest, &dlen, bz2buf, bz2len, 2, 0);

    /* verbosity out of range */
    dlen = sizeof(dest);
    BZ2_bzBuffToBuffDecompress(dest, &dlen, bz2buf, bz2len, 0, 5);

    /* destLen too small */
    dlen = 1;
    BZ2_bzBuffToBuffDecompress(dest, &dlen, bz2buf, bz2len, 0, 0);
}

/* ---------- FILE* write API with pipe-based error injection ---------- */
static void exercise_write_io_errors(void) {
    int bzerr;
    BZFILE *bz;

    /* Write to a broken pipe — triggers fwrite failure AND ferror.
     * Signal SIGPIPE to SIG_IGN to prevent process termination. */
    signal(SIGPIPE, SIG_IGN);

    /* 1. BZ2_bzWrite with fwrite failure in the compress loop.
     * Need to write enough data that the compressed output overflows
     * the pipe buffer (~64KB). Use block size 1 (100KB) to ensure
     * compressed blocks get flushed to fwrite frequently. We also
     * disable stdio buffering to make fwrite errors immediate. */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            close(pipefd[0]); /* Close read end to make writes fail */
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                /* Disable stdio buffering so fwrite errors are immediate */
                setvbuf(f, NULL, _IONBF, 0);
                bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
                if (bz) {
                    /* Write lots of data in a loop. With unbuffered I/O
                     * and blockSize=1, the internal fwrite at line 1014-1016
                     * should fail once the pipe buffer fills up. */
                    char bigbuf[131072];
                    memset(bigbuf, 'X', sizeof(bigbuf));
                    for (int i = 0; i < 50; i++) {
                        BZ2_bzWrite(&bzerr, bz, bigbuf, sizeof(bigbuf));
                        if (bzerr != BZ_OK) break;
                    }
                    /* If the first writes succeeded and set ferror,
                     * try another write to hit the ferror check at line 996 */
                    if (bzerr == BZ_IO_ERROR) {
                        /* bz handle still exists, write again to hit line 996 */
                        BZ2_bzWrite(&bzerr, bz, bigbuf, 100);
                    }
                    /* WriteClose with abandon=1 */
                    BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
                }
                fclose(f);
            } else {
                close(pipefd[1]);
            }
        }
    }

    /* 1b. Same as above but with NULL bzerror to cover both bzerror branches */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            close(pipefd[0]);
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                setvbuf(f, NULL, _IONBF, 0);
                bz = BZ2_bzWriteOpen(NULL, f, 1, 0, 0);
                if (bz) {
                    char bigbuf[131072];
                    memset(bigbuf, 'X', sizeof(bigbuf));
                    for (int i = 0; i < 50; i++) {
                        BZ2_bzWrite(NULL, bz, bigbuf, sizeof(bigbuf));
                    }
                    BZ2_bzWrite(NULL, bz, bigbuf, 100);
                    BZ2_bzWriteClose(NULL, bz, 1, NULL, NULL);
                }
                fclose(f);
            } else {
                close(pipefd[1]);
            }
        }
    }

    /* 2. BZ2_bzWriteClose with fwrite failure during finish loop.
     * Write enough data to fill a block, close the read end BEFORE
     * calling WriteClose. The finish loop calls BZ2_bzCompress(BZ_FINISH)
     * which produces output that gets fwrite'd — this fwrite should fail.
     * Use unbuffered I/O to make it immediate. */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                setvbuf(f, NULL, _IONBF, 0);
                bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
                if (bz) {
                    /* Write enough data to create a full block.
                     * blockSize=1 means 100KB. Write 100KB+. */
                    char bigbuf[110000];
                    memset(bigbuf, 'A', sizeof(bigbuf));
                    BZ2_bzWrite(&bzerr, bz, bigbuf, sizeof(bigbuf));
                    /* Now close read end so fwrite fails during finish */
                    close(pipefd[0]);
                    pipefd[0] = -1;
                    /* WriteClose calls BZ2_bzCompress(BZ_FINISH) which
                     * produces compressed data, then fwrite -> IO_ERROR */
                    unsigned int in_lo, out_lo;
                    BZ2_bzWriteClose64(&bzerr, bz, 0,
                                       &in_lo, NULL, &out_lo, NULL);
                }
                fclose(f);
            } else {
                close(pipefd[1]);
            }
            if (pipefd[0] >= 0) close(pipefd[0]);
        }
    }

    /* 2b. Same but with NULL bzerror */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                setvbuf(f, NULL, _IONBF, 0);
                bz = BZ2_bzWriteOpen(NULL, f, 1, 0, 0);
                if (bz) {
                    char bigbuf[110000];
                    memset(bigbuf, 'A', sizeof(bigbuf));
                    BZ2_bzWrite(NULL, bz, bigbuf, sizeof(bigbuf));
                    close(pipefd[0]);
                    pipefd[0] = -1;
                    BZ2_bzWriteClose64(NULL, bz, 0, NULL, NULL, NULL, NULL);
                }
                fclose(f);
            } else {
                close(pipefd[1]);
            }
            if (pipefd[0] >= 0) close(pipefd[0]);
        }
    }

    /* 2c. WriteClose with ferror BEFORE the finish loop (line 1060-1061).
     * The finish loop is skipped when ferror is set at entry.
     * Already triggers at line 1060. Also test with fflush failing. */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
                if (bz) {
                    BZ2_bzWrite(&bzerr, bz, (void *)test_data,
                                sizeof(test_data) - 1);
                    /* Close read end, then do a direct fwrite to set ferror */
                    close(pipefd[0]);
                    pipefd[0] = -1;
                    char junk[65536];
                    memset(junk, 0, sizeof(junk));
                    for (int i = 0; i < 10; i++)
                        fwrite(junk, 1, sizeof(junk), f);
                    /* Now ferror(f) should be true */
                    /* WriteClose should see ferror at line 1060 */
                    BZ2_bzWriteClose64(&bzerr, bz, 0, NULL, NULL, NULL, NULL);
                }
                fclose(f);
            } else {
                close(pipefd[1]);
            }
            if (pipefd[0] >= 0) close(pipefd[0]);
        }
    }

    /* 3. BZ2_bzWriteClose with abandon=1 and lastErr != BZ_OK */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            close(pipefd[0]);
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
                if (bz) {
                    char bigbuf[131072];
                    memset(bigbuf, 'Y', sizeof(bigbuf));
                    for (int i = 0; i < 20; i++) {
                        BZ2_bzWrite(&bzerr, bz, bigbuf, sizeof(bigbuf));
                        if (bzerr != BZ_OK) break;
                    }
                    /* Now close with abandon=0 — should skip flush
                     * because lastErr != BZ_OK */
                    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
                }
                fclose(f);
            } else {
                close(pipefd[1]);
            }
        }
    }

    /* 4. BZ2_bzWriteOpen with ferror already set */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            close(pipefd[0]);
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                /* Force ferror by writing to broken pipe */
                char junk[65536];
                memset(junk, 'Z', sizeof(junk));
                for (int i = 0; i < 10; i++)
                    fwrite(junk, 1, sizeof(junk), f);
                /* Now ferror(f) should be true */
                bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
                /* bz should be NULL (BZ_IO_ERROR) */
                if (bz) BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
                fclose(f);
            } else {
                close(pipefd[1]);
            }
        }
    }

    signal(SIGPIPE, SIG_DFL);
}

/* ---------- FILE* read API with error injection ---------- */
static void exercise_read_io_errors(void) {
    int bzerr;
    BZFILE *bz;
    char buf[4096];

    /* 1. BZ2_bzRead hitting ferror mid-stream */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            /* Write partial bz2 data then close write end */
            write(pipefd[1], bz2buf, bz2len / 4);
            close(pipefd[1]);

            FILE *f = fdopen(pipefd[0], "rb");
            if (f) {
                bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
                if (bz) {
                    /* Read will fail with unexpected EOF */
                    BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                    /* Try ReadGetUnused on non-STREAM_END state */
                    void *unused;
                    int nUnused;
                    BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
                    BZ2_bzReadClose(&bzerr, bz);
                }
                fclose(f);
            } else {
                close(pipefd[0]);
            }
        }
    }

    /* 2. BZ2_bzReadOpen with ferror set */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            close(pipefd[1]); /* EOF immediately */
            FILE *f = fdopen(pipefd[0], "rb");
            if (f) {
                /* Try to set ferror by reading past EOF */
                char junk[16];
                clearerr(f);
                /* Read until EOF to set feof, then try to trigger ferror */
                while (fread(junk, 1, sizeof(junk), f) > 0) ;
                /* feof is set but ferror may not be. Let's try BZ2_bzReadOpen */
                bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
                if (bz) {
                    BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                    BZ2_bzReadClose(&bzerr, bz);
                }
                fclose(f);
            } else {
                close(pipefd[0]);
            }
        }
    }

    /* 3. BZ2_bzRead with corrupted bz2 data (decompressor error) */
    {
        FILE *f = tmpfile();
        if (f) {
            /* Write valid header then garbage */
            char corrupt[256];
            memcpy(corrupt, bz2buf, bz2len < 20 ? bz2len : 20);
            /* Corrupt some bytes in the middle */
            for (int i = 10; i < 20 && i < (int)bz2len; i++)
                corrupt[i] ^= 0xFF;
            fwrite(corrupt, 1, 20, f);
            rewind(f);

            bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                /* Should get BZ_DATA_ERROR from decompress */
                BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                BZ2_bzReadClose(&bzerr, bz);
            }
            fclose(f);
        }
    }

    /* 4. Full successful read cycle with ReadGetUnused */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            /* Append some trailing bytes */
            fwrite("EXTRA", 1, 5, f);
            rewind(f);

            bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                /* Read all data */
                int total = 0;
                while (1) {
                    int nr = BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                    total += nr;
                    if (bzerr == BZ_STREAM_END) break;
                    if (bzerr != BZ_OK) break;
                }
                /* Now get unused bytes */
                if (bzerr == BZ_STREAM_END) {
                    void *unused;
                    int nUnused;
                    BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
                }
                BZ2_bzReadClose(&bzerr, bz);
            }
            fclose(f);
        }
    }

    /* 5. BZ2_bzRead with small output buffer (exercise avail_out==0 loop) */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);

            bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                char tiny[4];
                int total = 0;
                while (1) {
                    int nr = BZ2_bzRead(&bzerr, bz, tiny, sizeof(tiny));
                    total += nr;
                    if (bzerr == BZ_STREAM_END || bzerr != BZ_OK) break;
                }
                BZ2_bzReadClose(&bzerr, bz);
            }
            fclose(f);
        }
    }

    /* 6. BZ2_bzReadOpen with unused data from previous stream */
    {
        FILE *f = tmpfile();
        if (f) {
            /* Write two concatenated bz2 streams */
            fwrite(bz2buf, 1, bz2len, f);
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);

            /* Read first stream */
            bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                while (1) {
                    int nr = BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                    (void)nr;
                    if (bzerr == BZ_STREAM_END || bzerr != BZ_OK) break;
                }
                void *unused = NULL;
                int nUnused = 0;
                if (bzerr == BZ_STREAM_END) {
                    BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
                }
                /* Save unused data */
                char unused_buf[BZ_MAX_UNUSED];
                int saved_nUnused = 0;
                if (unused && nUnused > 0) {
                    memcpy(unused_buf, unused, nUnused);
                    saved_nUnused = nUnused;
                }
                BZ2_bzReadClose(&bzerr, bz);

                /* Open second stream with unused data */
                bz = BZ2_bzReadOpen(&bzerr, f, 0, 0,
                                    saved_nUnused > 0 ? unused_buf : NULL,
                                    saved_nUnused);
                if (bz) {
                    while (1) {
                        int nr = BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                        (void)nr;
                        if (bzerr == BZ_STREAM_END || bzerr != BZ_OK) break;
                    }
                    BZ2_bzReadClose(&bzerr, bz);
                }
            }
            fclose(f);
        }
    }
}

/* ---------- NULL bzerror through every FILE* function ---------- */
static void exercise_null_bzerror_comprehensive(void) {
    BZFILE *bz;
    char buf[4096];

    /* BZ2_bzWriteOpen with NULL bzerror — valid params */
    {
        FILE *f = tmpfile();
        if (f) {
            bz = BZ2_bzWriteOpen(NULL, f, 1, 0, 0);
            if (bz) {
                /* BZ2_bzWrite with NULL bzerror */
                BZ2_bzWrite(NULL, bz, (void *)test_data, sizeof(test_data) - 1);
                /* BZ2_bzWriteClose64 with NULL bzerror */
                BZ2_bzWriteClose64(NULL, bz, 0, NULL, NULL, NULL, NULL);
            }
            fclose(f);
        }
    }

    /* BZ2_bzWriteOpen with NULL bzerror — invalid params */
    BZ2_bzWriteOpen(NULL, NULL, 1, 0, 0);

    /* BZ2_bzWriteClose with NULL bzerror and NULL b */
    BZ2_bzWriteClose(NULL, NULL, 0, NULL, NULL);

    /* BZ2_bzReadOpen with NULL bzerror — valid params */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            bz = BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 0);
            if (bz) {
                /* BZ2_bzRead with NULL bzerror */
                int nr = BZ2_bzRead(NULL, bz, buf, sizeof(buf));
                (void)nr;
                /* BZ2_bzReadClose with NULL bzerror */
                BZ2_bzReadClose(NULL, bz);
            }
            fclose(f);
        }
    }

    /* BZ2_bzReadOpen with NULL bzerror — invalid params */
    BZ2_bzReadOpen(NULL, NULL, 0, 0, NULL, 0);

    /* BZ2_bzReadClose with NULL bzerror and NULL b */
    BZ2_bzReadClose(NULL, NULL);

    /* BZ2_bzReadGetUnused with NULL bzerror and NULL b */
    {
        void *unused;
        int nUnused;
        BZ2_bzReadGetUnused(NULL, NULL, &unused, &nUnused);
    }

    /* BZ2_bzRead with NULL bzerror — various error states */
    BZ2_bzRead(NULL, NULL, buf, sizeof(buf)); /* NULL b */

    /* BZ2_bzWrite with NULL bzerror — various errors */
    BZ2_bzWrite(NULL, NULL, (void *)test_data, 10); /* NULL b */
}

/* ---------- BZ2_bzWriteClose abandon + error combos ---------- */
static void exercise_writeclose_combos(void) {
    int bzerr;
    BZFILE *bz;

    /* Close with abandon=1, verify it skips compression */
    {
        FILE *f = tmpfile();
        if (f) {
            bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(&bzerr, bz, (void *)test_data,
                            sizeof(test_data) - 1);
                unsigned int in_lo, in_hi, out_lo, out_hi;
                BZ2_bzWriteClose64(&bzerr, bz, 1,
                                   &in_lo, &in_hi, &out_lo, &out_hi);
            }
            fclose(f);
        }
    }

    /* Close with all byte count pointers non-NULL */
    {
        FILE *f = tmpfile();
        if (f) {
            bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(&bzerr, bz, (void *)test_data,
                            sizeof(test_data) - 1);
                unsigned int in_lo, in_hi, out_lo, out_hi;
                BZ2_bzWriteClose64(&bzerr, bz, 0,
                                   &in_lo, &in_hi, &out_lo, &out_hi);
            }
            fclose(f);
        }
    }

    /* Close with all byte count pointers NULL */
    {
        FILE *f = tmpfile();
        if (f) {
            bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(&bzerr, bz, (void *)test_data,
                            sizeof(test_data) - 1);
                BZ2_bzWriteClose64(&bzerr, bz, 0, NULL, NULL, NULL, NULL);
            }
            fclose(f);
        }
    }
}

/* ---------- BZ2_bzclose error path in write mode ---------- */
static void exercise_bzclose_write_error(void) {
    signal(SIGPIPE, SIG_IGN);

    /* Make BZ2_bzWriteClose return an error so bzclose hits its retry path */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                BZFILE *bz = BZ2_bzopen("/dev/null", "w1");
                /* Can't easily trigger the bzclose retry path without
                 * controlling the fd. Use bzdopen instead. */
                (void)bz;
                fclose(f);
            } else {
                close(pipefd[1]);
            }
            close(pipefd[0]);
        }
    }

    /* Use bzdopen to write, then make the close fail */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            /* Write via bzdopen to a pipe */
            BZFILE *bz = BZ2_bzdopen(pipefd[1], "w1");
            if (bz) {
                /* Write lots of data */
                char bigbuf[131072];
                memset(bigbuf, 'A', sizeof(bigbuf));
                /* Close read end to cause write errors */
                close(pipefd[0]);
                pipefd[0] = -1;
                for (int i = 0; i < 20; i++)
                    BZ2_bzwrite(bz, bigbuf, sizeof(bigbuf));
                /* bzclose should hit the error retry path */
                BZ2_bzclose(bz);
            } else {
                close(pipefd[1]);
            }
            if (pipefd[0] >= 0) close(pipefd[0]);
        }
    }

    signal(SIGPIPE, SIG_DFL);
}

/* ---------- bzopen edge cases ---------- */
static void exercise_bzopen_edges(void) {
    BZFILE *bz;

    /* Invalid mode strings */
    bz = BZ2_bzopen("/dev/null", "x"); /* invalid mode */
    if (bz) BZ2_bzclose(bz);

    bz = BZ2_bzopen("/dev/null", ""); /* empty mode */
    if (bz) BZ2_bzclose(bz);

    /* Block size edge cases in mode string */
    bz = BZ2_bzopen("/dev/null", "w0"); /* block size 0 — clamped to 1 */
    if (bz) {
        BZ2_bzwrite(bz, (void *)test_data, 10);
        BZ2_bzclose(bz);
    }

    /* Write mode with 's' (small) — ignored for write */
    {
        char tmppath[] = "/tmp/cov_bzopen_XXXXXX";
        int fd = mkstemp(tmppath);
        if (fd >= 0) {
            close(fd);
            bz = BZ2_bzopen(tmppath, "ws");
            if (bz) {
                BZ2_bzwrite(bz, (void *)test_data, sizeof(test_data) - 1);
                BZ2_bzclose(bz);
            }
            unlink(tmppath);
        }
    }

    /* bzdopen with -1 fd */
    bz = BZ2_bzdopen(-1, "r");
    if (bz) BZ2_bzclose(bz);

    bz = BZ2_bzdopen(-1, "w");
    if (bz) BZ2_bzclose(bz);
}

/* ---------- BZ2_bzRead with small reads to exercise output loop ---------- */
static void exercise_read_small_chunks(void) {
    int bzerr;
    FILE *f = tmpfile();
    if (!f) return;

    fwrite(bz2buf, 1, bz2len, f);
    rewind(f);

    BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 1, NULL, 0); /* small=1 */
    if (bz) {
        char tiny[1];
        int total = 0;
        while (1) {
            int nr = BZ2_bzRead(&bzerr, bz, tiny, 1);
            total += nr;
            if (bzerr == BZ_STREAM_END || bzerr != BZ_OK) break;
        }
        if (bzerr == BZ_STREAM_END) {
            void *unused;
            int nUnused;
            BZ2_bzReadGetUnused(&bzerr, bz, &unused, &nUnused);
        }
        BZ2_bzReadClose(&bzerr, bz);
    }
    fclose(f);
}

/* ---------- Sequence errors on wrong handle type ---------- */
static void exercise_sequence_errors(void) {
    int bzerr;
    char buf[4096];

    /* Write handle passed to read functions */
    {
        FILE *f = tmpfile();
        if (f) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
            if (bz) {
                /* Try read operations on write handle */
                BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                BZ2_bzReadClose(&bzerr, bz);
                /* Handle is freed by ReadClose, don't double-free */
            }
            fclose(f);
        }
    }

    /* Read handle passed to write functions */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                /* Try write operations on read handle */
                BZ2_bzWrite(&bzerr, bz, (void *)test_data, 10);
                BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
                /* Handle freed by WriteClose, don't double-free */
            }
            fclose(f);
        }
    }

    /* ReadGetUnused with NULL unused/nUnused pointers */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                int nr = BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                (void)nr;
                if (bzerr == BZ_STREAM_END) {
                    /* NULL unused pointer */
                    int nUnused;
                    BZ2_bzReadGetUnused(&bzerr, bz, NULL, &nUnused);
                    /* NULL nUnused pointer */
                    void *unused;
                    BZ2_bzReadGetUnused(&bzerr, bz, &unused, NULL);
                }
                BZ2_bzReadClose(&bzerr, bz);
            }
            fclose(f);
        }
    }

    /* BZ2_bzWrite with len=0 and NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(NULL, bz, (void *)test_data, 0);
                BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
            }
            fclose(f);
        }
    }
}

/* ---------- Verbosity paths ---------- */
static void exercise_verbosity(void) {
    int bzerr;

    /* Write with verbosity=4 */
    {
        FILE *f = tmpfile();
        if (f) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 4, 0);
            if (bz) {
                BZ2_bzWrite(&bzerr, bz, (void *)test_data,
                            sizeof(test_data) - 1);
                BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
            }
            fclose(f);
        }
    }

    /* Read with verbosity=4 */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 4, 0, NULL, 0);
            if (bz) {
                char buf[4096];
                BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                BZ2_bzReadClose(&bzerr, bz);
            }
            fclose(f);
        }
    }
}

int main(void) {
    if (prepare_bz2() != BZ_OK) {
        fprintf(stderr, "Failed to prepare bz2 data\n");
        return 1;
    }

    printf("Exercise B2B compress params...\n");
    exercise_b2b_compress_params();

    printf("Exercise B2B decompress params...\n");
    exercise_b2b_decompress_params();

    printf("Exercise write I/O errors...\n");
    exercise_write_io_errors();

    printf("Exercise read I/O errors...\n");
    exercise_read_io_errors();

    printf("Exercise NULL bzerror comprehensive...\n");
    exercise_null_bzerror_comprehensive();

    printf("Exercise WriteClose combos...\n");
    exercise_writeclose_combos();

    printf("Exercise bzclose write error...\n");
    exercise_bzclose_write_error();

    printf("Exercise bzopen edges...\n");
    exercise_bzopen_edges();

    printf("Exercise read small chunks...\n");
    exercise_read_small_chunks();

    printf("Exercise sequence errors...\n");
    exercise_sequence_errors();

    printf("Exercise verbosity...\n");
    exercise_verbosity();

    printf("Done.\n");
    return 0;
}
