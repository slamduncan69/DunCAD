/*
 * ts_extrude.h — Extrusion operations
 *
 * Generates 3D meshes from 2D profiles via extrusion.
 *
 * GPU parallelization:
 *   Linear extrude: each slice is independent (parallel per-slice)
 *   Rotate extrude: each angular step is independent (parallel per-step)
 *   Both: vertex generation embarrassingly parallel,
 *          index generation sequential but trivial pattern.
 *
 * OpenSCAD equivalents:
 *   linear_extrude(height, twist, slices, scale) — ts_linear_extrude
 *   rotate_extrude(angle, $fn)                   — ts_rotate_extrude
 */
#ifndef TS_EXTRUDE_H
#define TS_EXTRUDE_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TS_EXTRUDE_OK    0
#define TS_EXTRUDE_ERROR -1

/* ================================================================
 * EAR-CLIPPING TRIANGULATION for 2D polygon caps
 *
 * Simple O(n^2) ear-clipping. Sufficient for typical profiles (<100 pts).
 * GPU: not worth offloading (small N), but the extrusion itself is parallel.
 * ================================================================ */

/* Signed area of 2D triangle (positive = CCW) */
static inline double ts_ext_tri_area2(double ax, double ay,
                                       double bx, double by,
                                       double cx, double cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

/* Check if point p is inside triangle abc (assumes CCW winding) */
static inline int ts_ext_point_in_tri(double px, double py,
                                       double ax, double ay,
                                       double bx, double by,
                                       double cx, double cy) {
    double d1 = ts_ext_tri_area2(px, py, ax, ay, bx, by);
    double d2 = ts_ext_tri_area2(px, py, bx, by, cx, cy);
    double d3 = ts_ext_tri_area2(px, py, cx, cy, ax, ay);
    int has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    int has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

/*
 * Ear-clip triangulate a 2D polygon.
 * points_xy: x,y pairs (n_points * 2 doubles)
 * tri_out: output triangle indices (must have space for (n_points-2)*3 ints)
 * Returns number of triangles, or -1 on error.
 */
static inline int ts_ext_triangulate(const double *points_xy, int n_points,
                                      int *tri_out) {
    if (n_points < 3) return -1;
    if (n_points == 3) {
        tri_out[0] = 0; tri_out[1] = 1; tri_out[2] = 2;
        return 1;
    }

    /* Compute winding (signed area) */
    double area = 0;
    for (int i = 0; i < n_points; i++) {
        int j = (i + 1) % n_points;
        area += points_xy[i*2] * points_xy[j*2+1];
        area -= points_xy[j*2] * points_xy[i*2+1];
    }
    int ccw = (area > 0) ? 1 : 0;

    /* Index list (we'll remove ears as we go) */
    int *idx = (int *)malloc((size_t)n_points * sizeof(int));
    if (!idx) return -1;
    for (int i = 0; i < n_points; i++) idx[i] = i;

    int remaining = n_points;
    int tri_count = 0;
    int max_iter = remaining * remaining; /* safety bound */

    int cur = 0;
    while (remaining > 2 && max_iter-- > 0) {
        int prev = (cur + remaining - 1) % remaining;
        int next = (cur + 1) % remaining;

        int ip = idx[prev], ic = idx[cur], in_ = idx[next];
        double ax = points_xy[ip*2], ay = points_xy[ip*2+1];
        double bx = points_xy[ic*2], by = points_xy[ic*2+1];
        double cx = points_xy[in_*2], cy = points_xy[in_*2+1];

        double cross = ts_ext_tri_area2(ax, ay, bx, by, cx, cy);
        int is_ear = ccw ? (cross > 1e-10) : (cross < -1e-10);

        if (is_ear) {
            /* Check no other vertex inside this triangle */
            int inside = 0;
            for (int k = 0; k < remaining; k++) {
                if (k == prev || k == cur || k == next) continue;
                int ik = idx[k];
                if (ts_ext_point_in_tri(points_xy[ik*2], points_xy[ik*2+1],
                                         ax, ay, bx, by, cx, cy)) {
                    inside = 1;
                    break;
                }
            }
            if (!inside) {
                /* Emit triangle */
                if (ccw) {
                    tri_out[tri_count*3+0] = ip;
                    tri_out[tri_count*3+1] = ic;
                    tri_out[tri_count*3+2] = in_;
                } else {
                    tri_out[tri_count*3+0] = ip;
                    tri_out[tri_count*3+1] = in_;
                    tri_out[tri_count*3+2] = ic;
                }
                tri_count++;

                /* Remove ear vertex */
                for (int k = cur; k < remaining - 1; k++)
                    idx[k] = idx[k+1];
                remaining--;
                if (cur >= remaining) cur = 0;
                continue;
            }
        }
        cur = (cur + 1) % remaining;
    }

    free(idx);
    return tri_count;
}

/* ================================================================
 * LINEAR EXTRUDE
 *
 * Extrudes a 2D profile along the Z axis.
 *
 * Parameters:
 *   profile_xy:  2D points (x,y pairs), closed polygon
 *   n_points:    number of 2D points
 *   height:      extrusion height along Z
 *   twist:       total twist in degrees over the height
 *   slices:      number of intermediate slices (more = smoother twist)
 *   scale_top:   scale factor at the top (1.0 = no taper)
 *   center:      if true, center vertically on Z=0
 *   out:         output mesh
 *
 * GPU: each slice's vertices are independent (N_slices * N_profile parallel).
 * For twist/taper, each vertex does sin/cos of its slice angle — still parallel.
 * ================================================================ */
static inline int ts_linear_extrude(const double *profile_xy, int n_points,
                                     double height, double twist, int slices,
                                     double scale_top, int center,
                                     ts_mesh *out) {
    if (!profile_xy || n_points < 3 || !out) return TS_EXTRUDE_ERROR;
    if (slices < 1) slices = 1;
    if (height <= 0) return TS_EXTRUDE_ERROR;

    int n_layers = slices + 1;
    double z_off = center ? -height * 0.5 : 0.0;
    double twist_rad = twist * (M_PI / 180.0);

    /* Reserve space: n_layers * n_points vertices for sides,
     * plus cap vertices. Triangles: slices * n_points * 2 (sides)
     * + (n_points - 2) * 2 (caps) */
    int side_verts = n_layers * n_points;
    int side_tris = slices * n_points * 2;
    int cap_tris = (n_points - 2) * 2;
    ts_mesh_reserve(out, out->vert_count + side_verts + n_points * 2,
                    out->tri_count + side_tris + cap_tris);

    int base = out->vert_count;

    /* Determine polygon winding (signed area):
     * CCW = positive area, CW = negative area.
     * Side faces must match cap winding for manifold mesh. */
    double poly_area = 0;
    for (int i = 0; i < n_points; i++) {
        int j = (i + 1) % n_points;
        poly_area += profile_xy[i*2] * profile_xy[j*2+1];
        poly_area -= profile_xy[j*2] * profile_xy[i*2+1];
    }
    int ccw = (poly_area > 0);

    /* Generate vertices for each layer — GPU: parallel per (layer, point) */
    for (int s = 0; s < n_layers; s++) {
        double t = (double)s / (double)slices;
        double z = z_off + height * t;
        double angle = twist_rad * t;
        double sc = 1.0 + (scale_top - 1.0) * t;  /* interpolate scale */
        double ca = cos(angle), sa = sin(angle);

        for (int p = 0; p < n_points; p++) {
            double px = profile_xy[p*2+0] * sc;
            double py = profile_xy[p*2+1] * sc;
            /* Apply twist rotation around Z */
            double rx = px * ca - py * sa;
            double ry = px * sa + py * ca;

            /* Approximate normal: cross of extrusion tangent with edge tangent */
            /* For now, use outward-facing XY normal; recompute after */
            ts_mesh_add_vertex(out, rx, ry, z, 0, 0, 0);
        }
    }

    /* Side faces: connect adjacent layers — GPU: parallel per (slice, segment)
     * Winding must match polygon direction for consistent normals.
     * CCW polygon → (bot+p, bot+pnext, top+pnext) faces outward
     * CW polygon → reversed winding faces outward */
    for (int s = 0; s < slices; s++) {
        for (int p = 0; p < n_points; p++) {
            int pnext = (p + 1) % n_points;
            int bot = base + s * n_points;
            int top = base + (s + 1) * n_points;

            if (ccw) {
                ts_mesh_add_triangle(out, bot + p, bot + pnext, top + pnext);
                ts_mesh_add_triangle(out, bot + p, top + pnext, top + p);
            } else {
                ts_mesh_add_triangle(out, bot + p, top + pnext, bot + pnext);
                ts_mesh_add_triangle(out, bot + p, top + p, top + pnext);
            }
        }
    }

    /* Bottom cap (z = z_off) — triangulate the profile */
    int *cap_indices = (int *)malloc((size_t)(n_points - 2) * 3 * sizeof(int));
    if (cap_indices) {
        int ntri = ts_ext_triangulate(profile_xy, n_points, cap_indices);
        if (ntri > 0) {
            /* Bottom cap: reverse winding so normal faces -Z */
            int bot_base = base;
            for (int i = 0; i < ntri; i++) {
                ts_mesh_add_triangle(out,
                    bot_base + cap_indices[i*3+0],
                    bot_base + cap_indices[i*3+2],
                    bot_base + cap_indices[i*3+1]);
            }
            /* Top cap: normal winding, faces +Z */
            int top_base = base + slices * n_points;
            for (int i = 0; i < ntri; i++) {
                ts_mesh_add_triangle(out,
                    top_base + cap_indices[i*3+0],
                    top_base + cap_indices[i*3+1],
                    top_base + cap_indices[i*3+2]);
            }
        }
        free(cap_indices);
    }

    /* Recompute normals from geometry */
    ts_mesh_compute_normals(out);

    return TS_EXTRUDE_OK;
}

/* ================================================================
 * ROTATE EXTRUDE
 *
 * Revolves a 2D profile (in XY plane, X>0) around the Y axis.
 * The profile is swept through `angle` degrees.
 *
 * Parameters:
 *   profile_xy:  2D points (x,y pairs) — X = radius, Y = height
 *   n_points:    number of 2D points
 *   angle:       revolution angle in degrees (360 = full revolution)
 *   fn:          number of angular steps
 *   out:         output mesh
 *
 * GPU: each angular step's vertices are independent.
 * For N steps and M profile points, N*M parallel vertex computations.
 * ================================================================ */
static inline int ts_rotate_extrude(const double *profile_xy, int n_points,
                                     double angle, int fn,
                                     ts_mesh *out) {
    if (!profile_xy || n_points < 2 || !out) return TS_EXTRUDE_ERROR;
    if (fn < 3) fn = 3;
    if (angle <= 0 || angle > 360.0) angle = 360.0;

    int full_rev = (fabs(angle - 360.0) < 1e-6);
    int n_steps = fn;
    int n_rings = full_rev ? fn : fn + 1;
    double angle_rad = angle * (M_PI / 180.0);

    /* Profile is a closed polygon: connect last point back to first.
     * Vertices: n_rings * n_points
     * Triangles: n_steps * n_points * 2 (side quads, including closing edge)
     * + caps if not full revolution */
    int total_verts = n_rings * n_points;
    int total_tris = n_steps * n_points * 2;
    if (!full_rev) total_tris += (n_points - 2) * 2; /* start and end caps */

    ts_mesh_reserve(out, out->vert_count + total_verts,
                    out->tri_count + total_tris);

    int base = out->vert_count;

    /* Determine polygon winding for consistent side face normals */
    double poly_area = 0;
    for (int i = 0; i < n_points; i++) {
        int j = (i + 1) % n_points;
        poly_area += profile_xy[i*2] * profile_xy[j*2+1];
        poly_area -= profile_xy[j*2] * profile_xy[i*2+1];
    }
    int ccw = (poly_area > 0);

    /* Generate vertices — GPU: parallel per (ring, profile_point) */
    for (int r = 0; r < n_rings; r++) {
        double t = (double)r / (double)n_steps;
        double theta = angle_rad * t;
        double ct = cos(theta), st = sin(theta);

        for (int p = 0; p < n_points; p++) {
            double px = profile_xy[p*2+0]; /* radius from axis */
            double py = profile_xy[p*2+1]; /* height (Y) */

            /* Revolve around Y axis: X,Z from radius, Y stays */
            double rx = px * ct;
            double rz = px * st;

            ts_mesh_add_vertex(out, rx, py, rz, 0, 0, 0);
        }
    }

    /* Side faces: connect adjacent rings.
     * Winding must match polygon direction for manifold mesh. */
    for (int r = 0; r < n_steps; r++) {
        int r0 = base + r * n_points;
        int r1 = full_rev ? (base + ((r + 1) % fn) * n_points)
                          : (base + (r + 1) * n_points);

        for (int p = 0; p < n_points; p++) {
            int pnext = (p + 1) % n_points;
            if (ccw) {
                ts_mesh_add_triangle(out, r0 + p, r1 + p, r1 + pnext);
                ts_mesh_add_triangle(out, r0 + p, r1 + pnext, r0 + pnext);
            } else {
                ts_mesh_add_triangle(out, r0 + p, r1 + pnext, r1 + p);
                ts_mesh_add_triangle(out, r0 + p, r0 + pnext, r1 + pnext);
            }
        }
    }

    /* End caps for partial revolution */
    if (!full_rev && n_points >= 3) {
        /* Triangulate the profile as a 2D polygon for caps */
        int *cap_idx = (int *)malloc((size_t)(n_points - 2) * 3 * sizeof(int));
        if (cap_idx) {
            int ntri = ts_ext_triangulate(profile_xy, n_points, cap_idx);
            if (ntri > 0) {
                /* Start cap (ring 0): reverse winding */
                int start_base = base;
                for (int i = 0; i < ntri; i++) {
                    ts_mesh_add_triangle(out,
                        start_base + cap_idx[i*3+0],
                        start_base + cap_idx[i*3+2],
                        start_base + cap_idx[i*3+1]);
                }
                /* End cap (last ring): normal winding */
                int end_base = base + n_steps * n_points;
                for (int i = 0; i < ntri; i++) {
                    ts_mesh_add_triangle(out,
                        end_base + cap_idx[i*3+0],
                        end_base + cap_idx[i*3+1],
                        end_base + cap_idx[i*3+2]);
                }
            }
            free(cap_idx);
        }
    }

    /* Recompute normals from geometry */
    ts_mesh_compute_normals(out);

    return TS_EXTRUDE_OK;
}

#endif /* TS_EXTRUDE_H */
