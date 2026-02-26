/*
 * test_array.c — Tests for DC_Array.
 *
 * Uses a simple assertion framework: each test is a function that returns
 * 0 on pass or 1 on failure.  main() collects pass/fail counts and exits
 * with 0 (all pass) or 1 (any fail), which CTest interprets correctly.
 *
 * All tests are designed to run cleanly under AddressSanitizer with leak
 * detection enabled.
 */

#include "core/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework
 * ---------------------------------------------------------------------- */
static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL: %s:%d: assertion failed: %s\n", \
                    __FILE__, __LINE__, #cond); \
            return 1; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        fprintf(stderr, "  %-40s ", #fn); \
        int r = fn(); \
        if (r == 0) { fprintf(stderr, "PASS\n"); g_pass++; } \
        else        { fprintf(stderr, "(see above)\n"); g_fail++; } \
    } while (0)

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static int
test_new_and_free(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);
    ASSERT(dc_array_length(arr) == 0);
    dc_array_free(arr);
    return 0;
}

static int
test_new_zero_element_size_returns_null(void)
{
    DC_Array *arr = dc_array_new(0);
    ASSERT(arr == NULL);
    return 0;
}

static int
test_free_null_is_safe(void)
{
    dc_array_free(NULL);
    return 0;
}

static int
test_push_and_get_ints(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    for (int i = 0; i < 10; i++) {
        int rc = dc_array_push(arr, &i);
        ASSERT(rc == 0);
    }

    ASSERT(dc_array_length(arr) == 10);

    for (int i = 0; i < 10; i++) {
        int *p = dc_array_get(arr, (size_t)i);
        ASSERT(p != NULL);
        ASSERT(*p == i);
    }

    dc_array_free(arr);
    return 0;
}

static int
test_push_triggers_realloc(void)
{
    /* Push well past the initial capacity of 8 */
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    for (int i = 0; i < 100; i++) {
        ASSERT(dc_array_push(arr, &i) == 0);
    }

    ASSERT(dc_array_length(arr) == 100);
    for (int i = 0; i < 100; i++) {
        int *p = dc_array_get(arr, (size_t)i);
        ASSERT(p != NULL && *p == i);
    }

    dc_array_free(arr);
    return 0;
}

static int
test_get_out_of_bounds_returns_null(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    int val = 42;
    dc_array_push(arr, &val);

    ASSERT(dc_array_get(arr, 1) == NULL);
    ASSERT(dc_array_get(arr, 100) == NULL);

    dc_array_free(arr);
    return 0;
}

static int
test_remove_middle(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    for (int i = 0; i < 5; i++) dc_array_push(arr, &i);
    /* Array: [0, 1, 2, 3, 4] */

    ASSERT(dc_array_remove(arr, 2) == 0);
    /* Array: [0, 1, 3, 4] */

    ASSERT(dc_array_length(arr) == 4);
    ASSERT(*(int *)dc_array_get(arr, 0) == 0);
    ASSERT(*(int *)dc_array_get(arr, 1) == 1);
    ASSERT(*(int *)dc_array_get(arr, 2) == 3);
    ASSERT(*(int *)dc_array_get(arr, 3) == 4);

    dc_array_free(arr);
    return 0;
}

static int
test_remove_first(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    for (int i = 0; i < 3; i++) dc_array_push(arr, &i);

    ASSERT(dc_array_remove(arr, 0) == 0);

    ASSERT(dc_array_length(arr) == 2);
    ASSERT(*(int *)dc_array_get(arr, 0) == 1);
    ASSERT(*(int *)dc_array_get(arr, 1) == 2);

    dc_array_free(arr);
    return 0;
}

static int
test_remove_last(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    for (int i = 0; i < 3; i++) dc_array_push(arr, &i);

    ASSERT(dc_array_remove(arr, 2) == 0);

    ASSERT(dc_array_length(arr) == 2);
    ASSERT(*(int *)dc_array_get(arr, 0) == 0);
    ASSERT(*(int *)dc_array_get(arr, 1) == 1);

    dc_array_free(arr);
    return 0;
}

static int
test_remove_out_of_bounds(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    int val = 1;
    dc_array_push(arr, &val);

    ASSERT(dc_array_remove(arr, 5) == -1);
    ASSERT(dc_array_length(arr) == 1);

    dc_array_free(arr);
    return 0;
}

static int
test_clear(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    for (int i = 0; i < 5; i++) dc_array_push(arr, &i);
    ASSERT(dc_array_length(arr) == 5);

    dc_array_clear(arr);
    ASSERT(dc_array_length(arr) == 0);

    /* Can push after clear */
    int x = 99;
    ASSERT(dc_array_push(arr, &x) == 0);
    ASSERT(dc_array_length(arr) == 1);
    ASSERT(*(int *)dc_array_get(arr, 0) == 99);

    dc_array_free(arr);
    return 0;
}

static int
test_struct_elements(void)
{
    typedef struct { int x; float y; char label[8]; } Point;

    DC_Array *arr = dc_array_new(sizeof(Point));
    ASSERT(arr != NULL);

    Point p1 = { 1, 2.5f, "alpha" };
    Point p2 = { 3, 4.5f, "beta"  };

    ASSERT(dc_array_push(arr, &p1) == 0);
    ASSERT(dc_array_push(arr, &p2) == 0);

    Point *r1 = dc_array_get(arr, 0);
    ASSERT(r1 != NULL);
    ASSERT(r1->x == 1);
    ASSERT(r1->y == 2.5f);
    ASSERT(strcmp(r1->label, "alpha") == 0);

    Point *r2 = dc_array_get(arr, 1);
    ASSERT(r2 != NULL);
    ASSERT(r2->x == 3);
    ASSERT(strcmp(r2->label, "beta") == 0);

    dc_array_free(arr);
    return 0;
}

static int
test_push_null_element_returns_error(void)
{
    DC_Array *arr = dc_array_new(sizeof(int));
    ASSERT(arr != NULL);

    ASSERT(dc_array_push(arr, NULL) == -1);
    ASSERT(dc_array_length(arr) == 0);

    dc_array_free(arr);
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(void)
{
    fprintf(stderr, "=== test_array ===\n");

    RUN_TEST(test_new_and_free);
    RUN_TEST(test_new_zero_element_size_returns_null);
    RUN_TEST(test_free_null_is_safe);
    RUN_TEST(test_push_and_get_ints);
    RUN_TEST(test_push_triggers_realloc);
    RUN_TEST(test_get_out_of_bounds_returns_null);
    RUN_TEST(test_remove_middle);
    RUN_TEST(test_remove_first);
    RUN_TEST(test_remove_last);
    RUN_TEST(test_remove_out_of_bounds);
    RUN_TEST(test_clear);
    RUN_TEST(test_struct_elements);
    RUN_TEST(test_push_null_element_returns_error);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
