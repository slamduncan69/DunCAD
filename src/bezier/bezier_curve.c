#include "bezier/bezier_curve.h"

#include <math.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */

struct DC_BezierCurve {
    DC_Array *knots;  /* DC_Array of DC_BezierKnot */
};

/* Maximum recursion depth for adaptive tessellation */
#define DC_SUBDIVIDE_MAX_DEPTH 16

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

DC_BezierCurve *
dc_bezier_curve_new(void)
{
    DC_BezierCurve *curve = malloc(sizeof(DC_BezierCurve));
    if (!curve) return NULL;

    curve->knots = dc_array_new(sizeof(DC_BezierKnot));
    if (!curve->knots) {
        free(curve);
        return NULL;
    }

    return curve;
}

void
dc_bezier_curve_free(DC_BezierCurve *curve)
{
    if (!curve) return;
    dc_array_free(curve->knots);
    free(curve);
}

/* -------------------------------------------------------------------------
 * Knot manipulation
 * ---------------------------------------------------------------------- */

int
dc_bezier_curve_add_knot(DC_BezierCurve *curve, double x, double y)
{
    if (!curve) return -1;

    DC_BezierKnot knot;
    knot.x   = x;
    knot.y   = y;
    knot.hpx = x;
    knot.hpy = y;
    knot.hnx = x;
    knot.hny = y;
    knot.cont = DC_CONTINUITY_SMOOTH;

    return dc_array_push(curve->knots, &knot);
}

int
dc_bezier_curve_knot_count(const DC_BezierCurve *curve)
{
    if (!curve) return 0;
    return (int)dc_array_length((DC_Array *)curve->knots);
}

DC_BezierKnot *
dc_bezier_curve_get_knot(DC_BezierCurve *curve, int index)
{
    if (!curve || index < 0) return NULL;
    return dc_array_get(curve->knots, (size_t)index);
}

int
dc_bezier_curve_set_continuity(DC_BezierCurve *curve, int index,
                               DC_Continuity c)
{
    if (!curve || index < 0) return -1;

    DC_BezierKnot *knot = dc_array_get(curve->knots, (size_t)index);
    if (!knot) return -1;

    knot->cont = c;

    if (c == DC_CONTINUITY_CORNER) return 0;

    /* SMOOTH and SYMMETRIC: adjust h_prev to match h_next direction */
    double dnx = knot->hnx - knot->x;
    double dny = knot->hny - knot->y;
    double mag_next = sqrt(dnx * dnx + dny * dny);

    if (mag_next < 1e-12) {
        /* h_next is coincident with knot; collapse h_prev too */
        knot->hpx = knot->x;
        knot->hpy = knot->y;
        return 0;
    }

    double dir_x = dnx / mag_next;
    double dir_y = dny / mag_next;

    if (c == DC_CONTINUITY_SMOOTH) {
        /* Keep h_prev magnitude, set direction opposite to h_next */
        double dpx = knot->hpx - knot->x;
        double dpy = knot->hpy - knot->y;
        double mag_prev = sqrt(dpx * dpx + dpy * dpy);

        knot->hpx = knot->x - mag_prev * dir_x;
        knot->hpy = knot->y - mag_prev * dir_y;
    } else {
        /* SYMMETRIC: colinear + equal magnitude */
        knot->hpx = knot->x - mag_next * dir_x;
        knot->hpy = knot->y - mag_next * dir_y;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * De Casteljau evaluation (static helper)
 * ---------------------------------------------------------------------- */

static void
eval_cubic(double p0x, double p0y, double p1x, double p1y,
           double p2x, double p2y, double p3x, double p3y,
           double t, double *out_x, double *out_y)
{
    double u = 1.0 - t;

    double q0x = u * p0x + t * p1x;
    double q0y = u * p0y + t * p1y;
    double q1x = u * p1x + t * p2x;
    double q1y = u * p1y + t * p2y;
    double q2x = u * p2x + t * p3x;
    double q2y = u * p2y + t * p3y;

    double r0x = u * q0x + t * q1x;
    double r0y = u * q0y + t * q1y;
    double r1x = u * q1x + t * q2x;
    double r1y = u * q1y + t * q2y;

    *out_x = u * r0x + t * r1x;
    *out_y = u * r0y + t * r1y;
}

/* -------------------------------------------------------------------------
 * Evaluation (public)
 * ---------------------------------------------------------------------- */

int
dc_bezier_curve_eval(const DC_BezierCurve *curve, int segment, double t,
                     double *out_x, double *out_y)
{
    if (!curve || !out_x || !out_y) return -1;

    int n = dc_bezier_curve_knot_count(curve);
    if (n < 2) return -1;
    if (segment < 0 || segment >= n - 1) return -1;

    DC_BezierKnot *k0 = dc_array_get((DC_Array *)curve->knots,
                                      (size_t)segment);
    DC_BezierKnot *k1 = dc_array_get((DC_Array *)curve->knots,
                                      (size_t)(segment + 1));

    eval_cubic(k0->x, k0->y, k0->hnx, k0->hny,
               k1->hpx, k1->hpy, k1->x, k1->y,
               t, out_x, out_y);

    return 0;
}

/* -------------------------------------------------------------------------
 * Adaptive subdivision for tessellation (static helper)
 * ---------------------------------------------------------------------- */

static void
subdivide(double p0x, double p0y, double p1x, double p1y,
          double p2x, double p2y, double p3x, double p3y,
          double tolerance, DC_Array *out, int depth)
{
    /* Flatness test: distance from curve midpoint to chord midpoint */
    double mx, my;
    eval_cubic(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y, 0.5, &mx, &my);

    double cx = (p0x + p3x) * 0.5;
    double cy = (p0y + p3y) * 0.5;
    double dx = mx - cx;
    double dy = my - cy;
    double dist = sqrt(dx * dx + dy * dy);

    if (dist <= tolerance || depth >= DC_SUBDIVIDE_MAX_DEPTH) {
        DC_Point2 pt;
        pt.x = p3x;
        pt.y = p3y;
        dc_array_push(out, &pt);
        return;
    }

    /* Split at t=0.5 using De Casteljau */
    double q0x = (p0x + p1x) * 0.5;
    double q0y = (p0y + p1y) * 0.5;
    double q1x = (p1x + p2x) * 0.5;
    double q1y = (p1y + p2y) * 0.5;
    double q2x = (p2x + p3x) * 0.5;
    double q2y = (p2y + p3y) * 0.5;

    double r0x = (q0x + q1x) * 0.5;
    double r0y = (q0y + q1y) * 0.5;
    double r1x = (q1x + q2x) * 0.5;
    double r1y = (q1y + q2y) * 0.5;

    double sx = (r0x + r1x) * 0.5;
    double sy = (r0y + r1y) * 0.5;

    subdivide(p0x, p0y, q0x, q0y, r0x, r0y, sx, sy,
              tolerance, out, depth + 1);
    subdivide(sx, sy, r1x, r1y, q2x, q2y, p3x, p3y,
              tolerance, out, depth + 1);
}

/* -------------------------------------------------------------------------
 * Tessellation (public)
 * ---------------------------------------------------------------------- */

int
dc_bezier_curve_polyline(const DC_BezierCurve *curve, double tolerance,
                         DC_Array *out)
{
    if (!curve || !out || tolerance <= 0.0) return -1;

    int n = dc_bezier_curve_knot_count(curve);
    if (n < 2) return -1;

    /* Push the starting point */
    DC_BezierKnot *k0 = dc_array_get((DC_Array *)curve->knots, 0);
    DC_Point2 start;
    start.x = k0->x;
    start.y = k0->y;
    dc_array_push(out, &start);

    /* Tessellate each segment */
    for (int i = 0; i < n - 1; i++) {
        DC_BezierKnot *ka = dc_array_get((DC_Array *)curve->knots,
                                          (size_t)i);
        DC_BezierKnot *kb = dc_array_get((DC_Array *)curve->knots,
                                          (size_t)(i + 1));

        subdivide(ka->x, ka->y, ka->hnx, ka->hny,
                  kb->hpx, kb->hpy, kb->x, kb->y,
                  tolerance, out, 0);
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Bounding box (public)
 * ---------------------------------------------------------------------- */

int
dc_bezier_curve_bounds(const DC_BezierCurve *curve,
                       double *min_x, double *min_y,
                       double *max_x, double *max_y)
{
    if (!curve || !min_x || !min_y || !max_x || !max_y) return -1;

    int n = dc_bezier_curve_knot_count(curve);
    if (n < 1) return -1;

    DC_BezierKnot *k = dc_array_get((DC_Array *)curve->knots, 0);
    double lo_x = k->x, hi_x = k->x;
    double lo_y = k->y, hi_y = k->y;

    for (int i = 0; i < n; i++) {
        k = dc_array_get((DC_Array *)curve->knots, (size_t)i);

        if (k->x   < lo_x) lo_x = k->x;
        if (k->x   > hi_x) hi_x = k->x;
        if (k->y   < lo_y) lo_y = k->y;
        if (k->y   > hi_y) hi_y = k->y;

        if (k->hpx < lo_x) lo_x = k->hpx;
        if (k->hpx > hi_x) hi_x = k->hpx;
        if (k->hpy < lo_y) lo_y = k->hpy;
        if (k->hpy > hi_y) hi_y = k->hpy;

        if (k->hnx < lo_x) lo_x = k->hnx;
        if (k->hnx > hi_x) hi_x = k->hnx;
        if (k->hny < lo_y) lo_y = k->hny;
        if (k->hny > hi_y) hi_y = k->hny;
    }

    *min_x = lo_x;
    *min_y = lo_y;
    *max_x = hi_x;
    *max_y = hi_y;

    return 0;
}
