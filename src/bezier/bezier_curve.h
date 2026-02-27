#ifndef DC_BEZIER_CURVE_H
#define DC_BEZIER_CURVE_H

/*
 * bezier_curve.h — Cubic bezier spline data model.
 *
 * Pure geometry with no GTK dependency. Stores an array of knots with
 * position, two handles (h_prev incoming, h_next outgoing), and a
 * continuity constraint per knot. Segment i is the cubic from knot[i]
 * to knot[i+1]; n knots = n-1 segments.
 *
 * Ownership:
 *   - DC_BezierCurve is opaque; create with dc_bezier_curve_new(),
 *     destroy with dc_bezier_curve_free().
 *   - dc_bezier_curve_get_knot() returns a borrowed interior pointer
 *     into the knots array. It is invalidated by any add_knot call
 *     that triggers reallocation.
 *
 * Return conventions: 0 = success, -1 = error (NULL args, out of
 * bounds, insufficient knots for evaluation).
 */

#include "core/array.h"

/* -------------------------------------------------------------------------
 * DC_Continuity — constraint between a knot's two handles
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_CONTINUITY_SMOOTH,    /* colinear handles, independent magnitude */
    DC_CONTINUITY_SYMMETRIC, /* colinear + equal magnitude */
    DC_CONTINUITY_CORNER     /* fully independent handles */
} DC_Continuity;

/* -------------------------------------------------------------------------
 * DC_BezierKnot — position + two handles + continuity
 *
 * Plain struct (not opaque) because the editor and numeric panel need
 * direct field access. Stored by value in DC_Array.
 * ---------------------------------------------------------------------- */
typedef struct {
    double x, y;             /* knot position */
    double hpx, hpy;         /* handle-prev (incoming) */
    double hnx, hny;         /* handle-next (outgoing) */
    DC_Continuity cont;      /* continuity constraint */
} DC_BezierKnot;

/* -------------------------------------------------------------------------
 * DC_Point2 — lightweight 2D point for tessellation output
 * ---------------------------------------------------------------------- */
typedef struct {
    double x, y;
} DC_Point2;

/* -------------------------------------------------------------------------
 * DC_BezierCurve — opaque handle; internals in bezier_curve.c
 * ---------------------------------------------------------------------- */
typedef struct DC_BezierCurve DC_BezierCurve;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/* Allocate a new empty curve. Returns NULL on allocation failure. */
DC_BezierCurve *dc_bezier_curve_new(void);

/* Free curve and all owned memory. NULL is a no-op. */
void dc_bezier_curve_free(DC_BezierCurve *curve);

/* -------------------------------------------------------------------------
 * Knot manipulation
 * ---------------------------------------------------------------------- */

/* Append a knot at (x,y). Handles default to coincident with position;
 * continuity defaults to DC_CONTINUITY_SMOOTH. Returns 0/-1. */
int dc_bezier_curve_add_knot(DC_BezierCurve *curve, double x, double y);

/* Number of knots (0 if curve is NULL). */
int dc_bezier_curve_knot_count(const DC_BezierCurve *curve);

/* Borrowed pointer to knot at index. Returns NULL if out of bounds. */
DC_BezierKnot *dc_bezier_curve_get_knot(DC_BezierCurve *curve, int index);

/* Set continuity constraint on knot at index. For SMOOTH and SYMMETRIC,
 * h_prev is adjusted to match h_next direction. Returns 0/-1. */
int dc_bezier_curve_set_continuity(DC_BezierCurve *curve, int index,
                                   DC_Continuity c);

/* Deep-copy a curve. Returns NULL if src is NULL or on allocation failure. */
DC_BezierCurve *dc_bezier_curve_clone(const DC_BezierCurve *src);

/* Remove the knot at index, shifting later knots left. Returns 0/-1. */
int dc_bezier_curve_remove_knot(DC_BezierCurve *curve, int index);

/* -------------------------------------------------------------------------
 * Evaluation
 * ---------------------------------------------------------------------- */

/* Evaluate the cubic at parameter t on the given segment using
 * De Casteljau. Requires >= 2 knots. Returns 0/-1. */
int dc_bezier_curve_eval(const DC_BezierCurve *curve, int segment, double t,
                         double *out_x, double *out_y);

/* -------------------------------------------------------------------------
 * Tessellation
 * ---------------------------------------------------------------------- */

/* Adaptively tessellate the entire curve into DC_Point2 values appended
 * to out. Subdivides until midpoint deviation from chord < tolerance.
 * Requires >= 2 knots. Returns 0/-1. */
int dc_bezier_curve_polyline(const DC_BezierCurve *curve, double tolerance,
                             DC_Array *out);

/* -------------------------------------------------------------------------
 * Bounding box
 * ---------------------------------------------------------------------- */

/* Compute axis-aligned bounding box from the control polygon hull
 * (all knot positions and handle positions). Requires >= 1 knot.
 * Returns 0/-1. */
int dc_bezier_curve_bounds(const DC_BezierCurve *curve,
                           double *min_x, double *min_y,
                           double *max_x, double *max_y);

#endif /* DC_BEZIER_CURVE_H */
