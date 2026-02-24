/* coverage_bzopen_driver.c — Targeted coverage exercises for bzopen/bzdopen,
 * BZ2_bzRead/bzWrite API error paths, and mode string parsing.
 *
 * Exercises uncovered branches in bzlib.c lines 950-1500:
 * - bzopen_or_bzdopen mode string parsing (all mode combinations)
 * - BZ2_bzReadOpen parameter validation
 * - BZ2_bzRead loop with ferror/EOF paths
 * - BZ2_bzReadClose/ReadGetUnused
 * - BZ2_bzWriteClose64 with abandon/non-abandon/byte-count retrieval
 * - BZ2_bzopen with path=NULL, path="", path="-"
 * - BZ2_bzdopen with fd
 * - BZ2_bzread/bzwrite convenience wrappers
 * - Pipe-based error injection for ferror() paths
 *
 * Build:
 *   gcc --coverage -O0 -g -Iinclude coverage_bzopen_driver.c libqbz2_cov.a -o coverage_bzopen_driver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include "bzlib.h"

static const char test_data[] = "Hello World! This is test data for coverage.\n"
                                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n";

/* Compress test_data into a bz2 buffer for later use */
static int make_bz2(char *dest, unsigned int *destLen) {
    return BZ2_bzBuffToBuffCompress(dest, destLen,
                                    (char *)test_data, sizeof(test_data) - 1,
                                    1, 0, 0);
}

/* Exercise BZ2_bzWriteOpen + BZ2_bzWrite + BZ2_bzWriteClose64
 * with various parameter combinations */
static void exercise_write_api(void) {
    FILE *f;
    BZFILE *bz;
    int bzerr;
    unsigned int in_lo, in_hi, out_lo, out_hi;

    /* Normal write with all byte count pointers */
    f = tmpfile();
    if (!f) return;
    bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    if (bz) {
        BZ2_bzWrite(&bzerr, bz, (void *)test_data, sizeof(test_data) - 1);
        BZ2_bzWriteClose64(&bzerr, bz, 0, &in_lo, &in_hi, &out_lo, &out_hi);
    }
    fclose(f);

    /* Write with abandon=1 */
    f = tmpfile();
    if (!f) return;
    bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    if (bz) {
        BZ2_bzWrite(&bzerr, bz, (void *)test_data, sizeof(test_data) - 1);
        BZ2_bzWriteClose64(&bzerr, bz, 1, NULL, NULL, NULL, NULL);
    }
    fclose(f);

    /* Write with len=0 (early return) */
    f = tmpfile();
    if (!f) return;
    bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    if (bz) {
        BZ2_bzWrite(&bzerr, bz, (void *)test_data, 0);
        BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
    }
    fclose(f);

    /* Write with various block sizes */
    for (int bs = 1; bs <= 9; bs += 4) {
        f = tmpfile();
        if (!f) continue;
        bz = BZ2_bzWriteOpen(&bzerr, f, bs, 0, 0);
        if (bz) {
            BZ2_bzWrite(&bzerr, bz, (void *)test_data, sizeof(test_data) - 1);
            BZ2_bzWriteClose64(&bzerr, bz, 0, &in_lo, NULL, &out_lo, NULL);
        }
        fclose(f);
    }

    /* Write with verbosity > 0 */
    f = tmpfile();
    if (!f) return;
    bz = BZ2_bzWriteOpen(&bzerr, f, 1, 4, 0);
    if (bz) {
        BZ2_bzWrite(&bzerr, bz, (void *)test_data, sizeof(test_data) - 1);
        BZ2_bzWriteClose64(&bzerr, bz, 0, NULL, &in_hi, NULL, &out_hi);
    }
    fclose(f);

    /* Error: NULL file */
    BZ2_bzWriteOpen(&bzerr, NULL, 1, 0, 0);

    /* Error: bad block size */
    f = tmpfile();
    if (!f) return;
    BZ2_bzWriteOpen(&bzerr, f, 0, 0, 0);
    BZ2_bzWriteOpen(&bzerr, f, 10, 0, 0);
    fclose(f);

    /* Error: bad workFactor */
    f = tmpfile();
    if (!f) return;
    BZ2_bzWriteOpen(&bzerr, f, 1, 0, -1);
    BZ2_bzWriteOpen(&bzerr, f, 1, 0, 251);
    fclose(f);

    /* Error: bad verbosity */
    f = tmpfile();
    if (!f) return;
    BZ2_bzWriteOpen(&bzerr, f, 1, -1, 0);
    BZ2_bzWriteOpen(&bzerr, f, 1, 5, 0);
    fclose(f);

    /* BZ2_bzWrite errors: NULL bzf, NULL buf, negative len */
    BZ2_bzWrite(&bzerr, NULL, (void *)test_data, 10);
    f = tmpfile();
    if (!f) return;
    bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
    if (bz) {
        BZ2_bzWrite(&bzerr, bz, NULL, 10);
        BZ2_bzWrite(&bzerr, bz, (void *)test_data, -1);
        BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
    }
    fclose(f);

    /* BZ2_bzWriteClose with NULL handle */
    BZ2_bzWriteClose(&bzerr, NULL, 0, NULL, NULL);
    BZ2_bzWriteClose64(&bzerr, NULL, 0, NULL, NULL, NULL, NULL);
}

/* Exercise BZ_SETERR with NULL bzerror pointer to cover the bzerror==NULL branch */
static void exercise_null_bzerror(void) {
    char bz2buf[4096];
    unsigned int bz2len = sizeof(bz2buf);
    char buf[4096];

    if (make_bz2(bz2buf, &bz2len) != BZ_OK) return;

    /* BZ2_bzWriteOpen with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (!f) return;
        BZFILE *bz = BZ2_bzWriteOpen(NULL, f, 1, 0, 0);
        if (bz) {
            /* BZ2_bzWrite with NULL bzerror */
            BZ2_bzWrite(NULL, bz, (void *)test_data, sizeof(test_data) - 1);
            /* BZ2_bzWriteClose with NULL bzerror */
            BZ2_bzWriteClose(NULL, bz, 0, NULL, NULL);
        }
        fclose(f);
    }

    /* BZ2_bzWriteOpen param error with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            BZ2_bzWriteOpen(NULL, f, 0, 0, 0);  /* bad blockSize */
            fclose(f);
        }
    }

    /* BZ2_bzReadOpen with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(bz2buf, 1, bz2len, f);
        rewind(f);
        BZFILE *bz = BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 0);
        if (bz) {
            /* BZ2_bzRead with NULL bzerror */
            BZ2_bzRead(NULL, bz, buf, sizeof(buf));
            /* BZ2_bzReadGetUnused with NULL bzerror */
            void *uptr; int nu;
            BZ2_bzReadGetUnused(NULL, bz, &uptr, &nu);
            /* BZ2_bzReadClose with NULL bzerror */
            BZ2_bzReadClose(NULL, bz);
        }
        fclose(f);
    }

    /* BZ2_bzReadOpen param error with NULL bzerror */
    BZ2_bzReadOpen(NULL, NULL, 0, 0, NULL, 0);

    /* BZ2_bzWriteClose64 with NULL bzerror and various byte-count combos */
    {
        FILE *f = tmpfile();
        if (!f) return;
        int bzerr;
        BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
        if (bz) {
            BZ2_bzWrite(&bzerr, bz, (void *)test_data, sizeof(test_data) - 1);
            unsigned int lo, hi;
            BZ2_bzWriteClose64(NULL, bz, 0, &lo, &hi, &lo, &hi);
        }
        fclose(f);
    }

    /* BZ2_bzReadClose with NULL bzerror */
    BZ2_bzReadClose(NULL, NULL);

    /* BZ2_bzWrite sequence error: write to read handle with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(bz2buf, 1, bz2len, f);
        rewind(f);
        int bzerr;
        BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
        if (bz) {
            BZ2_bzWrite(NULL, bz, (void *)test_data, 10);
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* BZ2_bzWriteClose64 with NULL handle (covers bzf==NULL path) */
    BZ2_bzWriteClose64(NULL, NULL, 0, NULL, NULL, NULL, NULL);
}

/* Exercise BZ2_bzReadOpen + BZ2_bzRead + BZ2_bzReadClose + BZ2_bzReadGetUnused
 * with various parameter combinations */
static void exercise_read_api(void) {
    int bzerr;
    BZFILE *bz;
    char buf[4096];
    char bz2buf[4096];
    unsigned int bz2len = sizeof(bz2buf);
    void *unused_ptr;
    int nUnused;

    if (make_bz2(bz2buf, &bz2len) != BZ_OK) return;

    /* Normal read cycle */
    {
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(bz2buf, 1, bz2len, f);
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
        if (bz) {
            int nr = BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
            (void)nr;
            /* After stream end, get unused */
            if (bzerr == BZ_STREAM_END) {
                BZ2_bzReadGetUnused(&bzerr, bz, &unused_ptr, &nUnused);
            }
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* Read with small=1 */
    {
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(bz2buf, 1, bz2len, f);
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 0, 1, NULL, 0);
        if (bz) {
            BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* Read with verbosity */
    {
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(bz2buf, 1, bz2len, f);
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 4, 0, NULL, 0);
        if (bz) {
            BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* Read with unused data prepended */
    {
        char unused_data[5] = {0x42, 0x5a, 0x68, 0x31, 0x00};
        FILE *f = tmpfile();
        if (!f) return;
        /* Write the rest of the bz2 stream after the first 4 bytes */
        fwrite(bz2buf + 4, 1, bz2len - 4, f);
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused_data, 4);
        if (bz) {
            BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* Read with len=0 (early return) */
    {
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(bz2buf, 1, bz2len, f);
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
        if (bz) {
            int nr = BZ2_bzRead(&bzerr, bz, buf, 0);
            (void)nr;
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* Read that hits unexpected EOF (truncated stream) */
    {
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(bz2buf, 1, bz2len / 2, f);  /* Write only half */
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
        if (bz) {
            BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
            /* Should get BZ_UNEXPECTED_EOF or BZ_DATA_ERROR */
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* Read with invalid data (should get BZ_DATA_ERROR) */
    {
        char garbage[100];
        memset(garbage, 0xAA, sizeof(garbage));
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(garbage, 1, sizeof(garbage), f);
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
        if (bz) {
            BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* Error: NULL file */
    BZ2_bzReadOpen(&bzerr, NULL, 0, 0, NULL, 0);

    /* Error: bad small value */
    {
        FILE *f = tmpfile();
        if (!f) return;
        BZ2_bzReadOpen(&bzerr, f, 0, 2, NULL, 0);
        fclose(f);
    }

    /* Error: bad verbosity */
    {
        FILE *f = tmpfile();
        if (!f) return;
        BZ2_bzReadOpen(&bzerr, f, -1, 0, NULL, 0);
        BZ2_bzReadOpen(&bzerr, f, 5, 0, NULL, 0);
        fclose(f);
    }

    /* Error: NULL unused with nUnused > 0 */
    {
        FILE *f = tmpfile();
        if (!f) return;
        BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 1);
        fclose(f);
    }

    /* Error: nUnused out of range */
    {
        char dummy[1] = {0};
        FILE *f = tmpfile();
        if (!f) return;
        BZ2_bzReadOpen(&bzerr, f, 0, 0, dummy, -1);
        BZ2_bzReadOpen(&bzerr, f, 0, 0, dummy, 5001);
        fclose(f);
    }

    /* BZ2_bzRead errors */
    BZ2_bzRead(&bzerr, NULL, buf, 10);

    /* BZ2_bzReadClose with NULL */
    BZ2_bzReadClose(&bzerr, NULL);

    /* BZ2_bzReadGetUnused errors */
    BZ2_bzReadGetUnused(&bzerr, NULL, &unused_ptr, &nUnused);

    /* Sequence error: call write on read handle */
    {
        FILE *f = tmpfile();
        if (!f) return;
        fwrite(bz2buf, 1, bz2len, f);
        rewind(f);
        bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
        if (bz) {
            /* Try writing to read handle */
            BZ2_bzWrite(&bzerr, bz, (void *)test_data, 10);
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }

    /* Sequence error: call read on write handle */
    {
        FILE *f = tmpfile();
        if (!f) return;
        bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
        if (bz) {
            BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
            /* Also try ReadClose/ReadGetUnused on write handle */
            BZ2_bzReadClose(&bzerr, bz);
        }
        fclose(f);
    }
}

/* Exercise bzopen_or_bzdopen mode string parsing */
static void exercise_bzopen(void) {
    BZFILE *bz;

    /* NULL mode */
    bz = BZ2_bzopen("/dev/null", NULL);
    if (bz) BZ2_bzclose(bz);

    /* Write modes with different block sizes */
    {
        char tmppath[] = "/tmp/bzopen_cov_XXXXXX";
        int fd = mkstemp(tmppath);
        if (fd >= 0) {
            close(fd);
            /* w mode with digit */
            bz = BZ2_bzopen(tmppath, "w1");
            if (bz) {
                BZ2_bzwrite(bz, (void *)test_data, sizeof(test_data) - 1);
                BZ2_bzclose(bz);
            }
            /* w mode with block size 9 */
            bz = BZ2_bzopen(tmppath, "w9");
            if (bz) {
                BZ2_bzwrite(bz, (void *)test_data, sizeof(test_data) - 1);
                BZ2_bzclose(bz);
            }
            /* w mode plain */
            bz = BZ2_bzopen(tmppath, "w");
            if (bz) {
                BZ2_bzwrite(bz, (void *)test_data, sizeof(test_data) - 1);
                BZ2_bzclose(bz);
            }

            /* Read back */
            bz = BZ2_bzopen(tmppath, "r");
            if (bz) {
                char buf[4096];
                BZ2_bzread(bz, buf, sizeof(buf));
                BZ2_bzclose(bz);
            }

            /* Read with small mode */
            bz = BZ2_bzopen(tmppath, "rs");
            if (bz) {
                char buf[4096];
                BZ2_bzread(bz, buf, sizeof(buf));
                BZ2_bzclose(bz);
            }

            unlink(tmppath);
        }
    }

    /* bzdopen with fd */
    {
        char tmppath[] = "/tmp/bzopen_cov2_XXXXXX";
        int fd = mkstemp(tmppath);
        if (fd >= 0) {
            /* Write via bzdopen */
            bz = BZ2_bzdopen(fd, "w1");
            if (bz) {
                BZ2_bzwrite(bz, (void *)test_data, sizeof(test_data) - 1);
                BZ2_bzclose(bz);
            } else {
                close(fd);
            }

            /* Read via bzdopen */
            fd = open(tmppath, O_RDONLY);
            if (fd >= 0) {
                bz = BZ2_bzdopen(fd, "r");
                if (bz) {
                    char buf[4096];
                    BZ2_bzread(bz, buf, sizeof(buf));
                    BZ2_bzclose(bz);
                } else {
                    close(fd);
                }
            }

            unlink(tmppath);
        }
    }

    /* bzopen with path="" (should use stdin/stdout) — skip in coverage
     * to avoid blocking on actual stdin, just test the branching */

    /* bzopen with nonexistent path */
    bz = BZ2_bzopen("/nonexistent/path/file.bz2", "r");
    if (bz) BZ2_bzclose(bz);

    bz = BZ2_bzopen("/nonexistent/path/file.bz2", "w");
    if (bz) BZ2_bzclose(bz);

    /* bzflush is a no-op convenience function */
    {
        char tmppath2[] = "/tmp/bzflush_cov_XXXXXX";
        int fd2 = mkstemp(tmppath2);
        if (fd2 >= 0) {
            close(fd2);
            bz = BZ2_bzopen(tmppath2, "w1");
            if (bz) {
                BZ2_bzflush(bz);
                BZ2_bzclose(bz);
            }
            unlink(tmppath2);
        }
    }
}

/* Exercise pipe-based error injection for ferror() paths.
 * Create a pipe, write partial data, close the write end,
 * then try to read — this triggers ferror/EOF conditions. */
static void exercise_pipe_errors(void) {
    int bzerr;
    BZFILE *bz;
    char buf[4096];
    char bz2buf[4096];
    unsigned int bz2len = sizeof(bz2buf);

    if (make_bz2(bz2buf, &bz2len) != BZ_OK) return;

    /* Pipe that provides truncated bz2 data */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            /* Write partial bz2 data to pipe and close write end */
            write(pipefd[1], bz2buf, bz2len / 3);
            close(pipefd[1]);

            FILE *f = fdopen(pipefd[0], "rb");
            if (f) {
                bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
                if (bz) {
                    /* Read will hit EOF mid-stream */
                    BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                    BZ2_bzReadClose(&bzerr, bz);
                }
                fclose(f);
            } else {
                close(pipefd[0]);
            }
        }
    }

    /* Pipe that provides complete bz2 data (should succeed) */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            write(pipefd[1], bz2buf, bz2len);
            close(pipefd[1]);

            FILE *f = fdopen(pipefd[0], "rb");
            if (f) {
                bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
                if (bz) {
                    int nr = BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                    (void)nr;
                    if (bzerr == BZ_STREAM_END) {
                        void *unused_ptr;
                        int nUnused;
                        BZ2_bzReadGetUnused(&bzerr, bz, &unused_ptr, &nUnused);
                    }
                    BZ2_bzReadClose(&bzerr, bz);
                }
                fclose(f);
            } else {
                close(pipefd[0]);
            }
        }
    }

    /* Write to a pipe that has been closed on the read end — triggers ferror */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            close(pipefd[0]);  /* Close read end */

            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
                if (bz) {
                    /* Write a lot of data to trigger fwrite failure */
                    char big[65536];
                    memset(big, 'A', sizeof(big));
                    BZ2_bzWrite(&bzerr, bz, big, sizeof(big));
                    /* The write may or may not fail depending on pipe buffer */
                    BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
                }
                fclose(f);
            } else {
                close(pipefd[1]);
            }
        }
    }
}

/* Exercise BZ2_bzread/bzwrite small-buffer reads */
static void exercise_convenience_io(void) {
    char tmppath[] = "/tmp/bzconv_cov_XXXXXX";
    int fd = mkstemp(tmppath);
    if (fd < 0) return;
    close(fd);

    /* Write via BZ2_bzwrite */
    BZFILE *bz = BZ2_bzopen(tmppath, "w1");
    if (bz) {
        /* Write in small chunks */
        for (int i = 0; i < 10; i++) {
            BZ2_bzwrite(bz, (void *)"Hello ", 6);
        }
        BZ2_bzclose(bz);
    }

    /* Read via BZ2_bzread in small chunks */
    bz = BZ2_bzopen(tmppath, "r");
    if (bz) {
        char buf[10];
        int total = 0;
        int nr;
        while ((nr = BZ2_bzread(bz, buf, sizeof(buf))) > 0) {
            total += nr;
        }
        BZ2_bzclose(bz);
    }

    /* Read with BZ2_bzerror reporting */
    bz = BZ2_bzopen(tmppath, "r");
    if (bz) {
        char buf[4096];
        int nr = BZ2_bzread(bz, buf, sizeof(buf));
        (void)nr;
        int errnum;
        const char *errmsg = BZ2_bzerror(bz, &errnum);
        (void)errmsg;
        BZ2_bzclose(bz);
    }

    unlink(tmppath);
}

/* Exercise every error path with NULL bzerror to cover the bzerror==NULL
 * branch in each BZ_SETERR macro expansion. Also exercise with NULL buf,
 * negative len, and sequence errors with NULL bzerror. */
static void exercise_null_bzerror_error_paths(void) {
    char bz2buf[4096];
    unsigned int bz2len = sizeof(bz2buf);
    char buf[4096];

    if (make_bz2(bz2buf, &bz2len) != BZ_OK) return;

    /* --- Write API error paths with NULL bzerror --- */

    /* BZ2_bzWrite: NULL buf with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            int bzerr;
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(NULL, bz, NULL, 10);  /* NULL buf */
                BZ2_bzWrite(NULL, bz, (void *)test_data, -1);  /* negative len */
                BZ2_bzWriteClose(NULL, bz, 1, NULL, NULL);
            }
            fclose(f);
        }
    }

    /* BZ2_bzWriteClose64: sequence error (read handle) with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            int bzerr;
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                /* Try WriteClose on read handle — sequence error */
                BZ2_bzWriteClose(NULL, bz, 0, NULL, NULL);
                BZ2_bzReadClose(&bzerr, bz);
            }
            fclose(f);
        }
    }

    /* BZ2_bzWriteOpen: ferror with NULL bzerror */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            close(pipefd[0]);  /* Close read end to cause write errors */
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                /* Write enough to trigger EPIPE, then check ferror */
                char big[65536];
                memset(big, 'A', sizeof(big));
                fwrite(big, 1, sizeof(big), f);
                /* Now f has ferror set */
                BZFILE *bz = BZ2_bzWriteOpen(NULL, f, 1, 0, 0);
                if (bz) BZ2_bzWriteClose(NULL, bz, 1, NULL, NULL);
                fclose(f);
            } else {
                close(pipefd[1]);
            }
        }
    }

    /* --- Read API error paths with NULL bzerror --- */

    /* BZ2_bzRead: NULL buf with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            int bzerr;
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                BZ2_bzRead(NULL, bz, NULL, 10);  /* NULL buf */
                BZ2_bzRead(NULL, bz, buf, -1);  /* negative len */
                BZ2_bzRead(NULL, bz, buf, 0);  /* zero len */
                BZ2_bzReadClose(NULL, bz);
            }
            fclose(f);
        }
    }

    /* BZ2_bzRead: sequence error (write handle) with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            int bzerr;
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
            if (bz) {
                BZ2_bzRead(NULL, bz, buf, sizeof(buf));
                BZ2_bzWriteClose(&bzerr, bz, 1, NULL, NULL);
            }
            fclose(f);
        }
    }

    /* BZ2_bzRead: data error with NULL bzerror */
    {
        char garbage[100];
        memset(garbage, 0xAA, sizeof(garbage));
        FILE *f = tmpfile();
        if (f) {
            fwrite(garbage, 1, sizeof(garbage), f);
            rewind(f);
            BZFILE *bz = BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 0);
            if (bz) {
                BZ2_bzRead(NULL, bz, buf, sizeof(buf));
                BZ2_bzReadClose(NULL, bz);
            }
            fclose(f);
        }
    }

    /* BZ2_bzRead: truncated stream (unexpected EOF) with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len / 2, f);
            rewind(f);
            BZFILE *bz = BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 0);
            if (bz) {
                BZ2_bzRead(NULL, bz, buf, sizeof(buf));
                BZ2_bzReadClose(NULL, bz);
            }
            fclose(f);
        }
    }

    /* BZ2_bzRead: successful read with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            BZFILE *bz = BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 0);
            if (bz) {
                BZ2_bzRead(NULL, bz, buf, sizeof(buf));
                /* ReadGetUnused with NULL bzerror after stream end */
                void *uptr; int nu;
                BZ2_bzReadGetUnused(NULL, bz, &uptr, &nu);
                BZ2_bzReadClose(NULL, bz);
            }
            fclose(f);
        }
    }

    /* BZ2_bzRead: small output buffer (returns partial, BZ_OK) with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            BZFILE *bz = BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 0);
            if (bz) {
                char tiny[1];
                BZ2_bzRead(NULL, bz, tiny, 1);  /* Very small buffer */
                BZ2_bzRead(NULL, bz, buf, sizeof(buf));  /* Read rest */
                BZ2_bzReadClose(NULL, bz);
            }
            fclose(f);
        }
    }

    /* --- ReadGetUnused error paths with NULL bzerror --- */

    /* Not at STREAM_END */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            int bzerr;
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                /* Don't read — lastErr is still BZ_OK, not STREAM_END */
                void *uptr; int nu;
                BZ2_bzReadGetUnused(NULL, bz, &uptr, &nu);  /* sequence error */
                BZ2_bzReadClose(&bzerr, bz);
            }
            fclose(f);
        }
    }

    /* NULL unused/nUnused pointers */
    {
        FILE *f = tmpfile();
        if (f) {
            fwrite(bz2buf, 1, bz2len, f);
            rewind(f);
            int bzerr;
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0);
            if (bz) {
                BZ2_bzRead(&bzerr, bz, buf, sizeof(buf));
                if (bzerr == BZ_STREAM_END) {
                    int nu;
                    BZ2_bzReadGetUnused(NULL, bz, NULL, &nu);  /* NULL unused */
                    void *uptr;
                    BZ2_bzReadGetUnused(NULL, bz, &uptr, NULL);  /* NULL nUnused */
                }
                BZ2_bzReadClose(&bzerr, bz);
            }
            fclose(f);
        }
    }

    /* --- Pipe-based ferror injection with NULL bzerror --- */

    /* Read from pipe that gets truncated, with NULL bzerror */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            write(pipefd[1], bz2buf, bz2len / 3);
            close(pipefd[1]);
            FILE *f = fdopen(pipefd[0], "rb");
            if (f) {
                BZFILE *bz = BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 0);
                if (bz) {
                    BZ2_bzRead(NULL, bz, buf, sizeof(buf));
                    BZ2_bzReadClose(NULL, bz);
                }
                fclose(f);
            } else {
                close(pipefd[0]);
            }
        }
    }

    /* Write to broken pipe with NULL bzerror */
    {
        int pipefd[2];
        if (pipe(pipefd) == 0) {
            close(pipefd[0]);
            FILE *f = fdopen(pipefd[1], "wb");
            if (f) {
                BZFILE *bz = BZ2_bzWriteOpen(NULL, f, 1, 0, 0);
                if (bz) {
                    char big[65536];
                    memset(big, 'A', sizeof(big));
                    BZ2_bzWrite(NULL, bz, big, sizeof(big));
                    BZ2_bzWriteClose(NULL, bz, 0, NULL, NULL);
                }
                fclose(f);
            } else {
                close(pipefd[1]);
            }
        }
    }

    /* --- ReadClose error paths with NULL bzerror --- */

    /* ReadClose on write handle with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            int bzerr;
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, f, 1, 0, 0);
            if (bz) {
                BZ2_bzReadClose(NULL, bz);
            }
            fclose(f);
        }
    }

    /* BZ2_bzReadOpen: various param errors with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            BZ2_bzReadOpen(NULL, f, 0, 2, NULL, 0);  /* bad small */
            BZ2_bzReadOpen(NULL, f, -1, 0, NULL, 0);  /* bad verbosity */
            BZ2_bzReadOpen(NULL, f, 5, 0, NULL, 0);  /* bad verbosity */
            BZ2_bzReadOpen(NULL, f, 0, 0, NULL, 1);  /* NULL unused + nUnused>0 */
            char dummy[1] = {0};
            BZ2_bzReadOpen(NULL, f, 0, 0, dummy, -1);  /* negative nUnused */
            BZ2_bzReadOpen(NULL, f, 0, 0, dummy, 5001);  /* nUnused too large */
            fclose(f);
        }
    }

    /* BZ2_bzWriteOpen: various param errors with NULL bzerror */
    {
        FILE *f = tmpfile();
        if (f) {
            BZ2_bzWriteOpen(NULL, f, 0, 0, 0);  /* bad blockSize */
            BZ2_bzWriteOpen(NULL, f, 10, 0, 0);  /* bad blockSize */
            BZ2_bzWriteOpen(NULL, f, 1, -1, 0);  /* bad verbosity */
            BZ2_bzWriteOpen(NULL, f, 1, 5, 0);  /* bad verbosity */
            BZ2_bzWriteOpen(NULL, f, 1, 0, -1);  /* bad workFactor */
            BZ2_bzWriteOpen(NULL, f, 1, 0, 251);  /* bad workFactor */
            fclose(f);
        }
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);  /* Ignore SIGPIPE from broken pipes */

    fprintf(stderr, "Exercise NULL bzerror...\n");
    exercise_null_bzerror();

    fprintf(stderr, "Exercise NULL bzerror error paths...\n");
    exercise_null_bzerror_error_paths();

    fprintf(stderr, "Exercise write API...\n");
    exercise_write_api();

    fprintf(stderr, "Exercise read API...\n");
    exercise_read_api();

    fprintf(stderr, "Exercise bzopen...\n");
    exercise_bzopen();

    fprintf(stderr, "Exercise pipe errors...\n");
    exercise_pipe_errors();

    fprintf(stderr, "Exercise convenience I/O...\n");
    exercise_convenience_io();

    fprintf(stderr, "Done.\n");
    return 0;
}
