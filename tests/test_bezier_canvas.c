/*
 * test_bezier_canvas.c — Coordinate transform round-trip tests.
 *
 * Verifies that screen_to_world and world_to_screen are consistent
 * inverses under various zoom and pan configurations.
 *
 * Requires GTK4 to be linked (widgets must be constructable).
 */

#include "bezier/bezier_canvas.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Minimal test framework (same pattern as test_array.c)
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
    do { \
        double _a = (a), _b = (b), _eps = (eps); \
        if (fabs(_a - _b) > _eps) { \
            fprintf(stderr, "  FAIL: %s:%d: %.10f != %.10f (eps=%.10f)\n", \
                    __FILE__, __LINE__, _a, _b, _eps); \
            return 1; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        fprintf(stderr, "  %-50s ", #fn); \
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
    DC_BezierCanvas *c = dc_bezier_canvas_new();
    ASSERT(c != NULL);
    ASSERT(dc_bezier_canvas_widget(c) != NULL);
    dc_bezier_canvas_free(c);
    return 0;
}

static int
test_free_null_is_safe(void)
{
    dc_bezier_canvas_free(NULL);
    return 0;
}

static int
test_widget_null_returns_null(void)
{
    ASSERT(dc_bezier_canvas_widget(NULL) == NULL);
    return 0;
}

static int
test_screen_to_world_round_trip_default(void)
{
    /* With default zoom=4.0, pan=(0,0), verify round-trip at several points.
     * Widget is unrealized (size 0x0), but the math is still consistent
     * because w/2 and h/2 cancel out in the round-trip. */
    DC_BezierCanvas *c = dc_bezier_canvas_new();
    ASSERT(c != NULL);

    double test_sx[] = { 0.0, 100.0, -50.0, 200.0, 0.5 };
    double test_sy[] = { 0.0, 100.0, -50.0, 200.0, 0.5 };

    for (int i = 0; i < 5; i++) {
        double wx, wy, sx2, sy2;
        dc_bezier_canvas_screen_to_world(c, test_sx[i], test_sy[i], &wx, &wy);
        dc_bezier_canvas_world_to_screen(c, wx, wy, &sx2, &sy2);
        ASSERT_NEAR(sx2, test_sx[i], EPS);
        ASSERT_NEAR(sy2, test_sy[i], EPS);
    }

    dc_bezier_canvas_free(c);
    return 0;
}

static int
test_world_to_screen_round_trip_default(void)
{
    DC_BezierCanvas *c = dc_bezier_canvas_new();
    ASSERT(c != NULL);

    double test_wx[] = { 0.0, 25.0, -10.0, 50.0, 0.125 };
    double test_wy[] = { 0.0, 25.0, -10.0, 50.0, 0.125 };

    for (int i = 0; i < 5; i++) {
        double sx, sy, wx2, wy2;
        dc_bezier_canvas_world_to_screen(c, test_wx[i], test_wy[i], &sx, &sy);
        dc_bezier_canvas_screen_to_world(c, sx, sy, &wx2, &wy2);
        ASSERT_NEAR(wx2, test_wx[i], EPS);
        ASSERT_NEAR(wy2, test_wy[i], EPS);
    }

    dc_bezier_canvas_free(c);
    return 0;
}

static int
test_round_trip_with_custom_zoom(void)
{
    DC_BezierCanvas *c = dc_bezier_canvas_new();
    ASSERT(c != NULL);

    double zooms[] = { 0.1, 1.0, 10.0, 50.0, 100.0 };
    for (int z = 0; z < 5; z++) {
        dc_bezier_canvas_set_zoom(c, zooms[z]);

        double sx = 123.456, sy = 78.9;
        double wx, wy, sx2, sy2;
        dc_bezier_canvas_screen_to_world(c, sx, sy, &wx, &wy);
        dc_bezier_canvas_world_to_screen(c, wx, wy, &sx2, &sy2);
        ASSERT_NEAR(sx2, sx, EPS);
        ASSERT_NEAR(sy2, sy, EPS);
    }

    dc_bezier_canvas_free(c);
    return 0;
}

static int
test_zoom_clamp_min(void)
{
    DC_BezierCanvas *c = dc_bezier_canvas_new();
    ASSERT(c != NULL);

    /* Setting zoom below minimum should clamp, not crash */
    dc_bezier_canvas_set_zoom(c, 0.001);

    /* Round-trip should still work */
    double wx, wy, sx2, sy2;
    double sx = 50.0, sy = 50.0;
    dc_bezier_canvas_screen_to_world(c, sx, sy, &wx, &wy);
    dc_bezier_canvas_world_to_screen(c, wx, wy, &sx2, &sy2);
    ASSERT_NEAR(sx2, sx, EPS);
    ASSERT_NEAR(sy2, sy, EPS);

    dc_bezier_canvas_free(c);
    return 0;
}

static int
test_zoom_clamp_max(void)
{
    DC_BezierCanvas *c = dc_bezier_canvas_new();
    ASSERT(c != NULL);

    dc_bezier_canvas_set_zoom(c, 999.0);

    double wx, wy, sx2, sy2;
    double sx = 50.0, sy = 50.0;
    dc_bezier_canvas_screen_to_world(c, sx, sy, &wx, &wy);
    dc_bezier_canvas_world_to_screen(c, wx, wy, &sx2, &sy2);
    ASSERT_NEAR(sx2, sx, EPS);
    ASSERT_NEAR(sy2, sy, EPS);

    dc_bezier_canvas_free(c);
    return 0;
}

static int
test_y_axis_flipped(void)
{
    /* Verify that world Y-up maps to screen Y-down:
     * A positive world Y should produce a smaller screen Y (higher on screen).
     * With pan=(0,0) and widget size=0, center is (0,0).
     * world(0, +10) -> screen: sx=0, sy = 0 - 10*zoom = -40 (default zoom=4)
     * world(0, -10) -> screen: sx=0, sy = 0 + 10*zoom = +40
     * So positive world Y gives negative screen Y offset (up on screen). */
    DC_BezierCanvas *c = dc_bezier_canvas_new();
    ASSERT(c != NULL);

    double sx_pos, sy_pos, sx_neg, sy_neg;
    dc_bezier_canvas_world_to_screen(c, 0.0, 10.0, &sx_pos, &sy_pos);
    dc_bezier_canvas_world_to_screen(c, 0.0, -10.0, &sx_neg, &sy_neg);

    /* Positive world Y should map to smaller screen Y */
    ASSERT(sy_pos < sy_neg);

    dc_bezier_canvas_free(c);
    return 0;
}

static int
test_origin_maps_to_center(void)
{
    /* With pan=(0,0), world origin should map to screen center.
     * Unrealized widget has size 0, so center = (0,0). */
    DC_BezierCanvas *c = dc_bezier_canvas_new();
    ASSERT(c != NULL);

    double sx, sy;
    dc_bezier_canvas_world_to_screen(c, 0.0, 0.0, &sx, &sy);
    /* Center of a 0x0 widget is (0,0) */
    ASSERT_NEAR(sx, 0.0, EPS);
    ASSERT_NEAR(sy, 0.0, EPS);

    dc_bezier_canvas_free(c);
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(int argc, char *argv[])
{
    gtk_init();
    (void)argc; (void)argv;

    fprintf(stderr, "=== test_bezier_canvas ===\n");

    RUN_TEST(test_new_and_free);
    RUN_TEST(test_free_null_is_safe);
    RUN_TEST(test_widget_null_returns_null);
    RUN_TEST(test_screen_to_world_round_trip_default);
    RUN_TEST(test_world_to_screen_round_trip_default);
    RUN_TEST(test_round_trip_with_custom_zoom);
    RUN_TEST(test_zoom_clamp_min);
    RUN_TEST(test_zoom_clamp_max);
    RUN_TEST(test_y_axis_flipped);
    RUN_TEST(test_origin_maps_to_center);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
