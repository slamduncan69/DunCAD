/*
 * test_scad_export.c — Tests for DC_ScadSpan export to OpenSCAD.
 *
 * No GTK dependency — links only dc_core.
 */

#include "scad/scad_export.h"

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
test_generate_library(void)
{
    char *lib = dc_scad_generate_library();
    ASSERT(lib != NULL);
    ASSERT(strstr(lib, "dc_decasteljau") != NULL);
    ASSERT(strstr(lib, "dc_bezier_path") != NULL);
    ASSERT(strstr(lib, "dc_bezier_span") != NULL);
    ASSERT(strstr(lib, "dc_lerp") != NULL);
    free(lib);
    return 0;
}

static int
test_generate_single_span(void)
{
    DC_Point2 pts[] = {{0.0, 0.0}, {5.0, 10.0}, {10.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 3 };

    DC_Error err = {0};
    char *src = dc_scad_generate("triangle", &span, 1, 1, &err);
    ASSERT(src != NULL);
    ASSERT(err.code == DC_OK);

    ASSERT(strstr(src, "use <duncad_bezier.scad>") != NULL);
    ASSERT(strstr(src, "triangle_spans") != NULL);
    ASSERT(strstr(src, "0.000000, 0.000000") != NULL);
    ASSERT(strstr(src, "5.000000, 10.000000") != NULL);
    ASSERT(strstr(src, "10.000000, 0.000000") != NULL);
    ASSERT(strstr(src, "module triangle_2d()") != NULL);
    ASSERT(strstr(src, "polygon(") != NULL);
    ASSERT(strstr(src, "module triangle(height = 5)") != NULL);
    ASSERT(strstr(src, "linear_extrude") != NULL);
    ASSERT(strstr(src, "triangle();") != NULL);

    free(src);
    return 0;
}

static int
test_generate_multiple_spans(void)
{
    DC_Point2 pts1[] = {{0.0, 0.0}, {5.0, 10.0}, {10.0, 0.0}};
    DC_Point2 pts2[] = {{10.0, 0.0}, {15.0, -5.0}, {20.0, 0.0}};
    DC_ScadSpan spans[] = {
        { .points = pts1, .count = 3 },
        { .points = pts2, .count = 3 },
    };

    char *src = dc_scad_generate("wave", spans, 2, 1, NULL);
    ASSERT(src != NULL);
    ASSERT(strstr(src, "0.000000, 0.000000") != NULL);
    ASSERT(strstr(src, "15.000000, -5.000000") != NULL);
    ASSERT(strstr(src, "20.000000, 0.000000") != NULL);

    free(src);
    return 0;
}

static int
test_generate_closed(void)
{
    DC_Point2 pts[] = {{0.0, 0.0}, {5.0, 10.0}, {10.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 3 };

    char *src = dc_scad_generate("closed", &span, 1, 1, NULL);
    ASSERT(src != NULL);
    ASSERT(strstr(src, "polygon(") != NULL);
    free(src);

    /* Open path uses hull-based line */
    src = dc_scad_generate("open", &span, 1, 0, NULL);
    ASSERT(src != NULL);
    ASSERT(strstr(src, "polygon(") == NULL);
    ASSERT(strstr(src, "hull()") != NULL);
    free(src);

    return 0;
}

static int
test_generate_null_name(void)
{
    DC_Point2 pts[] = {{0.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 1 };
    DC_Error err = {0};

    char *src = dc_scad_generate(NULL, &span, 1, 1, &err);
    ASSERT(src == NULL);
    ASSERT(err.code == DC_ERROR_INVALID_ARG);

    return 0;
}

static int
test_generate_zero_spans(void)
{
    DC_Point2 pts[] = {{0.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 1 };
    DC_Error err = {0};

    char *src = dc_scad_generate("zero", &span, 0, 1, &err);
    ASSERT(src == NULL);
    ASSERT(err.code == DC_ERROR_INVALID_ARG);

    return 0;
}

static int
test_spans_free_null(void)
{
    /* Should not crash */
    dc_scad_spans_free(NULL, 0);
    dc_scad_spans_free(NULL, 5);
    return 0;
}

static int
test_export_writes_files(void)
{
    /* Use /tmp directly — no mkdtemp to avoid POSIX_C_SOURCE in C11 */
    const char *shape_path = "/tmp/duncad_test_shape.scad";
    const char *lib_path   = "/tmp/duncad_bezier.scad";

    DC_Point2 pts[] = {{0.0, 0.0}, {5.0, 10.0}, {10.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 3 };

    DC_Error err = {0};
    int rc = dc_scad_export(shape_path, "test_shape", &span, 1, 1, &err);
    ASSERT(rc == 0);

    /* Verify shape file exists and has content */
    FILE *f = fopen(shape_path, "r");
    ASSERT(f != NULL);
    fseek(f, 0, SEEK_END);
    long shape_size = ftell(f);
    ASSERT(shape_size > 0);
    fclose(f);

    /* Verify companion library exists and has content */
    f = fopen(lib_path, "r");
    ASSERT(f != NULL);
    fseek(f, 0, SEEK_END);
    long lib_size = ftell(f);
    ASSERT(lib_size > 0);
    fclose(f);

    /* Cleanup */
    remove(shape_path);
    remove(lib_path);

    return 0;
}

static int
test_export_null_path(void)
{
    DC_Point2 pts[] = {{0.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 1 };
    DC_Error err = {0};

    int rc = dc_scad_export(NULL, "x", &span, 1, 1, &err);
    ASSERT(rc == -1);
    ASSERT(err.code == DC_ERROR_INVALID_ARG);

    return 0;
}

/* -------------------------------------------------------------------------
 * Inline generation tests
 * ---------------------------------------------------------------------- */

static int
test_inline_closed(void)
{
    DC_Point2 pts[] = {{0.0, 0.0}, {5.0, 10.0}, {10.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 3 };

    DC_Error err = {0};
    char *src = dc_scad_generate_inline("heart", &span, 1, 1, 1.0, &err);
    ASSERT(src != NULL);
    ASSERT(err.code == DC_OK);

    /* Should be self-contained (no use <...>) */
    ASSERT(strstr(src, "use <") == NULL);

    /* Should have embedded bezier math */
    ASSERT(strstr(src, "_heart_decasteljau") != NULL);
    ASSERT(strstr(src, "_heart_lerp") != NULL);
    ASSERT(strstr(src, "_heart_path") != NULL);

    /* Should have span data */
    ASSERT(strstr(src, "heart_spans") != NULL);

    /* Should have 2D module with polygon (closed) */
    ASSERT(strstr(src, "module heart_2d()") != NULL);
    ASSERT(strstr(src, "polygon(") != NULL);

    /* Should have 3D modules */
    ASSERT(strstr(src, "module heart(height") != NULL);
    ASSERT(strstr(src, "linear_extrude") != NULL);
    ASSERT(strstr(src, "module heart_revolve(angle") != NULL);
    ASSERT(strstr(src, "rotate_extrude") != NULL);
    ASSERT(strstr(src, "module heart_offset(r") != NULL);

    /* Should have preview call */
    ASSERT(strstr(src, "heart();") != NULL);

    free(src);
    return 0;
}

static int
test_inline_open(void)
{
    DC_Point2 pts[] = {{0.0, 0.0}, {5.0, 10.0}, {10.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 3 };

    char *src = dc_scad_generate_inline("wire", &span, 1, 0, 2.0, NULL);
    ASSERT(src != NULL);

    /* Open curve should use hull-chained circles for stroke */
    ASSERT(strstr(src, "hull()") != NULL);
    ASSERT(strstr(src, "module wire_2d(width") != NULL);
    ASSERT(strstr(src, "circle(d = width)") != NULL);

    free(src);
    return 0;
}

static int
test_inline_null_args(void)
{
    DC_Point2 pts[] = {{0.0, 0.0}};
    DC_ScadSpan span = { .points = pts, .count = 1 };
    DC_Error err = {0};

    char *src = dc_scad_generate_inline(NULL, &span, 1, 1, 1.0, &err);
    ASSERT(src == NULL);
    ASSERT(err.code == DC_ERROR_INVALID_ARG);

    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(void)
{
    fprintf(stderr, "=== test_scad_export ===\n");

    RUN_TEST(test_generate_library);
    RUN_TEST(test_generate_single_span);
    RUN_TEST(test_generate_multiple_spans);
    RUN_TEST(test_generate_closed);
    RUN_TEST(test_generate_null_name);
    RUN_TEST(test_generate_zero_spans);
    RUN_TEST(test_spans_free_null);
    RUN_TEST(test_export_writes_files);
    RUN_TEST(test_export_null_path);
    RUN_TEST(test_inline_closed);
    RUN_TEST(test_inline_open);
    RUN_TEST(test_inline_null_args);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
