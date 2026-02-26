/*
 * test_string_builder.c — Tests for EF_StringBuilder.
 *
 * All tests are designed to run cleanly under AddressSanitizer with leak
 * detection enabled.
 */

#include "core/string_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework (duplicated from test_array.c — no shared header
 * in Phase 1 tests to keep dependencies minimal)
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
test_new_is_empty(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);
    ASSERT(ef_sb_length(sb) == 0);
    ASSERT(strcmp(ef_sb_get(sb), "") == 0);
    ef_sb_free(sb);
    return 0;
}

static int
test_free_null_is_safe(void)
{
    ef_sb_free(NULL);
    return 0;
}

static int
test_append_basic(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ASSERT(ef_sb_append(sb, "hello") == 0);
    ASSERT(ef_sb_length(sb) == 5);
    ASSERT(strcmp(ef_sb_get(sb), "hello") == 0);

    ASSERT(ef_sb_append(sb, " world") == 0);
    ASSERT(ef_sb_length(sb) == 11);
    ASSERT(strcmp(ef_sb_get(sb), "hello world") == 0);

    ef_sb_free(sb);
    return 0;
}

static int
test_append_null_is_noop(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ASSERT(ef_sb_append(sb, "hi") == 0);
    ASSERT(ef_sb_append(sb, NULL) == 0);
    ASSERT(strcmp(ef_sb_get(sb), "hi") == 0);

    ef_sb_free(sb);
    return 0;
}

static int
test_appendf(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ASSERT(ef_sb_appendf(sb, "x=%d, y=%.2f", 42, 3.14) == 0);

    const char *result = ef_sb_get(sb);
    ASSERT(strcmp(result, "x=42, y=3.14") == 0);

    ef_sb_free(sb);
    return 0;
}

static int
test_appendf_multiple(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ASSERT(ef_sb_appendf(sb, "%s", "hello") == 0);
    ASSERT(ef_sb_appendf(sb, " %s", "world") == 0);
    ASSERT(ef_sb_appendf(sb, " %d", 2026) == 0);

    ASSERT(strcmp(ef_sb_get(sb), "hello world 2026") == 0);

    ef_sb_free(sb);
    return 0;
}

static int
test_append_char(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ASSERT(ef_sb_append_char(sb, 'A') == 0);
    ASSERT(ef_sb_append_char(sb, 'B') == 0);
    ASSERT(ef_sb_append_char(sb, 'C') == 0);

    ASSERT(ef_sb_length(sb) == 3);
    ASSERT(strcmp(ef_sb_get(sb), "ABC") == 0);

    ef_sb_free(sb);
    return 0;
}

static int
test_clear_resets_content(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ASSERT(ef_sb_append(sb, "some content") == 0);
    ASSERT(ef_sb_length(sb) > 0);

    ef_sb_clear(sb);
    ASSERT(ef_sb_length(sb) == 0);
    ASSERT(strcmp(ef_sb_get(sb), "") == 0);

    /* Can append after clear */
    ASSERT(ef_sb_append(sb, "fresh") == 0);
    ASSERT(strcmp(ef_sb_get(sb), "fresh") == 0);

    ef_sb_free(sb);
    return 0;
}

static int
test_take_transfers_ownership(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ASSERT(ef_sb_append(sb, "transferred") == 0);

    char *owned = ef_sb_take(sb);
    ASSERT(owned != NULL);
    ASSERT(strcmp(owned, "transferred") == 0);

    /* Builder should be reset */
    ASSERT(ef_sb_length(sb) == 0);
    ASSERT(strcmp(ef_sb_get(sb), "") == 0);

    /* Can still use the builder */
    ASSERT(ef_sb_append(sb, "new") == 0);
    ASSERT(strcmp(ef_sb_get(sb), "new") == 0);

    free(owned);
    ef_sb_free(sb);
    return 0;
}

static int
test_large_string_triggers_realloc(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    /* EF_SB_INITIAL_CAPACITY is 64; push far past it */
    const char *chunk = "0123456789abcdef"; /* 16 chars */
    size_t chunks = 50; /* 800 chars total */

    for (size_t i = 0; i < chunks; i++) {
        ASSERT(ef_sb_append(sb, chunk) == 0);
    }

    ASSERT(ef_sb_length(sb) == chunks * strlen(chunk));

    /* Verify first and last chunks */
    const char *s = ef_sb_get(sb);
    ASSERT(strncmp(s, chunk, strlen(chunk)) == 0);

    ef_sb_free(sb);
    return 0;
}

static int
test_json_construction(void)
{
    /* Simulate a typical use case: building a JSON fragment */
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ASSERT(ef_sb_append(sb, "{") == 0);
    ASSERT(ef_sb_appendf(sb, "\"name\":\"%s\"", "my_project") == 0);
    ASSERT(ef_sb_append(sb, ",") == 0);
    ASSERT(ef_sb_appendf(sb, "\"count\":%d", 3) == 0);
    ASSERT(ef_sb_append(sb, "}") == 0);

    ASSERT(strcmp(ef_sb_get(sb), "{\"name\":\"my_project\",\"count\":3}") == 0);

    ef_sb_free(sb);
    return 0;
}

static int
test_get_after_multiple_ops(void)
{
    EF_StringBuilder *sb = ef_sb_new();
    ASSERT(sb != NULL);

    ef_sb_append(sb, "line1\n");
    ef_sb_append(sb, "line2\n");
    ef_sb_append_char(sb, '\n');

    ASSERT(ef_sb_length(sb) == 13);

    ef_sb_free(sb);
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(void)
{
    fprintf(stderr, "=== test_string_builder ===\n");

    RUN_TEST(test_new_is_empty);
    RUN_TEST(test_free_null_is_safe);
    RUN_TEST(test_append_basic);
    RUN_TEST(test_append_null_is_noop);
    RUN_TEST(test_appendf);
    RUN_TEST(test_appendf_multiple);
    RUN_TEST(test_append_char);
    RUN_TEST(test_clear_resets_content);
    RUN_TEST(test_take_transfers_ownership);
    RUN_TEST(test_large_string_triggers_realloc);
    RUN_TEST(test_json_construction);
    RUN_TEST(test_get_after_multiple_ops);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
