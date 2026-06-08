/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_PASS 0
#define TEST_FAIL 1

static int test_count = 0;
static int test_passed = 0;
static int test_failed = 0;

#define assert_true(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        test_count++;                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            test_passed++;                                                                         \
            printf("  ✓ %s\n", msg);                                                               \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            test_failed++;                                                                         \
            printf("  ✗ %s\n", msg);                                                               \
        }                                                                                          \
    } while (0)

#define assert_equals(actual, expected, msg)                                                       \
    do                                                                                             \
    {                                                                                              \
        test_count++;                                                                              \
        if (actual == expected)                                                                    \
        {                                                                                          \
            test_passed++;                                                                         \
            printf("  ✓ %s\n", msg);                                                               \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            test_failed++;                                                                         \
            printf("  ✗ %s (got %d, expected %d)\n", msg, actual, expected);                       \
        }                                                                                          \
    } while (0)

#define assert_not_equals(actual, expected, msg)                                                   \
    do                                                                                             \
    {                                                                                              \
        test_count++;                                                                              \
        if (actual != expected)                                                                    \
        {                                                                                          \
            test_passed++;                                                                         \
            printf("  ✓ %s\n", msg);                                                               \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            test_failed++;                                                                         \
            printf("  ✗ %s (got %d)\n", msg, actual);                                              \
        }                                                                                          \
    } while (0)

#define test_suite(name) printf("\n%s\n", name);

#define test_summary()                                                                             \
    do                                                                                             \
    {                                                                                              \
        printf("\n========================================\n");                                    \
        printf("Tests passed: %d/%d\n", test_passed, test_count);                                  \
        printf("Tests failed: %d/%d\n", test_failed, test_count);                                  \
        printf("========================================\n");                                      \
        return (test_failed > 0) ? TEST_FAIL : TEST_PASS;                                          \
    } while (0)

#endif
