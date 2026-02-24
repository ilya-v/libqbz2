/* malloc_inject.c — LD_PRELOAD interceptor for malloc/fwrite/ferror.
 *
 * Build as shared library:
 *   gcc -shared -fPIC -o malloc_inject.so malloc_inject.c -ldl
 *
 * Use with LD_PRELOAD:
 *   LD_PRELOAD=./malloc_inject.so ./coverage_fault_inject
 *
 * Control via environment variables:
 *   MALLOC_FAIL_AT=N  — fail the Nth malloc call (1-based)
 *   FWRITE_FAIL_AT=N  — fail the Nth fwrite call (returns 0)
 *   FERROR_AFTER=N    — ferror returns 1 after N fread/fwrite calls
 *
 * The counters only count calls AFTER the first BZ2_ function call,
 * to avoid intercepting startup allocations.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int malloc_fail_at = 0;
static int fwrite_fail_at = 0;
static int ferror_after = 0;

static int malloc_count = 0;
static int fwrite_count = 0;
static int io_count = 0;

static int active = 0;  /* Only count after activation */

static void __attribute__((constructor)) init(void) {
    const char *s;
    s = getenv("MALLOC_FAIL_AT");
    if (s) malloc_fail_at = atoi(s);
    s = getenv("FWRITE_FAIL_AT");
    if (s) fwrite_fail_at = atoi(s);
    s = getenv("FERROR_AFTER");
    if (s) ferror_after = atoi(s);
}

/* Activation function — call from the test driver to start counting.
 * Re-reads environment variables so the driver can setenv() then activate(). */
void malloc_inject_activate(void) {
    const char *s;
    s = getenv("MALLOC_FAIL_AT");
    malloc_fail_at = s ? atoi(s) : 0;
    s = getenv("FWRITE_FAIL_AT");
    fwrite_fail_at = s ? atoi(s) : 0;
    s = getenv("FERROR_AFTER");
    ferror_after = s ? atoi(s) : 0;
    active = 1;
    malloc_count = 0;
    fwrite_count = 0;
    io_count = 0;
}

void malloc_inject_deactivate(void) {
    active = 0;
}

void malloc_inject_reset(void) {
    malloc_count = 0;
    fwrite_count = 0;
    io_count = 0;
}

void *malloc(size_t size) {
    static void *(*real_malloc)(size_t) = NULL;
    if (!real_malloc) real_malloc = dlsym(RTLD_NEXT, "malloc");

    if (active && malloc_fail_at > 0) {
        malloc_count++;
        if (malloc_count == malloc_fail_at) {
            return NULL;
        }
    }
    return real_malloc(size);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    static size_t (*real_fwrite)(const void *, size_t, size_t, FILE *) = NULL;
    if (!real_fwrite) real_fwrite = dlsym(RTLD_NEXT, "fwrite");

    if (active && fwrite_fail_at > 0) {
        fwrite_count++;
        if (fwrite_count >= fwrite_fail_at) {
            return 0;  /* Simulate fwrite failure */
        }
    }
    return real_fwrite(ptr, size, nmemb, stream);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    static size_t (*real_fread)(void *, size_t, size_t, FILE *) = NULL;
    if (!real_fread) real_fread = dlsym(RTLD_NEXT, "fread");

    if (active && ferror_after > 0) {
        io_count++;
    }
    return real_fread(ptr, size, nmemb, stream);
}

int ferror(FILE *stream) {
    static int (*real_ferror)(FILE *) = NULL;
    if (!real_ferror) real_ferror = dlsym(RTLD_NEXT, "ferror");

    if (active && ferror_after > 0 && io_count >= ferror_after) {
        return 1;  /* Simulate I/O error */
    }
    return real_ferror(stream);
}
