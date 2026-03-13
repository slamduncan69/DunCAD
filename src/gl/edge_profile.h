/*
 * edge_profile.h — Edge loop geometry analysis for bezier editing.
 *
 * Pure C, no GTK dependency. Analyzes 2D polygon points from a face
 * boundary or edge loop and detects shape type (circle, rectangle,
 * freeform). Generates bezier editor control points for the detected shape.
 *
 * Usage:
 *   DC_EdgeProfile prof;
 *   dc_edge_profile_analyze(pts2d, count, &prof);
 *   // prof.type tells you what shape it is
 *   // prof.circle.cx, .cy, .radius for circles
 *   // Use dc_edge_profile_to_bezier() to get editor points
 */
#ifndef DC_EDGE_PROFILE_H
#define DC_EDGE_PROFILE_H

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * Shape types
 * ========================================================================= */

typedef enum {
    DC_PROFILE_UNKNOWN  = 0,
    DC_PROFILE_CIRCLE   = 1,
    DC_PROFILE_RECT     = 2,
    DC_PROFILE_FREEFORM = 3,
} DC_ProfileType;

typedef struct {
    DC_ProfileType type;
    /* Circle params (valid when type == DC_PROFILE_CIRCLE) */
    struct {
        double cx, cy;      /* center */
        double radius;      /* average radius */
        double fit_error;   /* max deviation from perfect circle (0-1 relative) */
    } circle;
    /* Rect params (valid when type == DC_PROFILE_RECT) */
    struct {
        double cx, cy;      /* center */
        double width, height;
        double angle;       /* rotation angle (degrees) */
        double fit_error;
    } rect;
} DC_EdgeProfile;

/* =========================================================================
 * Analysis
 * ========================================================================= */

/* Analyze a 2D polygon (from face boundary extraction).
 * pts: array of [x,y] pairs (flat float array, count vertices).
 * Returns 0 on success, -1 on failure. */
static int
dc_edge_profile_analyze(const float *pts, int count, DC_EdgeProfile *out)
{
    if (!pts || count < 3 || !out) return -1;
    memset(out, 0, sizeof(*out));

    /* Compute centroid */
    double cx = 0, cy = 0;
    for (int i = 0; i < count; i++) {
        cx += pts[i*2+0];
        cy += pts[i*2+1];
    }
    cx /= count;
    cy /= count;

    /* Compute distances from centroid */
    double *dists = (double *)malloc((size_t)count * sizeof(double));
    if (!dists) return -1;

    double avg_r = 0, min_r = 1e30, max_r = 0;
    for (int i = 0; i < count; i++) {
        double dx = pts[i*2+0] - cx;
        double dy = pts[i*2+1] - cy;
        dists[i] = sqrt(dx*dx + dy*dy);
        avg_r += dists[i];
        if (dists[i] < min_r) min_r = dists[i];
        if (dists[i] > max_r) max_r = dists[i];
    }
    avg_r /= count;

    /* Circle test: max deviation from average radius / avg_radius */
    double max_dev = 0;
    for (int i = 0; i < count; i++) {
        double dev = fabs(dists[i] - avg_r) / avg_r;
        if (dev > max_dev) max_dev = dev;
    }
    free(dists);

    if (max_dev < 0.05 && count >= 8) {
        /* Circle: all points within 5% of average radius, enough points */
        out->type = DC_PROFILE_CIRCLE;
        out->circle.cx = cx;
        out->circle.cy = cy;
        out->circle.radius = avg_r;
        out->circle.fit_error = max_dev;
        return 0;
    }

    /* Rectangle test: check if points cluster around 4 distinct angles */
    /* For now, classify as freeform if not circle */
    /* TODO: implement rectangle detection */

    out->type = DC_PROFILE_FREEFORM;
    return 0;
}

/* =========================================================================
 * Bezier generation
 * ========================================================================= */

/* Generate bezier editor points for a circle.
 * A circle is approximated by 4 cubic bezier segments. The editor uses
 * chained quadratic format: [P0, C1, P2, C3, P4, C5, P6, C7]
 * where even=on-curve, odd=off-curve.
 *
 * For a proper cubic circle approximation, the magic constant is
 * k = 4*(sqrt(2)-1)/3 ≈ 0.5522847. The control point is at distance
 * k*r from the on-curve point, perpendicular to the radius.
 *
 * However, the editor uses QUADRATIC segments (3 points per segment),
 * not cubic. For a quadratic approximation of a quarter-circle, the
 * control point is at (r, r*tan(pi/4)) = (r, r) from center, which
 * gives a poor approximation. Better to use 8 segments (octants).
 *
 * Returns: malloc'd array of DC_Point2 with *out_count points.
 * The curve should be loaded as closed. Caller frees. */

typedef struct { double x, y; } DC_EP_Point;

/* Generate points for a circle using N_SEG quadratic bezier segments.
 * Each segment spans 360/N_SEG degrees. On-curve points are at the
 * segment boundaries, off-curve control points are computed to best
 * approximate the arc. */
static DC_EP_Point *
dc_edge_profile_circle_bezier(double cx, double cy, double radius,
                               int n_seg, int *out_count)
{
    if (n_seg < 4) n_seg = 4;
    /* n_seg segments → n_seg on-curve + n_seg off-curve = 2*n_seg points */
    int np = n_seg * 2;
    DC_EP_Point *pts = (DC_EP_Point *)malloc((size_t)np * sizeof(DC_EP_Point));
    if (!pts) return NULL;

    double da = 2.0 * M_PI / n_seg;

    for (int i = 0; i < n_seg; i++) {
        double a0 = i * da;
        double a1 = (i + 1) * da;
        double amid = (a0 + a1) / 2.0;

        /* On-curve point (even index) at angle a0 */
        pts[i*2].x = cx + radius * cos(a0);
        pts[i*2].y = cy + radius * sin(a0);

        /* Off-curve control point (odd index):
         * For a quadratic bezier to best fit a circular arc from a0 to a1,
         * the control point should be at the intersection of the tangent
         * lines at the two endpoints. For a unit circle:
         *   P0 = (cos(a0), sin(a0))
         *   P2 = (cos(a1), sin(a1))
         *   Tangent at P0: (-sin(a0), cos(a0))
         *   Tangent at P2: (-sin(a1), cos(a1))
         * The intersection is:
         *   C = P0 + t * T0 where t = (1 - cos(da/2)) / sin(da/2)...
         * Simplified: C is at angle amid, distance = r / cos(da/2) */
        double r_ctrl = radius / cos(da / 2.0);
        pts[i*2+1].x = cx + r_ctrl * cos(amid);
        pts[i*2+1].y = cy + r_ctrl * sin(amid);
    }

    *out_count = np;
    return pts;
}

/* Generate points for a freeform polygon. Each edge of the polygon
 * becomes a quadratic segment with the control point at the midpoint
 * (linear interpolation, user can then drag controls to curve it).
 * Returns malloc'd array, caller frees. */
static DC_EP_Point *
dc_edge_profile_polygon_bezier(const float *poly_pts, int count,
                                int *out_count)
{
    if (!poly_pts || count < 3) return NULL;

    /* N vertices → N segments (closed) → 2*N points */
    int np = count * 2;
    DC_EP_Point *pts = (DC_EP_Point *)malloc((size_t)np * sizeof(DC_EP_Point));
    if (!pts) return NULL;

    for (int i = 0; i < count; i++) {
        int next = (i + 1) % count;

        /* On-curve point */
        pts[i*2].x = poly_pts[i*2+0];
        pts[i*2].y = poly_pts[i*2+1];

        /* Off-curve control: midpoint of edge (linear — user can curve it) */
        pts[i*2+1].x = (poly_pts[i*2+0] + poly_pts[next*2+0]) / 2.0;
        pts[i*2+1].y = (poly_pts[i*2+1] + poly_pts[next*2+1]) / 2.0;
    }

    *out_count = np;
    return pts;
}

#endif /* DC_EDGE_PROFILE_H */
