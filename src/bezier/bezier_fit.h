#ifndef DC_BEZIER_FIT_H
#define DC_BEZIER_FIT_H

/*
 * bezier_fit.h — Schneider curve fitting algorithm.
 *
 * Fits a sequence of 2D sample points to a cubic bezier spline using
 * the algorithm from Graphics Gems I (Philip Schneider, 1990).
 *
 * Pure math — no GTK dependency. Links into dc_core.
 *
 * Output is in the editor's alternating format:
 *   out_pts:       [P0, C0, C1, P1, C0, C1, P2, ...]
 *   out_junctures: [1,  0,  0,  1,  0,  0,  ..., 1]
 */

#include "bezier/bezier_curve.h"   /* DC_Point2 */
#include "core/array.h"
#include "core/error.h"

#include <stdbool.h>

/* Fit a cubic bezier spline to the given sample points.
 *
 * Parameters:
 *   points    — array of DC_Point2 sample points (at least 2)
 *   n         — number of points
 *   error_tol — maximum allowed squared distance from any sample to curve
 *   out_pts   — output: DC_Array of DC_Point2 in alternating format
 *   out_junctures — output: DC_Array of uint8_t, parallel to out_pts
 *   err       — optional error output (may be NULL)
 *
 * Returns true on success, false on error. */
bool dc_bezier_fit(const DC_Point2 *points, int n,
                   double error_tol,
                   DC_Array *out_pts,
                   DC_Array *out_junctures,
                   DC_Error *err);

#endif /* DC_BEZIER_FIT_H */
