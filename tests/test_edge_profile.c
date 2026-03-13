/*
 * test_edge_profile.c — Tests for edge loop → bezier conversion.
 *
 * Tests circle detection, bezier circle generation, and polygon→bezier.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "gl/edge_profile.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL  %s: %s\n", __func__, msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps, msg) do { \
    if (fabs((double)(a) - (double)(b)) > (eps)) { \
        printf("  FAIL  %s: %s (got %f, expected %f)\n", __func__, msg, (double)(a), (double)(b)); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define PASS() do { printf("  PASS  %s\n", __func__); tests_passed++; } while(0)

/* ---- Helper: generate a circle polygon ---- */
static float *
make_circle(double cx, double cy, double r, int n, int *count)
{
    float *pts = malloc((size_t)n * 2 * sizeof(float));
    for (int i = 0; i < n; i++) {
        double a = 2.0 * M_PI * i / n;
        pts[i*2+0] = (float)(cx + r * cos(a));
        pts[i*2+1] = (float)(cy + r * sin(a));
    }
    *count = n;
    return pts;
}

/* ---- Helper: generate a rectangle polygon ---- */
static float *
make_rect(double cx, double cy, double w, double h, int *count)
{
    float *pts = malloc(4 * 2 * sizeof(float));
    pts[0] = (float)(cx - w/2); pts[1] = (float)(cy - h/2);
    pts[2] = (float)(cx + w/2); pts[3] = (float)(cy - h/2);
    pts[4] = (float)(cx + w/2); pts[5] = (float)(cy + h/2);
    pts[6] = (float)(cx - w/2); pts[7] = (float)(cy + h/2);
    *count = 4;
    return pts;
}

/* =========================================================================
 * Circle detection tests
 * ========================================================================= */

static void test_detect_circle_32(void)
{
    int n;
    float *pts = make_circle(0, 0, 10.0, 32, &n);
    DC_EdgeProfile prof;
    int rc = dc_edge_profile_analyze(pts, n, &prof);
    ASSERT(rc == 0, "analyze should succeed");
    ASSERT(prof.type == DC_PROFILE_CIRCLE, "should detect circle");
    ASSERT_NEAR(prof.circle.radius, 10.0, 0.1, "radius should be ~10");
    ASSERT_NEAR(prof.circle.cx, 0.0, 0.1, "center X should be ~0");
    ASSERT_NEAR(prof.circle.cy, 0.0, 0.1, "center Y should be ~0");
    ASSERT(prof.circle.fit_error < 0.01, "fit error should be tiny");
    free(pts);
    PASS();
}

static void test_detect_circle_64(void)
{
    int n;
    float *pts = make_circle(5.0, -3.0, 7.5, 64, &n);
    DC_EdgeProfile prof;
    int rc = dc_edge_profile_analyze(pts, n, &prof);
    ASSERT(rc == 0, "analyze should succeed");
    ASSERT(prof.type == DC_PROFILE_CIRCLE, "should detect circle");
    ASSERT_NEAR(prof.circle.radius, 7.5, 0.1, "radius");
    ASSERT_NEAR(prof.circle.cx, 5.0, 0.1, "center X");
    ASSERT_NEAR(prof.circle.cy, -3.0, 0.1, "center Y");
    free(pts);
    PASS();
}

static void test_detect_circle_8(void)
{
    /* Minimum viable circle — 8 points, fn=8 */
    int n;
    float *pts = make_circle(0, 0, 5.0, 8, &n);
    DC_EdgeProfile prof;
    int rc = dc_edge_profile_analyze(pts, n, &prof);
    ASSERT(rc == 0, "analyze should succeed");
    ASSERT(prof.type == DC_PROFILE_CIRCLE, "8-point circle should detect");
    ASSERT_NEAR(prof.circle.radius, 5.0, 0.1, "radius");
    free(pts);
    PASS();
}

static void test_detect_rect_not_circle(void)
{
    int n;
    float *pts = make_rect(0, 0, 20.0, 10.0, &n);
    DC_EdgeProfile prof;
    int rc = dc_edge_profile_analyze(pts, n, &prof);
    ASSERT(rc == 0, "analyze should succeed");
    ASSERT(prof.type != DC_PROFILE_CIRCLE, "rectangle should NOT detect as circle");
    free(pts);
    PASS();
}

static void test_detect_null_args(void)
{
    DC_EdgeProfile prof;
    ASSERT(dc_edge_profile_analyze(NULL, 10, &prof) == -1, "NULL pts");
    float pts[6] = {0};
    ASSERT(dc_edge_profile_analyze(pts, 2, &prof) == -1, "count < 3");
    ASSERT(dc_edge_profile_analyze(pts, 3, NULL) == -1, "NULL out");
    PASS();
}

/* =========================================================================
 * Bezier circle generation tests
 * ========================================================================= */

static void test_circle_bezier_point_count(void)
{
    int count;
    DC_EP_Point *pts = dc_edge_profile_circle_bezier(0, 0, 10.0, 8, &count);
    ASSERT(pts != NULL, "should allocate");
    ASSERT(count == 16, "8 segments → 16 points");
    free(pts);
    PASS();
}

static void test_circle_bezier_on_curve_points(void)
{
    /* On-curve points (even indices) should be exactly on the circle */
    int count;
    double r = 10.0, cx = 5.0, cy = -3.0;
    DC_EP_Point *pts = dc_edge_profile_circle_bezier(cx, cy, r, 8, &count);
    ASSERT(pts != NULL, "should allocate");

    for (int i = 0; i < count; i += 2) {
        double dx = pts[i].x - cx;
        double dy = pts[i].y - cy;
        double dist = sqrt(dx*dx + dy*dy);
        ASSERT_NEAR(dist, r, 0.001, "on-curve point should be on circle");
    }
    free(pts);
    PASS();
}

static void test_circle_bezier_control_points_outside(void)
{
    /* Off-curve points (odd indices) should be OUTSIDE the circle
     * (further from center than the radius) */
    int count;
    double r = 10.0;
    DC_EP_Point *pts = dc_edge_profile_circle_bezier(0, 0, r, 8, &count);
    ASSERT(pts != NULL, "should allocate");

    for (int i = 1; i < count; i += 2) {
        double dist = sqrt(pts[i].x * pts[i].x + pts[i].y * pts[i].y);
        ASSERT(dist > r, "control point should be outside circle");
    }
    free(pts);
    PASS();
}

static void test_circle_bezier_arc_accuracy(void)
{
    /* Sample the quadratic bezier arcs and check they stay close to
     * the circle. A quadratic arc through 45° (8 segments) should
     * have max error < 0.5% of radius. */
    int count;
    double r = 100.0; /* large radius to amplify errors */
    DC_EP_Point *pts = dc_edge_profile_circle_bezier(0, 0, r, 8, &count);
    ASSERT(pts != NULL, "should allocate");

    double max_err = 0;
    int n_seg = count / 2;
    for (int seg = 0; seg < n_seg; seg++) {
        int i0 = seg * 2;
        int i1 = seg * 2 + 1;
        int i2 = (seg * 2 + 2) % count;

        /* Sample 20 points along this quadratic segment */
        for (int s = 0; s <= 20; s++) {
            double t = s / 20.0;
            double u = 1.0 - t;
            /* Quadratic bezier: B(t) = (1-t)²P0 + 2(1-t)tP1 + t²P2 */
            double bx = u*u*pts[i0].x + 2*u*t*pts[i1].x + t*t*pts[i2].x;
            double by = u*u*pts[i0].y + 2*u*t*pts[i1].y + t*t*pts[i2].y;
            double dist = sqrt(bx*bx + by*by);
            double err = fabs(dist - r) / r;
            if (err > max_err) max_err = err;
        }
    }

    /* 8-segment quadratic should be within 0.5% */
    ASSERT(max_err < 0.005, "arc error should be < 0.5% of radius");
    printf("         (max arc error: %.4f%%)\n", max_err * 100.0);
    free(pts);
    PASS();
}

static void test_circle_bezier_min_segments(void)
{
    /* Requesting < 4 segments should clamp to 4 */
    int count;
    DC_EP_Point *pts = dc_edge_profile_circle_bezier(0, 0, 5.0, 2, &count);
    ASSERT(pts != NULL, "should allocate");
    ASSERT(count == 8, "clamped to 4 segments → 8 points");
    free(pts);
    PASS();
}

/* =========================================================================
 * Polygon → bezier tests
 * ========================================================================= */

static void test_polygon_bezier_point_count(void)
{
    int n;
    float *rect = make_rect(0, 0, 10, 5, &n);
    int count;
    DC_EP_Point *pts = dc_edge_profile_polygon_bezier(rect, n, &count);
    ASSERT(pts != NULL, "should allocate");
    ASSERT(count == 8, "4-vertex polygon → 8 points (4 on-curve + 4 off-curve)");
    free(pts);
    free(rect);
    PASS();
}

static void test_polygon_bezier_on_curve_match(void)
{
    /* On-curve points should match the original polygon vertices */
    int n;
    float *rect = make_rect(0, 0, 10, 5, &n);
    int count;
    DC_EP_Point *pts = dc_edge_profile_polygon_bezier(rect, n, &count);
    ASSERT(pts != NULL, "should allocate");

    for (int i = 0; i < n; i++) {
        ASSERT_NEAR(pts[i*2].x, rect[i*2+0], 0.001, "on-curve X should match vertex");
        ASSERT_NEAR(pts[i*2].y, rect[i*2+1], 0.001, "on-curve Y should match vertex");
    }
    free(pts);
    free(rect);
    PASS();
}

static void test_polygon_bezier_controls_at_midpoint(void)
{
    /* Off-curve controls should be at edge midpoints */
    int n;
    float *rect = make_rect(0, 0, 10, 5, &n);
    int count;
    DC_EP_Point *pts = dc_edge_profile_polygon_bezier(rect, n, &count);
    ASSERT(pts != NULL, "should allocate");

    for (int i = 0; i < n; i++) {
        int next = (i + 1) % n;
        double mx = (rect[i*2+0] + rect[next*2+0]) / 2.0;
        double my = (rect[i*2+1] + rect[next*2+1]) / 2.0;
        ASSERT_NEAR(pts[i*2+1].x, mx, 0.001, "control X should be midpoint");
        ASSERT_NEAR(pts[i*2+1].y, my, 0.001, "control Y should be midpoint");
    }
    free(pts);
    free(rect);
    PASS();
}

static void test_polygon_bezier_null(void)
{
    int count;
    ASSERT(dc_edge_profile_polygon_bezier(NULL, 5, &count) == NULL, "NULL pts");
    float pts[4] = {0};
    ASSERT(dc_edge_profile_polygon_bezier(pts, 2, &count) == NULL, "count < 3");
    PASS();
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    printf("\n=== Edge Profile Tests ===\n\n");

    /* Circle detection */
    test_detect_circle_32();
    test_detect_circle_64();
    test_detect_circle_8();
    test_detect_rect_not_circle();
    test_detect_null_args();

    /* Bezier circle generation */
    test_circle_bezier_point_count();
    test_circle_bezier_on_curve_points();
    test_circle_bezier_control_points_outside();
    test_circle_bezier_arc_accuracy();
    test_circle_bezier_min_segments();

    /* Polygon → bezier */
    test_polygon_bezier_point_count();
    test_polygon_bezier_on_curve_match();
    test_polygon_bezier_controls_at_midpoint();
    test_polygon_bezier_null();

    printf("\n========================================\n");
    printf("EDGE PROFILE RESULTS\n");
    printf("========================================\n");
    printf("  Tests:      %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_passed + tests_failed);
    printf("========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
