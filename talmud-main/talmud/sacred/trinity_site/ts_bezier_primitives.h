/*
 * ts_bezier_primitives.h — Analytically correct bezier mesh primitives
 *
 * Converts analytical patch arrays (ts_bezier_sphere, ts_bezier_torus)
 * into the shared-CP grid format (ts_bezier_mesh). Also provides
 * direct analytical constructors for box and cylinder.
 *
 * These produce mathematically correct geometry — no ad-hoc CP placement.
 *
 * Depends on: ts_bezier_surface.h, ts_bezier_mesh.h
 */
#ifndef TS_BEZIER_PRIMITIVES_H
#define TS_BEZIER_PRIMITIVES_H

#include "ts_bezier_surface.h"
#include "ts_bezier_mesh.h"
#include <math.h>

/* =========================================================================
 * Sphere → Mesh (2×3 grid: equatorial belt, top pole, bottom pole)
 *
 * Layout: 2 rows × 3 columns = 6 patches.
 * Row 0: top hemisphere (equator to north pole), 3 patches at 120° each
 * Row 1: bottom hemisphere (south pole to equator), 3 patches at 120° each
 *
 * CP grid is (5 rows × 7 cols).
 * Row 0 (cp_row=0): equator (shared between top and bottom hemispheres)
 * Row 2 (cp_row=2): mid-latitude (45° north)
 * Row 4 (cp_row=4): north pole (all CPs at pole)
 * Bottom half mirrors through equator.
 *
 * CPs are projected onto the sphere surface. Middle CPs on circular arcs
 * are pushed outward by 1/cos(half_arc) to compensate for bezier droop.
 * ========================================================================= */
static inline ts_bezier_mesh ts_bezier_mesh_from_sphere(const ts_bezier_sphere *s) {
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 3);
    double r = s->radius;
    ts_vec3 c = s->center;

    double dtheta = 2.0 * M_PI / 3.0; /* 120° per column */
    double half_arc = dtheta * 0.5;   /* 60° */
    /* Optimal quadratic bezier circle factor: places t=0.5 exactly on circle.
     * For arc of half-angle α: mid_factor = 2 - cos(α).
     * 120° arc (α=60°): 2 - cos(60°) = 2 - 0.5 = 1.5
     * 90° arc (α=45°):  2 - cos(45°) = 2 - √2/2 ≈ 1.293 */
    double mid_factor = 2.0 - cos(half_arc);

    double elev_mid = M_PI / 4.0;   /* 45° — midpoint of pole↔equator arc */
    double elev_mid_factor = 2.0 - cos(elev_mid);

    for (int pc = 0; pc < 3; pc++) {
        double theta0 = dtheta * (double)pc;
        double theta_mid = theta0 + half_arc;
        double theta1 = theta0 + dtheta;

        double c0t = cos(theta0),  s0t = sin(theta0);
        double cmt = cos(theta_mid), smt = sin(theta_mid);
        double c1t = cos(theta1), s1t = sin(theta1);

        int bc = 2 * pc;

        /* --- Equator (cp_row 0, shared between row 0 and row 1) --- */
        /* These are actually cp_row 2 in the bottom hemisphere (row 1 goes: pole→mid→equator)
         * Layout: row 1 bottom half goes from cp_row 2 to 4, but with
         * row 1 patch starting at cp_row 2 (equator at cp_row 2, pole at cp_row 4) */
        /* Actually, let's use:
         *   cp_row 0: north pole
         *   cp_row 1: mid-latitude (top)
         *   cp_row 2: equator (SHARED between patch row 0 and 1)
         *   cp_row 3: mid-latitude (bottom)
         *   cp_row 4: south pole */

        /* Edge CPs (on sphere surface at the azimuthal angles) */
        /* Mid CPs pushed outward for circle approximation */

        /* --- North pole (cp_row 0) --- */
        ts_bezier_mesh_set_cp(&m, 0, bc,     ts_vec3_add(c, ts_vec3_make(0, 0, r)));
        ts_bezier_mesh_set_cp(&m, 0, bc + 1, ts_vec3_add(c, ts_vec3_make(0, 0, r)));
        ts_bezier_mesh_set_cp(&m, 0, bc + 2, ts_vec3_add(c, ts_vec3_make(0, 0, r)));

        /* --- Mid-latitude top (cp_row 1, ~45° north) --- */
        /* Mid-latitude CPs are pushed outward from the sphere surface
         * by elev_mid_factor so the bezier arc passes through the sphere
         * at t=0.5. Factor = 2 - cos(45°) ≈ 1.293. */
        double mz_on = r * sin(elev_mid);      /* z on sphere at 45° */
        double mr_on = r * cos(elev_mid);       /* xy-radius on sphere at 45° */
        double mr_cp = mr_on * elev_mid_factor;
        double mz_cp = mz_on * elev_mid_factor;
        /* Azimuthal mid CP also pushed for circle approximation */
        double mr_mid_cp = mr_cp * mid_factor;
        ts_bezier_mesh_set_cp(&m, 1, bc,
            ts_vec3_add(c, ts_vec3_make(mr_cp * c0t, mr_cp * s0t, mz_cp)));
        ts_bezier_mesh_set_cp(&m, 1, bc + 1,
            ts_vec3_add(c, ts_vec3_make(mr_mid_cp * cmt, mr_mid_cp * smt, mz_cp)));
        ts_bezier_mesh_set_cp(&m, 1, bc + 2,
            ts_vec3_add(c, ts_vec3_make(mr_cp * c1t, mr_cp * s1t, mz_cp)));

        /* --- Equator (cp_row 2, shared) --- */
        double eq_mid_r = r * mid_factor;
        ts_bezier_mesh_set_cp(&m, 2, bc,
            ts_vec3_add(c, ts_vec3_make(r * c0t, r * s0t, 0)));
        ts_bezier_mesh_set_cp(&m, 2, bc + 1,
            ts_vec3_add(c, ts_vec3_make(eq_mid_r * cmt, eq_mid_r * smt, 0)));
        ts_bezier_mesh_set_cp(&m, 2, bc + 2,
            ts_vec3_add(c, ts_vec3_make(r * c1t, r * s1t, 0)));

        /* --- Mid-latitude bottom (cp_row 3, ~45° south) --- */
        ts_bezier_mesh_set_cp(&m, 3, bc,
            ts_vec3_add(c, ts_vec3_make(mr_cp * c0t, mr_cp * s0t, -mz_cp)));
        ts_bezier_mesh_set_cp(&m, 3, bc + 1,
            ts_vec3_add(c, ts_vec3_make(mr_mid_cp * cmt, mr_mid_cp * smt, -mz_cp)));
        ts_bezier_mesh_set_cp(&m, 3, bc + 2,
            ts_vec3_add(c, ts_vec3_make(mr_cp * c1t, mr_cp * s1t, -mz_cp)));

        /* --- South pole (cp_row 4) --- */
        ts_bezier_mesh_set_cp(&m, 4, bc,     ts_vec3_add(c, ts_vec3_make(0, 0, -r)));
        ts_bezier_mesh_set_cp(&m, 4, bc + 1, ts_vec3_add(c, ts_vec3_make(0, 0, -r)));
        ts_bezier_mesh_set_cp(&m, 4, bc + 2, ts_vec3_add(c, ts_vec3_make(0, 0, -r)));
    }

    return m;
}

/* =========================================================================
 * Torus → Mesh (rows×cols grid)
 *
 * The ts_bezier_torus has rows×cols patches. Map directly to mesh grid.
 * Note: the torus wraps in both directions, but ts_bezier_mesh is
 * open-boundary. We copy CPs from the patch array; boundary CPs that
 * should wrap will be duplicated (same position, separate storage).
 * This gives C0 continuity at the seam.
 * ========================================================================= */
static inline ts_bezier_mesh ts_bezier_mesh_from_torus(const ts_bezier_torus *t) {
    ts_bezier_mesh m = ts_bezier_mesh_new(t->rows, t->cols);

    for (int pr = 0; pr < t->rows; pr++) {
        for (int pc = 0; pc < t->cols; pc++) {
            const ts_bezier_patch *face = &t->faces[pr * t->cols + pc];

            int base_r = 2 * pr;
            int base_c = 2 * pc;

            for (int j = 0; j < 3; j++) {
                for (int i = 0; i < 3; i++) {
                    ts_bezier_mesh_set_cp(&m, base_r + j, base_c + i,
                                          face->cp[j][i]);
                }
            }
        }
    }

    return m;
}

/* =========================================================================
 * Box → Mesh (2×3 grid of flat patches)
 *
 * 6 flat quadratic patches (all CPs coplanar on each face).
 * Same 2×3 layout as the sphere:
 *   Row 0: +Z top, +X right, +Y back
 *   Row 1: -Y front, -Z bottom, -X left
 *
 * Each face is a degenerate (flat) quadratic patch — the 9 CPs are
 * evenly spaced across the face rectangle. This gives exact box geometry.
 * ========================================================================= */
static inline ts_bezier_mesh ts_bezier_mesh_from_box(ts_vec3 min_corner,
                                                      ts_vec3 max_corner) {
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 3);

    double x0 = min_corner.v[0], y0 = min_corner.v[1], z0 = min_corner.v[2];
    double x1 = max_corner.v[0], y1 = max_corner.v[1], z1 = max_corner.v[2];
    (void)min_corner; (void)max_corner; /* used via x0..z1 above */

    /* Helper: set a 3×3 grid of CPs for a flat face patch.
     * The face is parameterized by two corner axes (u_axis, v_axis).
     * corner00..corner11 are the 4 corners; we interpolate the 9 CPs. */
    #define SET_FACE_PATCH(pr, pc, c00, c10, c01, c11) do {                   \
        int br = 2 * (pr), bc = 2 * (pc);                                     \
        ts_vec3 corners[2][2] = { { (c00), (c10) }, { (c01), (c11) } };       \
        for (int jj = 0; jj < 3; jj++) {                                      \
            double vv = (double)jj * 0.5;                                      \
            for (int ii = 0; ii < 3; ii++) {                                   \
                double uu = (double)ii * 0.5;                                  \
                /* Bilinear interpolation of corners */                        \
                ts_vec3 p = ts_vec3_add(                                       \
                    ts_vec3_add(                                               \
                        ts_vec3_scale(corners[0][0], (1.0-uu)*(1.0-vv)),       \
                        ts_vec3_scale(corners[1][0], (1.0-uu)*vv)),            \
                    ts_vec3_add(                                               \
                        ts_vec3_scale(corners[0][1], uu*(1.0-vv)),             \
                        ts_vec3_scale(corners[1][1], uu*vv)));                 \
                ts_bezier_mesh_set_cp(&m, br + jj, bc + ii, p);               \
            }                                                                  \
        }                                                                      \
    } while(0)

    /* Face layout: Row 0 = +Z, +X, +Y faces; Row 1 = -Y, -Z, -X faces */
    /* +Z (top) face: z=z1, u=x, v=y */
    SET_FACE_PATCH(0, 0,
        ts_vec3_make(x0, y0, z1), ts_vec3_make(x1, y0, z1),
        ts_vec3_make(x0, y1, z1), ts_vec3_make(x1, y1, z1));

    /* +X (right) face: x=x1, u=y, v=z */
    SET_FACE_PATCH(0, 1,
        ts_vec3_make(x1, y0, z0), ts_vec3_make(x1, y1, z0),
        ts_vec3_make(x1, y0, z1), ts_vec3_make(x1, y1, z1));

    /* +Y (back) face: y=y1, u=x, v=z */
    SET_FACE_PATCH(0, 2,
        ts_vec3_make(x0, y1, z0), ts_vec3_make(x1, y1, z0),
        ts_vec3_make(x0, y1, z1), ts_vec3_make(x1, y1, z1));

    /* -Y (front) face: y=y0, u=x (reversed), v=z */
    SET_FACE_PATCH(1, 0,
        ts_vec3_make(x1, y0, z0), ts_vec3_make(x0, y0, z0),
        ts_vec3_make(x1, y0, z1), ts_vec3_make(x0, y0, z1));

    /* -Z (bottom) face: z=z0, u=x, v=y (reversed) */
    SET_FACE_PATCH(1, 1,
        ts_vec3_make(x0, y1, z0), ts_vec3_make(x1, y1, z0),
        ts_vec3_make(x0, y0, z0), ts_vec3_make(x1, y0, z0));

    /* -X (left) face: x=x0, u=y (reversed), v=z */
    SET_FACE_PATCH(1, 2,
        ts_vec3_make(x0, y1, z0), ts_vec3_make(x0, y0, z0),
        ts_vec3_make(x0, y1, z1), ts_vec3_make(x0, y0, z1));

    #undef SET_FACE_PATCH

    return m;
}

/* =========================================================================
 * Cylinder → Mesh (2×segments grid + 2 cap rows = 4×segments)
 *
 * Body: 2 rows of patches around the circumference (top half, bottom half)
 * Caps: 1 row of degenerate patches for top cap, 1 for bottom cap
 * Total: 4 rows, `segments` columns
 *
 * For a quadratic bezier to approximate a circular arc, the middle CP
 * must be placed at the intersection of tangent lines at the endpoints.
 * For an arc of half-angle α: mid_CP = endpoint / cos(α).
 * ========================================================================= */
static inline ts_bezier_mesh ts_bezier_mesh_from_cylinder(double radius,
                                                           double z0, double z1,
                                                           int segments) {
    if (segments < 3) segments = 3;
    if (segments > 64) segments = 64;

    /* 4 rows: [0]=top cap, [1]=top body, [2]=bottom body, [3]=bottom cap */
    ts_bezier_mesh m = ts_bezier_mesh_new(4, segments);

    double zmid = (z0 + z1) * 0.5;
    double dtheta = 2.0 * M_PI / (double)segments;
    double half_arc = dtheta * 0.5;
    /* For quadratic bezier circle approximation:
     * mid CP radius = radius / cos(half_arc) to hit the tangent intersection */
    double mid_r = radius / cos(half_arc);

    for (int seg = 0; seg < segments; seg++) {
        double theta0 = dtheta * (double)seg;
        double theta_mid = theta0 + half_arc;
        double theta1 = theta0 + dtheta;

        double c0 = cos(theta0), s0 = sin(theta0);
        double cm = cos(theta_mid), sm = sin(theta_mid);
        double c1 = cos(theta1), s1 = sin(theta1);

        /* Edge points (on circle) */
        double ex0 = radius * c0, ey0 = radius * s0;
        double ex1 = radius * c1, ey1 = radius * s1;
        /* Mid control point (off circle, at tangent intersection) */
        double mx = mid_r * cm, my = mid_r * sm;

        int bc = 2 * seg;

        /* --- Top cap (row 0): center → edge at z=z1 --- */
        /* CP row 0: center of top cap */
        ts_bezier_mesh_set_cp(&m, 0, bc,     ts_vec3_make(0, 0, z1));
        ts_bezier_mesh_set_cp(&m, 0, bc + 1, ts_vec3_make(0, 0, z1));
        ts_bezier_mesh_set_cp(&m, 0, bc + 2, ts_vec3_make(0, 0, z1));
        /* CP row 1: halfway to edge */
        ts_bezier_mesh_set_cp(&m, 1, bc,     ts_vec3_make(ex0*0.5, ey0*0.5, z1));
        ts_bezier_mesh_set_cp(&m, 1, bc + 1, ts_vec3_make(mx*0.5,  my*0.5,  z1));
        ts_bezier_mesh_set_cp(&m, 1, bc + 2, ts_vec3_make(ex1*0.5, ey1*0.5, z1));
        /* CP row 2: on edge at z=z1 (shared with body top) */
        ts_bezier_mesh_set_cp(&m, 2, bc,     ts_vec3_make(ex0, ey0, z1));
        ts_bezier_mesh_set_cp(&m, 2, bc + 1, ts_vec3_make(mx,  my,  z1));
        ts_bezier_mesh_set_cp(&m, 2, bc + 2, ts_vec3_make(ex1, ey1, z1));

        /* --- Top body (row 1): z1 → zmid --- */
        /* CP row 2: shared with top cap (already set above) */
        /* CP row 3: on wall at z midpoint between z1 and zmid */
        double z_q1 = (z1 + zmid) * 0.5;
        ts_bezier_mesh_set_cp(&m, 3, bc,     ts_vec3_make(ex0, ey0, z_q1));
        ts_bezier_mesh_set_cp(&m, 3, bc + 1, ts_vec3_make(mx,  my,  z_q1));
        ts_bezier_mesh_set_cp(&m, 3, bc + 2, ts_vec3_make(ex1, ey1, z_q1));
        /* CP row 4: on wall at zmid (shared with bottom body) */
        ts_bezier_mesh_set_cp(&m, 4, bc,     ts_vec3_make(ex0, ey0, zmid));
        ts_bezier_mesh_set_cp(&m, 4, bc + 1, ts_vec3_make(mx,  my,  zmid));
        ts_bezier_mesh_set_cp(&m, 4, bc + 2, ts_vec3_make(ex1, ey1, zmid));

        /* --- Bottom body (row 2): zmid → z0 --- */
        /* CP row 4: shared with top body (already set above) */
        double z_q2 = (zmid + z0) * 0.5;
        ts_bezier_mesh_set_cp(&m, 5, bc,     ts_vec3_make(ex0, ey0, z_q2));
        ts_bezier_mesh_set_cp(&m, 5, bc + 1, ts_vec3_make(mx,  my,  z_q2));
        ts_bezier_mesh_set_cp(&m, 5, bc + 2, ts_vec3_make(ex1, ey1, z_q2));
        /* CP row 6: on edge at z=z0 (shared with bottom cap) */
        ts_bezier_mesh_set_cp(&m, 6, bc,     ts_vec3_make(ex0, ey0, z0));
        ts_bezier_mesh_set_cp(&m, 6, bc + 1, ts_vec3_make(mx,  my,  z0));
        ts_bezier_mesh_set_cp(&m, 6, bc + 2, ts_vec3_make(ex1, ey1, z0));

        /* --- Bottom cap (row 3): edge → center at z=z0 --- */
        /* CP row 6: shared with body bottom (already set above) */
        ts_bezier_mesh_set_cp(&m, 7, bc,     ts_vec3_make(ex0*0.5, ey0*0.5, z0));
        ts_bezier_mesh_set_cp(&m, 7, bc + 1, ts_vec3_make(mx*0.5,  my*0.5,  z0));
        ts_bezier_mesh_set_cp(&m, 7, bc + 2, ts_vec3_make(ex1*0.5, ey1*0.5, z0));
        /* CP row 8: center of bottom cap */
        ts_bezier_mesh_set_cp(&m, 8, bc,     ts_vec3_make(0, 0, z0));
        ts_bezier_mesh_set_cp(&m, 8, bc + 1, ts_vec3_make(0, 0, z0));
        ts_bezier_mesh_set_cp(&m, 8, bc + 2, ts_vec3_make(0, 0, z0));
    }

    return m;
}

#endif /* TS_BEZIER_PRIMITIVES_H */
