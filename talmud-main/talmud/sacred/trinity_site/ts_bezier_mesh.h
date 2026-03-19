/*
 * ts_bezier_mesh.h — Quadratic Bezier Patch Mesh
 *
 * A grid of quadratic bezier patches sharing edges for watertight surfaces.
 * Adjacent patches share boundary control points (C0 continuity).
 * Optional C1 enforcement: tangent vectors across boundaries are colinear.
 *
 * Memory layout: (rows x cols) grid of patches.
 * Each patch has 3x3 control points, but shared edges mean the mesh
 * stores control points in a (2*rows+1) x (2*cols+1) grid.
 *
 * The control point grid:
 *   For a mesh with R rows and C cols of patches:
 *   - CP grid has (2R+1) rows and (2C+1) cols
 *   - Patch (r,c) uses CP rows [2r..2r+2], CP cols [2c..2c+2]
 *   - Adjacent patches share the boundary row/col of CPs
 *
 * Depends on: ts_vec.h, ts_bezier_surface.h, ts_mesh.h
 *
 * GPU: batch evaluation across all patches is embarrassingly parallel.
 * Parallelism: GPU for tessellation/voxelization, SIMD for single-patch ops.
 */
#ifndef TS_BEZIER_MESH_H
#define TS_BEZIER_MESH_H

#include <stdlib.h>
#include <string.h>
#include "ts_vec.h"
#include "ts_bezier_surface.h"
#include "ts_mesh.h"

typedef struct {
    ts_vec3 *cps;       /* control point grid: cp_rows * cp_cols */
    int      rows;      /* number of patch rows */
    int      cols;      /* number of patch cols */
    int      cp_rows;   /* = 2*rows + 1 */
    int      cp_cols;   /* = 2*cols + 1 */
} ts_bezier_mesh;

/* --- Lifecycle --- */

static inline ts_bezier_mesh ts_bezier_mesh_new(int rows, int cols) {
    ts_bezier_mesh m;
    m.rows = rows;
    m.cols = cols;
    m.cp_rows = 2 * rows + 1;
    m.cp_cols = 2 * cols + 1;
    int total = m.cp_rows * m.cp_cols;
    m.cps = (ts_vec3 *)calloc((size_t)total, sizeof(ts_vec3));
    return m;
}

static inline void ts_bezier_mesh_free(ts_bezier_mesh *m) {
    free(m->cps);
    m->cps = NULL;
    m->rows = m->cols = m->cp_rows = m->cp_cols = 0;
}

/* --- Control point access --- */
/* cp_row in [0, cp_rows), cp_col in [0, cp_cols) */

static inline ts_vec3 *ts_bezier_mesh_cp(ts_bezier_mesh *m,
                                          int cp_row, int cp_col) {
    return &m->cps[cp_row * m->cp_cols + cp_col];
}

static inline ts_vec3 ts_bezier_mesh_get_cp(const ts_bezier_mesh *m,
                                             int cp_row, int cp_col) {
    return m->cps[cp_row * m->cp_cols + cp_col];
}

/* Set a control point. This is the ONLY way to modify CPs.
 * C0 continuity is automatic because adjacent patches share CPs in memory. */
static inline void ts_bezier_mesh_set_cp(ts_bezier_mesh *m,
                                          int cp_row, int cp_col,
                                          ts_vec3 pos) {
    m->cps[cp_row * m->cp_cols + cp_col] = pos;
}

/* --- Extract a single patch --- */
/* Copies the 3x3 control points for patch (row, col) into a ts_bezier_patch.
 * patch_row in [0, rows), patch_col in [0, cols). */
static inline ts_bezier_patch ts_bezier_mesh_get_patch(const ts_bezier_mesh *m,
                                                        int patch_row,
                                                        int patch_col) {
    ts_bezier_patch p;
    int base_r = 2 * patch_row;
    int base_c = 2 * patch_col;
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            p.cp[j][i] = m->cps[(base_r + j) * m->cp_cols + (base_c + i)];
        }
    }
    return p;
}

/* --- Evaluate surface point on the mesh --- */
/* patch_row, patch_col select the patch; u,v in [0,1] within that patch. */
static inline ts_vec3 ts_bezier_mesh_eval(const ts_bezier_mesh *m,
                                           int patch_row, int patch_col,
                                           double u, double v) {
    ts_bezier_patch p = ts_bezier_mesh_get_patch(m, patch_row, patch_col);
    return ts_bezier_patch_eval(&p, u, v);
}

/* --- Surface normal on the mesh --- */
static inline ts_vec3 ts_bezier_mesh_normal(const ts_bezier_mesh *m,
                                             int patch_row, int patch_col,
                                             double u, double v) {
    ts_bezier_patch p = ts_bezier_mesh_get_patch(m, patch_row, patch_col);
    return ts_bezier_patch_normal(&p, u, v);
}

/* --- Global bounding box of the entire mesh --- */
static inline void ts_bezier_mesh_bbox(const ts_bezier_mesh *m,
                                        ts_vec3 *out_min, ts_vec3 *out_max) {
    if (!m->cps || m->cp_rows == 0 || m->cp_cols == 0) {
        *out_min = ts_vec3_zero();
        *out_max = ts_vec3_zero();
        return;
    }
    *out_min = m->cps[0];
    *out_max = m->cps[0];
    int total = m->cp_rows * m->cp_cols;
    for (int i = 1; i < total; i++) {
        *out_min = ts_vec3_min(*out_min, m->cps[i]);
        *out_max = ts_vec3_max(*out_max, m->cps[i]);
    }
}

/* --- C1 continuity enforcement --- */
/* For an interior boundary between patch (r,c) and (r,c+1) (horizontal edge):
 * The shared column is cp_col = 2c+2. The tangent vectors on either side
 * are the differences to the adjacent columns.
 *
 * C1 across a vertical patch boundary (between col c and c+1):
 *   shared CP at cp_col = 2c+2
 *   left tangent:  cp[row][2c+2] - cp[row][2c+1]
 *   right tangent: cp[row][2c+3] - cp[row][2c+2]
 *   For C1: right tangent = left tangent (mirror across boundary)
 *   So: cp[row][2c+3] = 2*cp[row][2c+2] - cp[row][2c+1]
 *
 * This enforces C1 by adjusting the "right" side to match the "left" tangent.
 * Call after modifying control points on the left side of a boundary.
 */
static inline void ts_bezier_mesh_enforce_c1_col(ts_bezier_mesh *m, int patch_col) {
    if (patch_col < 0 || patch_col >= m->cols - 1) return;
    int shared_c = 2 * patch_col + 2;
    for (int r = 0; r < m->cp_rows; r++) {
        ts_vec3 left  = ts_bezier_mesh_get_cp(m, r, shared_c - 1);
        ts_vec3 shared = ts_bezier_mesh_get_cp(m, r, shared_c);
        /* right = 2*shared - left (reflection) */
        ts_vec3 right = ts_vec3_sub(ts_vec3_scale(shared, 2.0), left);
        ts_bezier_mesh_set_cp(m, r, shared_c + 1, right);
    }
}

/* C1 across a horizontal patch boundary (between row r and r+1) */
static inline void ts_bezier_mesh_enforce_c1_row(ts_bezier_mesh *m, int patch_row) {
    if (patch_row < 0 || patch_row >= m->rows - 1) return;
    int shared_r = 2 * patch_row + 2;
    for (int c = 0; c < m->cp_cols; c++) {
        ts_vec3 above  = ts_bezier_mesh_get_cp(m, shared_r - 1, c);
        ts_vec3 shared = ts_bezier_mesh_get_cp(m, shared_r, c);
        ts_vec3 below = ts_vec3_sub(ts_vec3_scale(shared, 2.0), above);
        ts_bezier_mesh_set_cp(m, shared_r + 1, c, below);
    }
}

/* Enforce C1 at ALL interior boundaries */
static inline void ts_bezier_mesh_enforce_c1(ts_bezier_mesh *m) {
    for (int c = 0; c < m->cols - 1; c++)
        ts_bezier_mesh_enforce_c1_col(m, c);
    for (int r = 0; r < m->rows - 1; r++)
        ts_bezier_mesh_enforce_c1_row(m, r);
}

/* --- Initialize as a flat grid --- */
/* Creates a flat mesh in the XY plane from (x0,y0,z) to (x1,y1,z).
 * Control points are evenly spaced. */
static inline void ts_bezier_mesh_init_flat(ts_bezier_mesh *m,
                                             double x0, double y0,
                                             double x1, double y1,
                                             double z) {
    for (int r = 0; r < m->cp_rows; r++) {
        for (int c = 0; c < m->cp_cols; c++) {
            double u = (double)c / (double)(m->cp_cols - 1);
            double v = (double)r / (double)(m->cp_rows - 1);
            ts_bezier_mesh_set_cp(m, r, c, ts_vec3_make(
                x0 + u * (x1 - x0),
                y0 + v * (y1 - y0),
                z
            ));
        }
    }
}

/* --- Tessellate mesh to triangle mesh --- */
/* Evaluates all patches at (u_steps x v_steps) resolution and emits triangles.
 * This produces a watertight triangle mesh suitable for STL export.
 * Adjacent patches share boundary vertices automatically because they share CPs.
 *
 * Total vertices: (rows*v_steps + 1) * (cols*u_steps + 1)
 * Total triangles: rows*v_steps * cols*u_steps * 2
 */
static inline int ts_bezier_mesh_tessellate(const ts_bezier_mesh *m,
                                             int u_steps, int v_steps,
                                             ts_mesh *out) {
    if (u_steps < 1 || v_steps < 1) return -1;
    if (!m->cps || m->rows == 0 || m->cols == 0) return -1;

    /* Global grid dimensions */
    int grid_u = m->cols * u_steps + 1;
    int grid_v = m->rows * v_steps + 1;
    int num_verts = grid_u * grid_v;
    int num_tris  = (grid_u - 1) * (grid_v - 1) * 2;

    ts_mesh_reserve(out, out->vert_count + num_verts, out->tri_count + num_tris);
    int base = out->vert_count;

    /* Generate vertices */
    for (int gv = 0; gv < grid_v; gv++) {
        int patch_row = gv / v_steps;
        int sub_v     = gv % v_steps;
        if (patch_row >= m->rows) { patch_row = m->rows - 1; sub_v = v_steps; }
        double v_param = (double)sub_v / (double)v_steps;

        for (int gu = 0; gu < grid_u; gu++) {
            int patch_col = gu / u_steps;
            int sub_u     = gu % u_steps;
            if (patch_col >= m->cols) { patch_col = m->cols - 1; sub_u = u_steps; }
            double u_param = (double)sub_u / (double)u_steps;

            ts_bezier_patch p = ts_bezier_mesh_get_patch(m, patch_row, patch_col);
            ts_vec3 pos = ts_bezier_patch_eval(&p, u_param, v_param);
            ts_vec3 nrm = ts_bezier_patch_normal(&p, u_param, v_param);

            ts_mesh_add_vertex(out, pos.v[0], pos.v[1], pos.v[2],
                                    nrm.v[0], nrm.v[1], nrm.v[2]);
        }
    }

    /* Generate triangles */
    for (int gv = 0; gv < grid_v - 1; gv++) {
        for (int gu = 0; gu < grid_u - 1; gu++) {
            int i00 = base + gv * grid_u + gu;
            int i10 = i00 + 1;
            int i01 = i00 + grid_u;
            int i11 = i01 + 1;
            ts_mesh_add_triangle(out, i00, i10, i11);
            ts_mesh_add_triangle(out, i00, i11, i01);
        }
    }

    return 0;
}

/* --- Closest point on entire mesh --- */
/* Tests all patches, returns the globally closest point.
 * Sets out_patch_row, out_patch_col to identify which patch. */
static inline double ts_bezier_mesh_closest(const ts_bezier_mesh *m,
                                             ts_vec3 query,
                                             int *out_patch_row,
                                             int *out_patch_col,
                                             double *out_u, double *out_v,
                                             int max_iter) {
    double best_dist = 1e30;
    *out_patch_row = 0;
    *out_patch_col = 0;
    *out_u = 0.5;
    *out_v = 0.5;

    for (int r = 0; r < m->rows; r++) {
        for (int c = 0; c < m->cols; c++) {
            /* Quick AABB rejection */
            ts_bezier_patch p = ts_bezier_mesh_get_patch(m, r, c);
            ts_vec3 bmin, bmax;
            ts_bezier_patch_bbox(&p, &bmin, &bmax);

            /* Minimum possible distance to AABB */
            double dx = fmax(bmin.v[0] - query.v[0], fmax(0.0, query.v[0] - bmax.v[0]));
            double dy = fmax(bmin.v[1] - query.v[1], fmax(0.0, query.v[1] - bmax.v[1]));
            double dz = fmax(bmin.v[2] - query.v[2], fmax(0.0, query.v[2] - bmax.v[2]));
            double aabb_dist = sqrt(dx*dx + dy*dy + dz*dz);

            if (aabb_dist > best_dist) continue;  /* skip — can't be closer */

            double u, v;
            ts_bezier_patch_closest_uv(&p, query, &u, &v, max_iter);
            ts_vec3 closest = ts_bezier_patch_eval(&p, u, v);
            double dist = ts_vec3_distance(closest, query);

            if (dist < best_dist) {
                best_dist = dist;
                *out_patch_row = r;
                *out_patch_col = c;
                *out_u = u;
                *out_v = v;
            }
        }
    }
    return best_dist;
}

#endif /* TS_BEZIER_MESH_H */
