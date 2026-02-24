/* coverage_driver.c — Run fuzz corpus through library APIs for coverage measurement.
 * Reads files from command line args and exercises compress, decompress,
 * streaming, and buffer-to-buffer APIs.
 * Build with gcov: gcc --coverage -O0 -g -Iinclude coverage_driver.c libqbz2_cov.a -o coverage_driver */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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

static void exercise_flush_path(const unsigned char *data, size_t size) {
    /* Exercise BZ_FLUSH compression path — the BZ_M_FLUSHING state */
    if (size < 2) return;
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzCompressInit(&strm, 1, 0, 0) != BZ_OK) return;

    size_t out_cap = size + size / 100 + 700;
    if (out_cap < 700) out_cap = 700;
    char *out = malloc(out_cap);
    if (!out) { BZ2_bzCompressEnd(&strm); return; }

    /* Feed half the data, then FLUSH, then finish */
    size_t half = size / 2;
    strm.next_in = (char *)data;
    strm.avail_in = (unsigned int)half;
    strm.next_out = out;
    strm.avail_out = (unsigned int)out_cap;

    int ret = BZ2_bzCompress(&strm, BZ_RUN);
    if (ret == BZ_RUN_OK) {
        /* Now flush */
        ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        while (ret == BZ_FLUSH_OK) {
            ret = BZ2_bzCompress(&strm, BZ_FLUSH);
        }
        /* Feed second half and finish */
        if (ret == BZ_RUN_OK) {
            strm.next_in = (char *)data + half;
            strm.avail_in = (unsigned int)(size - half);
            ret = BZ2_bzCompress(&strm, BZ_RUN);
            if (ret == BZ_RUN_OK) {
                strm.avail_in = 0;
                do {
                    ret = BZ2_bzCompress(&strm, BZ_FINISH);
                } while (ret == BZ_FINISH_OK);
            }
        }
    }
    BZ2_bzCompressEnd(&strm);
    free(out);
}

static void exercise_outbuff_full(const unsigned char *data, size_t size) {
    /* Exercise BZ_OUTBUFF_FULL paths with undersized output buffers */
    if (size == 0) return;

    /* Compress with too-small output buffer */
    unsigned int tiny_len = 1;
    char tiny[1];
    BZ2_bzBuffToBuffCompress(tiny, &tiny_len, (char *)data,
                             (unsigned int)(size > 1000 ? 1000 : size), 1, 0, 0);

    /* Decompress with too-small output buffer (only if input looks like bz2) */
    if (size >= 4 && data[0] == 'B' && data[1] == 'Z' && data[2] == 'h') {
        tiny_len = 1;
        BZ2_bzBuffToBuffDecompress(tiny, &tiny_len, (char *)data,
                                   (unsigned int)size, 0, 0);
    }
}

static void exercise_randomised_block(void) {
    /* Create a valid bz2 stream and flip the randomization bit to exercise
     * the blockRandomised code path in both FAST and SMALL decompression.
     *
     * Block header: stream header "BZhN" (4 bytes), then block magic
     * 6 bytes (0x314159265359) + 4 bytes CRC + 1 bit randomised flag.
     * The randomised bit is at byte offset 14, bit 7 (MSB). */
    const char *input = "hello world test data for randomised block coverage";
    unsigned int src_len = (unsigned int)strlen(input);
    unsigned int comp_len = src_len + src_len / 100 + 700;
    char *comp = malloc(comp_len);
    if (!comp) return;

    int ret = BZ2_bzBuffToBuffCompress(comp, &comp_len, (char *)input,
                                       src_len, 1, 0, 0);
    if (ret != BZ_OK) { free(comp); return; }

    /* Flip the randomised bit at byte 14 */
    if (comp_len > 14) {
        comp[14] ^= 0x80;
    }

    /* Try decompressing with FAST mode (small=0) — will likely get
     * BZ_DATA_ERROR due to CRC mismatch but exercises the code path */
    unsigned int decomp_len = 4 * 1024 * 1024;
    char *decomp = malloc(decomp_len);
    if (decomp) {
        BZ2_bzBuffToBuffDecompress(decomp, &decomp_len, comp,
                                   comp_len, 0, 0);
        free(decomp);
    }

    /* Try decompressing with SMALL mode (small=1) */
    decomp_len = 4 * 1024 * 1024;
    decomp = malloc(decomp_len);
    if (decomp) {
        BZ2_bzBuffToBuffDecompress(decomp, &decomp_len, comp,
                                   comp_len, 1, 0);
        free(decomp);
    }

    free(comp);
}

static void exercise_bzdopen(const unsigned char *data, size_t size) {
    /* Exercise BZ2_bzdopen — fd-based file open */
    const char *tmppath = "/tmp/cov_bzdopen_test.bz2";

    /* Write via bzdopen */
    int fd = open(tmppath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        BZFILE *bz = BZ2_bzdopen(fd, "wb1");
        if (bz) {
            BZ2_bzwrite(bz, (void *)data,
                        (int)(size > 10000 ? 10000 : size));
            BZ2_bzclose(bz);
        } else {
            close(fd);
        }
    }

    /* Read back via bzdopen */
    fd = open(tmppath, O_RDONLY);
    if (fd >= 0) {
        BZFILE *bz = BZ2_bzdopen(fd, "rb");
        if (bz) {
            char buf[4096];
            while (BZ2_bzread(bz, buf, sizeof(buf)) > 0) {}
            BZ2_bzclose(bz);
        } else {
            close(fd);
        }
    }
    remove(tmppath);
}

static void exercise_small_buffer_decompress(const unsigned char *data, size_t size) {
    /* Streaming decompress with tiny output buffer to exercise the
     * avail_out==0 drain loops in unRLE_obuf_to_output_FAST/SMALL */
    if (size < 4 || data[0] != 'B' || data[1] != 'Z' || data[2] != 'h') return;

    for (int small = 0; small <= 1; small++) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        if (BZ2_bzDecompressInit(&strm, 0, small) != BZ_OK) continue;

        strm.next_in = (char *)data;
        strm.avail_in = (unsigned int)size;

        char out[64];  /* tiny output buffer */
        int ret;
        unsigned int total = 0;
        do {
            strm.next_out = out;
            strm.avail_out = sizeof(out);
            ret = BZ2_bzDecompress(&strm);
            total += sizeof(out) - strm.avail_out;
            if (total > 4 * 1024 * 1024) break;  /* safety limit */
        } while (ret == BZ_OK);

        BZ2_bzDecompressEnd(&strm);
    }
}

static void exercise_byte_at_a_time_decompress(const unsigned char *data, size_t size) {
    /* Feed compressed input one byte at a time to exercise every GET_BITS
     * suspend/resume path in the decompression state machine. This covers
     * the "avail_in == 0 -> RETURN(BZ_OK)" branches in decompress.c. */
    if (size < 4 || data[0] != 'B' || data[1] != 'Z' || data[2] != 'h') return;

    for (int small = 0; small <= 1; small++) {
        bz_stream strm;
        memset(&strm, 0, sizeof(strm));
        if (BZ2_bzDecompressInit(&strm, 0, small) != BZ_OK) continue;

        char out[4096];
        int ret = BZ_OK;
        for (size_t i = 0; i < size && ret == BZ_OK; i++) {
            strm.next_in = (char *)data + i;
            strm.avail_in = 1;
            do {
                strm.next_out = out;
                strm.avail_out = sizeof(out);
                ret = BZ2_bzDecompress(&strm);
            } while (ret == BZ_OK && strm.avail_in > 0);
        }
        BZ2_bzDecompressEnd(&strm);
    }
}

static void exercise_byte_at_a_time_compress(const unsigned char *data, size_t size) {
    /* Feed uncompressed input one byte at a time and drain output one byte
     * at a time to exercise output buffer exhaustion paths in bzCompress. */
    if (size == 0 || size > 50000) return;  /* limit for speed at -O0 */

    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (BZ2_bzCompressInit(&strm, 1, 0, 0) != BZ_OK) return;

    char outbyte;
    int ret;
    /* Feed one byte at a time with BZ_RUN */
    for (size_t i = 0; i < size; i++) {
        strm.next_in = (char *)data + i;
        strm.avail_in = 1;
        do {
            strm.next_out = &outbyte;
            strm.avail_out = 1;
            ret = BZ2_bzCompress(&strm, BZ_RUN);
        } while (ret == BZ_RUN_OK && strm.avail_in > 0);
        if (ret != BZ_RUN_OK) break;
    }
    /* Finish with 1-byte output buffer */
    if (ret == BZ_RUN_OK) {
        strm.avail_in = 0;
        do {
            strm.next_out = &outbyte;
            strm.avail_out = 1;
            ret = BZ2_bzCompress(&strm, BZ_FINISH);
        } while (ret == BZ_FINISH_OK);
    }
    BZ2_bzCompressEnd(&strm);
}

static void exercise_fileio_error_paths(void) {
    /* Exercise FILE* API error paths: wrong-mode calls, NULL handles,
     * parameter errors, truncated files, etc. */
    int bzerr;
    const char *tmppath = "/tmp/cov_errio_test.bz2";

    /* BZ2_bzWriteOpen parameter errors */
    BZ2_bzWriteOpen(&bzerr, NULL, 5, 0, 0);  /* NULL file */
    BZ2_bzWriteOpen(NULL, NULL, 5, 0, 0);     /* NULL bzerror */

    /* BZ2_bzReadOpen parameter errors */
    BZ2_bzReadOpen(&bzerr, NULL, 0, 0, NULL, 0);  /* NULL file */
    {
        FILE *f = fopen("/dev/null", "rb");
        if (f) {
            char unused_buf[10] = {0};
            /* invalid nUnused (negative) */
            BZFILE *bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused_buf, -1);
            if (bz) BZ2_bzReadClose(&bzerr, bz);
            /* invalid small value */
            bz = BZ2_bzReadOpen(&bzerr, f, 0, 2, NULL, 0);
            if (bz) BZ2_bzReadClose(&bzerr, bz);
            /* unused != NULL but nUnused == 0 (valid) */
            bz = BZ2_bzReadOpen(&bzerr, f, 0, 0, unused_buf, 3);
            if (bz) BZ2_bzReadClose(&bzerr, bz);
            fclose(f);
        }
    }

    /* BZ2_bzRead error paths */
    {
        /* First create a valid bz2 file for reading */
        FILE *fw = fopen(tmppath, "wb");
        if (fw) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
            if (bz) {
                char data[] = "test data for error paths coverage";
                BZ2_bzWrite(&bzerr, bz, data, (int)sizeof(data));
                BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
            }
            fclose(fw);

            FILE *fr = fopen(tmppath, "rb");
            if (fr) {
                BZFILE *bzr = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
                if (bzr) {
                    /* Read with NULL buffer */
                    BZ2_bzRead(&bzerr, bzr, NULL, 100);
                    /* Read with len < 0 */
                    char buf[100];
                    BZ2_bzRead(&bzerr, bzr, buf, -1);
                    /* Read with len == 0 */
                    BZ2_bzRead(&bzerr, bzr, buf, 0);
                    /* Normal read to completion */
                    while (bzerr == BZ_OK) {
                        BZ2_bzRead(&bzerr, bzr, buf, sizeof(buf));
                    }
                    /* ReadGetUnused after stream end */
                    void *unused_ptr;
                    int nUnused;
                    BZ2_bzReadGetUnused(&bzerr, bzr, &unused_ptr, &nUnused);
                    /* ReadGetUnused with NULL params */
                    BZ2_bzReadGetUnused(&bzerr, bzr, NULL, &nUnused);
                    BZ2_bzReadGetUnused(&bzerr, bzr, &unused_ptr, NULL);
                    BZ2_bzReadClose(&bzerr, bzr);
                }
                fclose(fr);
            }
        }
        remove(tmppath);
    }

    /* BZ2_bzWrite on a read handle (sequence error) */
    {
        /* Write a valid bz2 file first */
        FILE *fw = fopen(tmppath, "wb");
        if (fw) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(&bzerr, bz, (void*)"hello", 5);
                BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
            }
            fclose(fw);

            /* Open for reading and try to write */
            FILE *fr = fopen(tmppath, "rb");
            if (fr) {
                BZFILE *bzr = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
                if (bzr) {
                    /* Write on read handle -> sequence error */
                    BZ2_bzWrite(&bzerr, bzr, (void*)"data", 4);
                    BZ2_bzReadClose(&bzerr, bzr);
                }
                fclose(fr);
            }
        }
        remove(tmppath);
    }

    /* BZ2_bzWriteClose on a read handle (sequence error) */
    {
        FILE *fw = fopen(tmppath, "wb");
        if (fw) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(&bzerr, bz, (void*)"hello", 5);
                BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
            }
            fclose(fw);

            FILE *fr = fopen(tmppath, "rb");
            if (fr) {
                BZFILE *bzr = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
                if (bzr) {
                    /* WriteClose on read handle -> sequence error */
                    BZ2_bzWriteClose(&bzerr, bzr, 0, NULL, NULL);
                    /* Need to properly close since WriteClose failed */
                    BZ2_bzReadClose(&bzerr, bzr);
                }
                fclose(fr);
            }
        }
        remove(tmppath);
    }

    /* BZ2_bzReadClose on a write handle */
    {
        FILE *fw = fopen(tmppath, "wb");
        if (fw) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
            if (bz) {
                BZ2_bzRead(&bzerr, bz, (void*)"buf", 3);  /* read on write */
                BZ2_bzReadClose(&bzerr, bz);  /* readclose on write handle */
                /* Now the handle is leaked but we need to clean up */
            }
            fclose(fw);
        }
        remove(tmppath);
    }

    /* BZ2_bzReadGetUnused before stream end */
    {
        FILE *fw = fopen(tmppath, "wb");
        if (fw) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(&bzerr, bz, (void*)"hello world", 11);
                BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
            }
            fclose(fw);

            FILE *fr = fopen(tmppath, "rb");
            if (fr) {
                BZFILE *bzr = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
                if (bzr) {
                    /* ReadGetUnused before stream end -> sequence error */
                    void *unused_ptr;
                    int nUnused;
                    BZ2_bzReadGetUnused(&bzerr, bzr, &unused_ptr, &nUnused);
                    BZ2_bzReadClose(&bzerr, bzr);
                }
                fclose(fr);
            }
        }
        remove(tmppath);
    }

    /* Truncated bz2 file -> BZ_UNEXPECTED_EOF */
    {
        /* Write a valid bz2 stream, then truncate it */
        FILE *fw = fopen(tmppath, "wb");
        if (fw) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
            if (bz) {
                char buf[1000];
                memset(buf, 'A', sizeof(buf));
                BZ2_bzWrite(&bzerr, bz, buf, sizeof(buf));
                BZ2_bzWriteClose(&bzerr, bz, 0, NULL, NULL);
            }
            fclose(fw);

            /* Read the file, determine its size, write a truncated version */
            FILE *fr = fopen(tmppath, "rb");
            if (fr) {
                fseek(fr, 0, SEEK_END);
                long sz = ftell(fr);
                fseek(fr, 0, SEEK_SET);
                char *full = malloc(sz);
                if (full) {
                    fread(full, 1, sz, fr);
                    fclose(fr);

                    /* Write truncated version (half the data) */
                    const char *trunc_path = "/tmp/cov_truncated.bz2";
                    fw = fopen(trunc_path, "wb");
                    if (fw) {
                        fwrite(full, 1, sz / 2, fw);
                        fclose(fw);

                        /* Try to read the truncated file -> BZ_UNEXPECTED_EOF */
                        fr = fopen(trunc_path, "rb");
                        if (fr) {
                            BZFILE *bzr = BZ2_bzReadOpen(&bzerr, fr, 0, 0, NULL, 0);
                            if (bzr) {
                                char rbuf[4096];
                                while (bzerr == BZ_OK) {
                                    BZ2_bzRead(&bzerr, bzr, rbuf, sizeof(rbuf));
                                }
                                /* bzerr should be BZ_UNEXPECTED_EOF */
                                BZ2_bzReadClose(&bzerr, bzr);
                            }
                            fclose(fr);
                        }
                        remove(trunc_path);
                    }
                    free(full);
                } else {
                    fclose(fr);
                }
            }
        }
        remove(tmppath);
    }

    /* BZ2_bzWriteClose with NULL bzerror */
    {
        FILE *fw = fopen(tmppath, "wb");
        if (fw) {
            BZFILE *bz = BZ2_bzWriteOpen(&bzerr, fw, 1, 0, 0);
            if (bz) {
                BZ2_bzWrite(&bzerr, bz, (void*)"test", 4);
                /* Close with NULL bzerror pointer */
                BZ2_bzWriteClose(NULL, bz, 0, NULL, NULL);
            }
            fclose(fw);
        }
        remove(tmppath);
    }

    /* BZ2_bzReadClose with NULL handle */
    BZ2_bzReadClose(&bzerr, NULL);
    /* BZ2_bzWriteClose with NULL handle */
    BZ2_bzWriteClose(&bzerr, NULL, 0, NULL, NULL);
    /* BZ2_bzReadGetUnused with NULL handle */
    {
        void *u; int nu;
        BZ2_bzReadGetUnused(&bzerr, NULL, &u, &nu);
    }
}

static void exercise_rle_run_length(void) {
    /* Create input data that produces long RLE runs (4+ same byte repeated)
     * to exercise the deep state_out_len >= 3 branches in both FAST and SMALL
     * decompression paths. */
    size_t len = 10000;
    char *data = malloc(len);
    if (!data) return;
    /* Fill with repeated bytes to produce RLE runs */
    memset(data, 'A', len);

    unsigned int comp_len = (unsigned int)(len + len / 100 + 700);
    char *comp = malloc(comp_len);
    if (!comp) { free(data); return; }

    int ret = BZ2_bzBuffToBuffCompress(comp, &comp_len, data, (unsigned int)len, 1, 0, 0);
    if (ret == BZ_OK) {
        /* Decompress with FAST mode */
        unsigned int decomp_len = (unsigned int)len + 100;
        char *decomp = malloc(decomp_len);
        if (decomp) {
            BZ2_bzBuffToBuffDecompress(decomp, &decomp_len, comp, comp_len, 0, 0);
            free(decomp);
        }
        /* Decompress with SMALL mode */
        decomp_len = (unsigned int)len + 100;
        decomp = malloc(decomp_len);
        if (decomp) {
            BZ2_bzBuffToBuffDecompress(decomp, &decomp_len, comp, comp_len, 1, 0);
            free(decomp);
        }
        /* Decompress SMALL mode with 1-byte output buffer to exercise output drain */
        {
            bz_stream strm;
            memset(&strm, 0, sizeof(strm));
            if (BZ2_bzDecompressInit(&strm, 0, 1) == BZ_OK) {
                strm.next_in = comp;
                strm.avail_in = comp_len;
                char outbyte;
                int r;
                unsigned int total = 0;
                do {
                    strm.next_out = &outbyte;
                    strm.avail_out = 1;
                    r = BZ2_bzDecompress(&strm);
                    total++;
                    if (total > len + 1000) break;
                } while (r == BZ_OK);
                BZ2_bzDecompressEnd(&strm);
            }
        }
    }
    free(comp);
    free(data);
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
    exercise_randomised_block();
    exercise_fileio_error_paths();
    exercise_rle_run_length();

    for (int i = 1; i < argc; i++) {
        size_t size;
        unsigned char *data = read_file(argv[i], &size);
        if (!data) continue;

        /* Try as uncompressed input (compress it) */
        exercise_compress(data, size);
        exercise_streaming_compress(data, size);
        exercise_flush_path(data, size);
        exercise_byte_at_a_time_compress(data, size);

        /* Try as compressed input (decompress it) */
        exercise_decompress(data, size);
        exercise_streaming_decompress(data, size);
        exercise_small_buffer_decompress(data, size);
        exercise_byte_at_a_time_decompress(data, size);

        /* Exercise output buffer overflow paths */
        exercise_outbuff_full(data, size);

        /* Exercise FILE* API */
        exercise_fileio_write_read(data, size);
        exercise_bzopen(data, size);
        exercise_bzdopen(data, size);

        free(data);
    }
    return 0;
}
