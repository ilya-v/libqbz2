/* Minimal single-header test framework for libqbz2 */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int tf_total = 0;
static int tf_passed = 0;
static int tf_failed = 0;
static int tf_assertions = 0;
static const char *tf_current_test = NULL;
static int tf_current_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_##name(void) { \
        tf_current_test = #name; \
        tf_current_failed = 0; \
        tf_total++; \
        test_##name(); \
        if (!tf_current_failed) { tf_passed++; } \
    } \
    static void test_##name(void)

#define ASSERT(cond) do { \
    tf_assertions++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s: assertion failed: %s\n", \
                __FILE__, __LINE__, tf_current_test, #cond); \
        tf_current_failed = 1; \
        if (!tf_failed || tf_current_failed == 0) tf_failed++; \
        tf_current_failed = 1; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    tf_assertions++; \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "  FAIL: %s:%d: %s: expected %lld == %lld (%s == %s)\n", \
                __FILE__, __LINE__, tf_current_test, _a, _b, #a, #b); \
        if (!tf_current_failed) tf_failed++; \
        tf_current_failed = 1; \
        return; \
    } \
} while (0)

#define ASSERT_NE(a, b) do { \
    tf_assertions++; \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a == _b) { \
        fprintf(stderr, "  FAIL: %s:%d: %s: expected %lld != %lld (%s != %s)\n", \
                __FILE__, __LINE__, tf_current_test, _a, _b, #a, #b); \
        if (!tf_current_failed) tf_failed++; \
        tf_current_failed = 1; \
        return; \
    } \
} while (0)

#define ASSERT_MEM_EQ(a, b, len) do { \
    tf_assertions++; \
    if (memcmp((a), (b), (len)) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: %s: memory mismatch (%s vs %s, %d bytes)\n", \
                __FILE__, __LINE__, tf_current_test, #a, #b, (int)(len)); \
        if (!tf_current_failed) tf_failed++; \
        tf_current_failed = 1; \
        return; \
    } \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
    tf_assertions++; \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: %s: expected \"%s\" == \"%s\"\n", \
                __FILE__, __LINE__, tf_current_test, (a), (b)); \
        if (!tf_current_failed) tf_failed++; \
        tf_current_failed = 1; \
        return; \
    } \
} while (0)

#define RUN(name) run_##name()

#define TEST_MAIN_BEGIN() \
    int main(void) { \
        struct timespec ts_start, ts_end; \
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

#define TEST_MAIN_END() \
        clock_gettime(CLOCK_MONOTONIC, &ts_end); \
        double elapsed = (ts_end.tv_sec - ts_start.tv_sec) + \
                         (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9; \
        printf("\n%d tests, %d passed, %d failed, %d assertions (%.3fs)\n", \
               tf_total, tf_passed, tf_failed, tf_assertions, elapsed); \
        return tf_failed > 0 ? 1 : 0; \
    }

#endif /* TEST_FRAMEWORK_H */
