/*
 * ts_extrude.h — Extrusion operations (STUBS)
 *
 * Generates 3D meshes from 2D profiles via extrusion.
 *
 * GPU parallelization plan:
 *   Linear extrude: each slice is independent (parallel per-slice)
 *   Rotate extrude: each angular step is independent (parallel per-step)
 *   Both: vertex generation embarrassingly parallel,
 *          index generation sequential but trivial pattern.
 *
 * Status: STUB — interfaces locked, implementation pending.
 *
 * OpenSCAD equivalents:
 *   linear_extrude(height, twist, slices, scale)
 *   rotate_extrude(angle, $fn)
 */
#ifndef TS_EXTRUDE_H
#define TS_EXTRUDE_H

#define TS_EXTRUDE_NOT_IMPLEMENTED -98

/*
 * Linear extrusion parameters.
 *
 * profile_xy:  2D points (x,y pairs), closed polygon
 * n_points:    number of 2D points
 * height:      extrusion height along Z
 * twist:       total twist in degrees over the height
 * slices:      number of intermediate slices (more = smoother twist)
 * scale_top:   scale factor at the top (1.0 = no taper)
 * center:      if true, center vertically on Z=0
 *
 * GPU: each slice's vertices are independent. For N slices and M profile
 * points, we compute N*M vertices in parallel. That's the main win.
 */
static inline int ts_linear_extrude(const double *profile_xy, int n_points,
                                    double height, double twist, int slices,
                                    double scale_top, int center,
                                    ts_mesh *out) {
    (void)profile_xy; (void)n_points; (void)height; (void)twist;
    (void)slices; (void)scale_top; (void)center; (void)out;
    return TS_EXTRUDE_NOT_IMPLEMENTED;
}

/*
 * Rotational extrusion parameters.
 *
 * profile_xy:  2D points (x,y pairs) — the profile to revolve
 * n_points:    number of 2D points
 * angle:       revolution angle in degrees (360 = full revolution)
 * fn:          number of angular steps
 *
 * GPU: each angular step's vertices are independent.
 * For N steps and M profile points, N*M parallel vertex computations.
 */
static inline int ts_rotate_extrude(const double *profile_xy, int n_points,
                                    double angle, int fn,
                                    ts_mesh *out) {
    (void)profile_xy; (void)n_points; (void)angle; (void)fn; (void)out;
    return TS_EXTRUDE_NOT_IMPLEMENTED;
}

#endif /* TS_EXTRUDE_H */
