/*
 * test_bezier_fit.c — Tests for Schneider curve fitting algorithm.
 *
 * No GTK dependency — links only dc_core.
 */

#include "bezier/bezier_fit.h"
#include "bezier/bezier_curve.h"
#include "core/array.h"
#include "core/error.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

#define ASSERT_NEAR(a, b, eps) \
    ASSERT(fabs((a) - (b)) < (eps))

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
test_fit_collinear(void)
{
    /* Collinear points should produce 1 segment */
    DC_Point2 pts[] = { {0,0}, {1,0}, {2,0}, {3,0}, {4,0} };
    DC_Array *out = dc_array_new(sizeof(DC_Point2));
    DC_Array *junc = dc_array_new(sizeof(uint8_t));
    DC_Error err = {0};

    ASSERT(dc_bezier_fit(pts, 5, 1.0, out, junc, &err) == true);

    /* Should have 4 entries: P0, C0, C1, P1 */
    int n = (int)dc_array_length(out);
    ASSERT(n == 4);

    /* Endpoints should match */
    DC_Point2 *p0 = dc_array_get(out, 0);
    DC_Point2 *p3 = dc_array_get(out, (size_t)(n - 1));
    ASSERT_NEAR(p0->x, 0.0, 1e-6);
    ASSERT_NEAR(p0->y, 0.0, 1e-6);
    ASSERT_NEAR(p3->x, 4.0, 1e-6);
    ASSERT_NEAR(p3->y, 0.0, 1e-6);

    /* Juncture flags: 1, 0, 0, 1 */
    uint8_t *f0 = dc_array_get(junc, 0);
    uint8_t *f3 = dc_array_get(junc, (size_t)(n - 1));
    ASSERT(*f0 == 1);
    ASSERT(*f3 == 1);

    dc_array_free(out);
    dc_array_free(junc);
    return 0;
}

static int
test_fit_quarter_circle(void)
{
    /* Generate quarter-circle points */
    int N = 20;
    DC_Point2 pts[20];
    for (int i = 0; i < N; i++) {
        double angle = (double)i / (N - 1) * (M_PI / 2.0);
        pts[i].x = cos(angle);
        pts[i].y = sin(angle);
    }

    DC_Array *out = dc_array_new(sizeof(DC_Point2));
    DC_Array *junc = dc_array_new(sizeof(uint8_t));
    DC_Error err = {0};

    ASSERT(dc_bezier_fit(pts, N, 0.1, out, junc, &err) == true);

    int n = (int)dc_array_length(out);
    ASSERT(n >= 4);  /* at least one segment */

    /* Verify endpoints match */
    DC_Point2 *first = dc_array_get(out, 0);
    DC_Point2 *last = dc_array_get(out, (size_t)(n - 1));
    ASSERT_NEAR(first->x, 1.0, 1e-6);
    ASSERT_NEAR(first->y, 0.0, 1e-6);
    ASSERT_NEAR(last->x, 0.0, 0.1);
    ASSERT_NEAR(last->y, 1.0, 0.1);

    /* Verify error is within tolerance by evaluating fitted curve */
    /* (Just check the output is reasonable) */
    ASSERT(n <= 16);  /* shouldn't need more than ~4 segments */

    dc_array_free(out);
    dc_array_free(junc);
    return 0;
}

static int
test_fit_s_curve(void)
{
    /* S-curve: likely needs multiple segments */
    int N = 40;
    DC_Point2 pts[40];
    for (int i = 0; i < N; i++) {
        double t = (double)i / (N - 1);
        pts[i].x = t * 4.0;
        pts[i].y = sin(t * 2.0 * M_PI);
    }

    DC_Array *out = dc_array_new(sizeof(DC_Point2));
    DC_Array *junc = dc_array_new(sizeof(uint8_t));
    DC_Error err = {0};

    ASSERT(dc_bezier_fit(pts, N, 0.01, out, junc, &err) == true);

    int n = (int)dc_array_length(out);
    /* S-curve needs at least 1 segment (4 pts), likely multiple */
    ASSERT(n >= 4);

    /* Verify juncture pattern: first and last are junctures */
    uint8_t *f0 = dc_array_get(junc, 0);
    ASSERT(*f0 == 1);
    uint8_t *fn = dc_array_get(junc, (size_t)(n - 1));
    ASSERT(*fn == 1);

    /* Verify output count is consistent: 3*segs + 1 */
    ASSERT((n - 1) % 3 == 0);

    dc_array_free(out);
    dc_array_free(junc);
    return 0;
}

static int
test_fit_two_points(void)
{
    /* Minimum input: 2 points → 1 linear segment */
    DC_Point2 pts[] = { {0, 0}, {5, 3} };
    DC_Array *out = dc_array_new(sizeof(DC_Point2));
    DC_Array *junc = dc_array_new(sizeof(uint8_t));
    DC_Error err = {0};

    ASSERT(dc_bezier_fit(pts, 2, 1.0, out, junc, &err) == true);
    ASSERT((int)dc_array_length(out) == 4);

    DC_Point2 *p0 = dc_array_get(out, 0);
    DC_Point2 *p3 = dc_array_get(out, 3);
    ASSERT_NEAR(p0->x, 0.0, 1e-6);
    ASSERT_NEAR(p0->y, 0.0, 1e-6);
    ASSERT_NEAR(p3->x, 5.0, 1e-6);
    ASSERT_NEAR(p3->y, 3.0, 1e-6);

    dc_array_free(out);
    dc_array_free(junc);
    return 0;
}

static int
test_fit_single_point_error(void)
{
    /* Single point should fail */
    DC_Point2 pts[] = { {1, 2} };
    DC_Array *out = dc_array_new(sizeof(DC_Point2));
    DC_Array *junc = dc_array_new(sizeof(uint8_t));
    DC_Error err = {0};

    ASSERT(dc_bezier_fit(pts, 1, 1.0, out, junc, &err) == false);
    ASSERT(err.code == DC_ERROR_INVALID_ARG);

    dc_array_free(out);
    dc_array_free(junc);
    return 0;
}

static int
test_fit_null_args(void)
{
    DC_Point2 pts[] = { {0, 0}, {1, 1} };
    DC_Array *out = dc_array_new(sizeof(DC_Point2));
    DC_Array *junc = dc_array_new(sizeof(uint8_t));
    DC_Error err = {0};

    ASSERT(dc_bezier_fit(NULL, 2, 1.0, out, junc, &err) == false);
    ASSERT(dc_bezier_fit(pts, 2, 1.0, NULL, junc, &err) == false);
    ASSERT(dc_bezier_fit(pts, 2, 1.0, out, NULL, &err) == false);
    ASSERT(dc_bezier_fit(pts, 2, 0.0, out, junc, &err) == false);
    /* NULL err is OK (no crash) */
    ASSERT(dc_bezier_fit(NULL, 2, 1.0, out, junc, NULL) == false);

    dc_array_free(out);
    dc_array_free(junc);
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(void)
{
    fprintf(stderr, "=== test_bezier_fit ===\n");

    RUN_TEST(test_fit_collinear);
    RUN_TEST(test_fit_quarter_circle);
    RUN_TEST(test_fit_s_curve);
    RUN_TEST(test_fit_two_points);
    RUN_TEST(test_fit_single_point_error);
    RUN_TEST(test_fit_null_args);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
