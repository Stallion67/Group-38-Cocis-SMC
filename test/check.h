#ifndef TEST_CHECK_H
#define TEST_CHECK_H
/* Tiny test helper: CHECK prints the failing line and counts failures. */
#include <stdio.h>

static int g_checks, g_failed;

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_failed++;                                                          \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                        \
    } while (0)

#define TEST_DONE(name)                                                          \
    do {                                                                         \
        printf("%s: %d checks, %d failed\n", name, g_checks, g_failed);          \
        return g_failed ? 1 : 0;                                                 \
    } while (0)

#endif
