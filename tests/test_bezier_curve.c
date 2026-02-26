/*
 * test_bezier_curve.c — Tests for DC_BezierCurve.
 *
 * Uses the same ASSERT/RUN_TEST framework as the other DunCAD tests.
 * No GTK dependency — links only dc_core.
 */

#include "bezier/bezier_curve.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

#define EPS 1e-9

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static int
test_new_and_free(void)
{
    DC_BezierCurve *curve = dc_bezier_curve_new();
    ASSERT(curve != NULL);
    ASSERT(dc_bezier_curve_knot_count(curve) == 0);
    dc_bezier_curve_free(curve);

    /* NULL free is safe */
    dc_bezier_curve_free(NULL);

    /* NULL knot_count returns 0 */
    ASSERT(dc_bezier_curve_knot_count(NULL) == 0);

    return 0;
}

static int
test_add_knot(void)
{
    DC_BezierCurve *curve = dc_bezier_curve_new();
    ASSERT(curve != NULL);

    ASSERT(dc_bezier_curve_add_knot(curve, 1.0, 2.0) == 0);
    ASSERT(dc_bezier_curve_add_knot(curve, 3.0, 4.0) == 0);
    ASSERT(dc_bezier_curve_add_knot(curve, 5.0, 6.0) == 0);
    ASSERT(dc_bezier_curve_knot_count(curve) == 3);

    DC_BezierKnot *k0 = dc_bezier_curve_get_knot(curve, 0);
    ASSERT(k0 != NULL);
    ASSERT_NEAR(k0->x, 1.0, EPS);
    ASSERT_NEAR(k0->y, 2.0, EPS);
    /* Handles default to coincident with position */
    ASSERT_NEAR(k0->hpx, 1.0, EPS);
    ASSERT_NEAR(k0->hpy, 2.0, EPS);
    ASSERT_NEAR(k0->hnx, 1.0, EPS);
    ASSERT_NEAR(k0->hny, 2.0, EPS);
    ASSERT(k0->cont == DC_CONTINUITY_SMOOTH);

    DC_BezierKnot *k2 = dc_bezier_curve_get_knot(curve, 2);
    ASSERT(k2 != NULL);
    ASSERT_NEAR(k2->x, 5.0, EPS);
    ASSERT_NEAR(k2->y, 6.0, EPS);

    /* add_knot on NULL curve returns -1 */
    ASSERT(dc_bezier_curve_add_knot(NULL, 0.0, 0.0) == -1);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_get_knot_out_of_bounds(void)
{
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 1.0, 2.0);

    ASSERT(dc_bezier_curve_get_knot(curve, -1) == NULL);
    ASSERT(dc_bezier_curve_get_knot(curve, 1) == NULL);
    ASSERT(dc_bezier_curve_get_knot(curve, 100) == NULL);
    ASSERT(dc_bezier_curve_get_knot(NULL, 0) == NULL);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_eval_linear(void)
{
    /* Two knots on a horizontal line with handles at 1/3 and 2/3 */
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 0.0, 0.0);
    dc_bezier_curve_add_knot(curve, 3.0, 0.0);

    DC_BezierKnot *k0 = dc_bezier_curve_get_knot(curve, 0);
    k0->hnx = 1.0;  k0->hny = 0.0;

    DC_BezierKnot *k1 = dc_bezier_curve_get_knot(curve, 1);
    k1->hpx = 2.0;  k1->hpy = 0.0;

    double x, y;

    ASSERT(dc_bezier_curve_eval(curve, 0, 0.0, &x, &y) == 0);
    ASSERT_NEAR(x, 0.0, EPS);
    ASSERT_NEAR(y, 0.0, EPS);

    ASSERT(dc_bezier_curve_eval(curve, 0, 0.5, &x, &y) == 0);
    ASSERT_NEAR(x, 1.5, EPS);
    ASSERT_NEAR(y, 0.0, EPS);

    ASSERT(dc_bezier_curve_eval(curve, 0, 1.0, &x, &y) == 0);
    ASSERT_NEAR(x, 3.0, EPS);
    ASSERT_NEAR(y, 0.0, EPS);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_eval_endpoints(void)
{
    /* Curved segment: eval(seg, 0) == knot[seg], eval(seg, 1) == knot[seg+1] */
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 0.0, 0.0);
    dc_bezier_curve_add_knot(curve, 1.0, 0.0);

    DC_BezierKnot *k0 = dc_bezier_curve_get_knot(curve, 0);
    k0->hnx = 0.0;  k0->hny = 1.0;

    DC_BezierKnot *k1 = dc_bezier_curve_get_knot(curve, 1);
    k1->hpx = 1.0;  k1->hpy = 1.0;

    double x, y;

    ASSERT(dc_bezier_curve_eval(curve, 0, 0.0, &x, &y) == 0);
    ASSERT_NEAR(x, 0.0, EPS);
    ASSERT_NEAR(y, 0.0, EPS);

    ASSERT(dc_bezier_curve_eval(curve, 0, 1.0, &x, &y) == 0);
    ASSERT_NEAR(x, 1.0, EPS);
    ASSERT_NEAR(y, 0.0, EPS);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_eval_insufficient_knots(void)
{
    double x, y;

    /* NULL curve */
    ASSERT(dc_bezier_curve_eval(NULL, 0, 0.5, &x, &y) == -1);

    /* 0 knots */
    DC_BezierCurve *curve = dc_bezier_curve_new();
    ASSERT(dc_bezier_curve_eval(curve, 0, 0.5, &x, &y) == -1);

    /* 1 knot */
    dc_bezier_curve_add_knot(curve, 0.0, 0.0);
    ASSERT(dc_bezier_curve_eval(curve, 0, 0.5, &x, &y) == -1);

    /* NULL out params */
    dc_bezier_curve_add_knot(curve, 1.0, 0.0);
    ASSERT(dc_bezier_curve_eval(curve, 0, 0.5, NULL, &y) == -1);
    ASSERT(dc_bezier_curve_eval(curve, 0, 0.5, &x, NULL) == -1);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_eval_segment_out_of_bounds(void)
{
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 0.0, 0.0);
    dc_bezier_curve_add_knot(curve, 1.0, 0.0);

    double x, y;

    /* segment -1 */
    ASSERT(dc_bezier_curve_eval(curve, -1, 0.5, &x, &y) == -1);
    /* segment 1 (only segment 0 exists with 2 knots) */
    ASSERT(dc_bezier_curve_eval(curve, 1, 0.5, &x, &y) == -1);
    /* segment 100 */
    ASSERT(dc_bezier_curve_eval(curve, 100, 0.5, &x, &y) == -1);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_polyline_straight(void)
{
    /* Linear segment: handles on the line -> tessellates to just 2 points */
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 0.0, 0.0);
    dc_bezier_curve_add_knot(curve, 3.0, 0.0);

    DC_BezierKnot *k0 = dc_bezier_curve_get_knot(curve, 0);
    k0->hnx = 1.0;  k0->hny = 0.0;

    DC_BezierKnot *k1 = dc_bezier_curve_get_knot(curve, 1);
    k1->hpx = 2.0;  k1->hpy = 0.0;

    DC_Array *pts = dc_array_new(sizeof(DC_Point2));
    ASSERT(dc_bezier_curve_polyline(curve, 0.01, pts) == 0);

    /* Straight line: start point + one endpoint = 2 points */
    ASSERT(dc_array_length(pts) == 2);

    DC_Point2 *p0 = dc_array_get(pts, 0);
    ASSERT_NEAR(p0->x, 0.0, EPS);
    ASSERT_NEAR(p0->y, 0.0, EPS);

    DC_Point2 *p1 = dc_array_get(pts, 1);
    ASSERT_NEAR(p1->x, 3.0, EPS);
    ASSERT_NEAR(p1->y, 0.0, EPS);

    dc_array_free(pts);
    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_polyline_curved(void)
{
    /* Arch-shaped segment: should produce more than 2 points */
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 0.0, 0.0);
    dc_bezier_curve_add_knot(curve, 1.0, 0.0);

    DC_BezierKnot *k0 = dc_bezier_curve_get_knot(curve, 0);
    k0->hnx = 0.0;  k0->hny = 1.0;

    DC_BezierKnot *k1 = dc_bezier_curve_get_knot(curve, 1);
    k1->hpx = 1.0;  k1->hpy = 1.0;

    DC_Array *pts = dc_array_new(sizeof(DC_Point2));
    ASSERT(dc_bezier_curve_polyline(curve, 0.01, pts) == 0);
    ASSERT(dc_array_length(pts) > 2);

    /* First point is start, last point is end */
    DC_Point2 *first = dc_array_get(pts, 0);
    ASSERT_NEAR(first->x, 0.0, EPS);
    ASSERT_NEAR(first->y, 0.0, EPS);

    DC_Point2 *last = dc_array_get(pts, dc_array_length(pts) - 1);
    ASSERT_NEAR(last->x, 1.0, EPS);
    ASSERT_NEAR(last->y, 0.0, EPS);

    /* Error cases */
    ASSERT(dc_bezier_curve_polyline(NULL, 0.01, pts) == -1);
    ASSERT(dc_bezier_curve_polyline(curve, 0.0, pts) == -1);
    ASSERT(dc_bezier_curve_polyline(curve, 0.01, NULL) == -1);

    dc_array_free(pts);
    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_bounds(void)
{
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 0.0, 0.0);
    dc_bezier_curve_add_knot(curve, 5.0, 1.0);

    /* Set handles to extend the control polygon */
    DC_BezierKnot *k0 = dc_bezier_curve_get_knot(curve, 0);
    k0->hnx = 2.0;  k0->hny = 3.0;

    DC_BezierKnot *k1 = dc_bezier_curve_get_knot(curve, 1);
    k1->hpx = 3.0;  k1->hpy = -1.0;

    double lo_x, lo_y, hi_x, hi_y;
    ASSERT(dc_bezier_curve_bounds(curve, &lo_x, &lo_y, &hi_x, &hi_y) == 0);

    /* Bounding box must contain all control points */
    ASSERT(lo_x <= 0.0 && hi_x >= 5.0);
    ASSERT(lo_y <= -1.0 && hi_y >= 3.0);

    /* Verify exact values */
    ASSERT_NEAR(lo_x, 0.0, EPS);
    ASSERT_NEAR(lo_y, -1.0, EPS);
    ASSERT_NEAR(hi_x, 5.0, EPS);
    ASSERT_NEAR(hi_y, 3.0, EPS);

    /* Error cases */
    ASSERT(dc_bezier_curve_bounds(NULL, &lo_x, &lo_y, &hi_x, &hi_y) == -1);
    ASSERT(dc_bezier_curve_bounds(curve, NULL, &lo_y, &hi_x, &hi_y) == -1);

    /* Empty curve */
    DC_BezierCurve *empty = dc_bezier_curve_new();
    ASSERT(dc_bezier_curve_bounds(empty, &lo_x, &lo_y, &hi_x, &hi_y) == -1);
    dc_bezier_curve_free(empty);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_continuity_smooth(void)
{
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 5.0, 5.0);

    DC_BezierKnot *k = dc_bezier_curve_get_knot(curve, 0);
    k->hpx = 3.0;  k->hpy = 5.0;   /* h_prev at (3,5) — mag 2 */
    k->hnx = 7.0;  k->hny = 6.0;   /* h_next at (7,6) — mag sqrt(5) */

    ASSERT(dc_bezier_curve_set_continuity(curve, 0, DC_CONTINUITY_SMOOTH) == 0);
    ASSERT(k->cont == DC_CONTINUITY_SMOOTH);

    /* h_next should be unchanged */
    ASSERT_NEAR(k->hnx, 7.0, EPS);
    ASSERT_NEAR(k->hny, 6.0, EPS);

    /* Verify colinearity: cross product of (hp - pos) and (hn - pos) == 0 */
    double dpx = k->hpx - k->x;
    double dpy = k->hpy - k->y;
    double dnx = k->hnx - k->x;
    double dny = k->hny - k->y;
    double cross = dpx * dny - dpy * dnx;
    ASSERT_NEAR(cross, 0.0, EPS);

    /* Verify h_prev points opposite to h_next (dot product < 0) */
    double dot = dpx * dnx + dpy * dny;
    ASSERT(dot < 0.0);

    /* Verify h_prev magnitude is preserved (was 2.0) */
    double mag_prev = sqrt(dpx * dpx + dpy * dpy);
    ASSERT_NEAR(mag_prev, 2.0, EPS);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_continuity_symmetric(void)
{
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 5.0, 5.0);

    DC_BezierKnot *k = dc_bezier_curve_get_knot(curve, 0);
    k->hpx = 3.0;  k->hpy = 5.0;
    k->hnx = 7.0;  k->hny = 6.0;

    ASSERT(dc_bezier_curve_set_continuity(curve, 0, DC_CONTINUITY_SYMMETRIC) == 0);
    ASSERT(k->cont == DC_CONTINUITY_SYMMETRIC);

    /* h_next unchanged */
    ASSERT_NEAR(k->hnx, 7.0, EPS);
    ASSERT_NEAR(k->hny, 6.0, EPS);

    /* h_prev should be (3, 4) — opposite direction, same magnitude */
    ASSERT_NEAR(k->hpx, 3.0, EPS);
    ASSERT_NEAR(k->hpy, 4.0, EPS);

    /* Verify colinearity */
    double dpx = k->hpx - k->x;
    double dpy = k->hpy - k->y;
    double dnx = k->hnx - k->x;
    double dny = k->hny - k->y;
    double cross = dpx * dny - dpy * dnx;
    ASSERT_NEAR(cross, 0.0, EPS);

    /* Verify equal magnitude */
    double mag_prev = sqrt(dpx * dpx + dpy * dpy);
    double mag_next = sqrt(dnx * dnx + dny * dny);
    ASSERT_NEAR(mag_prev, mag_next, EPS);

    dc_bezier_curve_free(curve);
    return 0;
}

static int
test_continuity_corner(void)
{
    DC_BezierCurve *curve = dc_bezier_curve_new();
    dc_bezier_curve_add_knot(curve, 5.0, 5.0);

    DC_BezierKnot *k = dc_bezier_curve_get_knot(curve, 0);
    k->hpx = 3.0;  k->hpy = 7.0;
    k->hnx = 8.0;  k->hny = 2.0;

    ASSERT(dc_bezier_curve_set_continuity(curve, 0, DC_CONTINUITY_CORNER) == 0);
    ASSERT(k->cont == DC_CONTINUITY_CORNER);

    /* Both handles should be completely unchanged */
    ASSERT_NEAR(k->hpx, 3.0, EPS);
    ASSERT_NEAR(k->hpy, 7.0, EPS);
    ASSERT_NEAR(k->hnx, 8.0, EPS);
    ASSERT_NEAR(k->hny, 2.0, EPS);

    /* Error cases */
    ASSERT(dc_bezier_curve_set_continuity(NULL, 0, DC_CONTINUITY_SMOOTH) == -1);
    ASSERT(dc_bezier_curve_set_continuity(curve, -1, DC_CONTINUITY_SMOOTH) == -1);
    ASSERT(dc_bezier_curve_set_continuity(curve, 100, DC_CONTINUITY_SMOOTH) == -1);

    dc_bezier_curve_free(curve);
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(void)
{
    fprintf(stderr, "=== test_bezier_curve ===\n");

    RUN_TEST(test_new_and_free);
    RUN_TEST(test_add_knot);
    RUN_TEST(test_get_knot_out_of_bounds);
    RUN_TEST(test_eval_linear);
    RUN_TEST(test_eval_endpoints);
    RUN_TEST(test_eval_insufficient_knots);
    RUN_TEST(test_eval_segment_out_of_bounds);
    RUN_TEST(test_polyline_straight);
    RUN_TEST(test_polyline_curved);
    RUN_TEST(test_bounds);
    RUN_TEST(test_continuity_smooth);
    RUN_TEST(test_continuity_symmetric);
    RUN_TEST(test_continuity_corner);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
