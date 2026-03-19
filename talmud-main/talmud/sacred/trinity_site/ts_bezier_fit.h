/*
 * ts_bezier_fit.h — Fit quadratic bezier patches to a triangle mesh
 *
 * Subdivides the mesh bounding box into a grid of regions.
 * For each region, collects nearby vertices, parameterizes in local (u,v),
 * and least-squares fits 9 control points of a quadratic bezier patch.
 *
 * The result is a ts_bezier_mesh that approximates the input triangle mesh.
 *
 * Depends on: ts_bezier_mesh.h, ts_mesh.h
 */
#ifndef TS_BEZIER_FIT_H
#define TS_BEZIER_FIT_H

#include "ts_bezier_mesh.h"
#include "ts_mesh.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * 9×9 Linear System Solver (Gaussian elimination with partial pivoting)
 *
 * Solves Ax = b for x, where A is 9×9 and b is 9×1.
 * A is modified in place. Returns 0 on success, -1 if singular.
 * ========================================================================= */
static inline int ts_solve_9x9(double A[9][9], double b[9], double x[9])
{
    int n = 9;
    /* Forward elimination with partial pivoting */
    for (int k = 0; k < n; k++) {
        /* Find pivot */
        int max_row = k;
        double max_val = fabs(A[k][k]);
        for (int i = k + 1; i < n; i++) {
            if (fabs(A[i][k]) > max_val) {
                max_val = fabs(A[i][k]);
                max_row = i;
            }
        }
        if (max_val < 1e-12) return -1; /* singular */

        /* Swap rows */
        if (max_row != k) {
            for (int j = k; j < n; j++) {
                double tmp = A[k][j]; A[k][j] = A[max_row][j]; A[max_row][j] = tmp;
            }
            double tmp = b[k]; b[k] = b[max_row]; b[max_row] = tmp;
        }

        /* Eliminate below */
        for (int i = k + 1; i < n; i++) {
            double factor = A[i][k] / A[k][k];
            for (int j = k + 1; j < n; j++)
                A[i][j] -= factor * A[k][j];
            A[i][k] = 0.0;
            b[i] -= factor * b[k];
        }
    }

    /* Back substitution */
    for (int i = n - 1; i >= 0; i--) {
        x[i] = b[i];
        for (int j = i + 1; j < n; j++)
            x[i] -= A[i][j] * x[j];
        x[i] /= A[i][i];
    }
    return 0;
}

/* =========================================================================
 * Quadratic Bernstein basis functions
 * B0(t) = (1-t)^2, B1(t) = 2t(1-t), B2(t) = t^2
 * ========================================================================= */
static inline void ts_bernstein2(double t, double out[3])
{
    double s = 1.0 - t;
    out[0] = s * s;
    out[1] = 2.0 * s * t;
    out[2] = t * t;
}

/* =========================================================================
 * Fit a rectangular grid of bezier patches to a triangle mesh.
 *
 * Algorithm per patch region:
 *   1. Compute region's bounding box (subdivision of global bbox)
 *   2. Collect triangle vertices within or near the region
 *   3. Parameterize each vertex: project onto dominant plane
 *   4. Build basis matrix B: rows are tensor products of Bernstein
 *   5. Solve normal equations (B^T B) c = B^T p for each coordinate
 *   6. Store 9 CPs into the mesh grid
 *
 * Parameters:
 *   trimesh    - input triangle mesh
 *   patch_rows - number of patch rows in output
 *   patch_cols - number of patch columns in output
 *   out        - output bezier mesh (allocated by this function)
 *
 * Returns 0 on success, -1 on error.
 * ========================================================================= */
static inline int ts_bezier_fit_from_trimesh(const ts_mesh *trimesh,
                                              int patch_rows, int patch_cols,
                                              ts_bezier_mesh *out)
{
    if (!trimesh || trimesh->vert_count == 0 || !out) return -1;
    if (patch_rows < 1) patch_rows = 1;
    if (patch_cols < 1) patch_cols = 1;

    /* Global bounding box */
    double gmin[3], gmax[3];
    ts_mesh_bounds(trimesh, gmin, gmax);

    /* Add small padding to avoid degenerate regions */
    for (int j = 0; j < 3; j++) {
        double pad = (gmax[j] - gmin[j]) * 0.01 + 1e-6;
        gmin[j] -= pad;
        gmax[j] += pad;
    }

    double gsize[3];
    for (int j = 0; j < 3; j++)
        gsize[j] = gmax[j] - gmin[j];

    /* Determine dominant projection plane:
     * Find the two axes with largest extent for u,v parameterization.
     * The third axis is the "height" that we're fitting. */
    int u_axis, v_axis;
    if (gsize[0] >= gsize[1] && gsize[0] >= gsize[2]) {
        /* X is largest */
        if (gsize[1] >= gsize[2]) { u_axis = 0; v_axis = 1; }
        else { u_axis = 0; v_axis = 2; }
    } else if (gsize[1] >= gsize[2]) {
        /* Y is largest */
        if (gsize[0] >= gsize[2]) { u_axis = 1; v_axis = 0; }
        else { u_axis = 1; v_axis = 2; }
    } else {
        /* Z is largest */
        if (gsize[0] >= gsize[1]) { u_axis = 2; v_axis = 0; }
        else { u_axis = 2; v_axis = 1; }
    }

    *out = ts_bezier_mesh_new(patch_rows, patch_cols);

    /* Fit each patch */
    for (int pr = 0; pr < patch_rows; pr++) {
        for (int pc = 0; pc < patch_cols; pc++) {
            /* Region bounds in u,v space */
            double u0 = gmin[u_axis] + gsize[u_axis] * (double)pc / (double)patch_cols;
            double u1 = gmin[u_axis] + gsize[u_axis] * (double)(pc+1) / (double)patch_cols;
            double v0 = gmin[v_axis] + gsize[v_axis] * (double)pr / (double)patch_rows;
            double v1 = gmin[v_axis] + gsize[v_axis] * (double)(pr+1) / (double)patch_rows;

            /* Expand region slightly to capture boundary vertices */
            double u_pad = (u1 - u0) * 0.15;
            double v_pad = (v1 - v0) * 0.15;

            /* Collect vertices in this region */
            int cap = 64;
            int count = 0;
            double *us = (double *)malloc((size_t)cap * sizeof(double));
            double *vs = (double *)malloc((size_t)cap * sizeof(double));
            double *px = (double *)malloc((size_t)cap * sizeof(double));
            double *py = (double *)malloc((size_t)cap * sizeof(double));
            double *pz = (double *)malloc((size_t)cap * sizeof(double));

            for (int i = 0; i < trimesh->vert_count; i++) {
                double pu = trimesh->verts[i].pos[u_axis];
                double pv = trimesh->verts[i].pos[v_axis];
                if (pu >= u0 - u_pad && pu <= u1 + u_pad &&
                    pv >= v0 - v_pad && pv <= v1 + v_pad) {
                    if (count >= cap) {
                        cap *= 2;
                        us = (double *)realloc(us, (size_t)cap * sizeof(double));
                        vs = (double *)realloc(vs, (size_t)cap * sizeof(double));
                        px = (double *)realloc(px, (size_t)cap * sizeof(double));
                        py = (double *)realloc(py, (size_t)cap * sizeof(double));
                        pz = (double *)realloc(pz, (size_t)cap * sizeof(double));
                    }
                    /* Normalize to [0,1] within the region */
                    us[count] = (u1 > u0) ? (pu - u0) / (u1 - u0) : 0.5;
                    vs[count] = (v1 > v0) ? (pv - v0) / (v1 - v0) : 0.5;
                    /* Clamp to [0,1] */
                    if (us[count] < 0.0) us[count] = 0.0;
                    if (us[count] > 1.0) us[count] = 1.0;
                    if (vs[count] < 0.0) vs[count] = 0.0;
                    if (vs[count] > 1.0) vs[count] = 1.0;
                    px[count] = trimesh->verts[i].pos[0];
                    py[count] = trimesh->verts[i].pos[1];
                    pz[count] = trimesh->verts[i].pos[2];
                    count++;
                }
            }

            int base_r = 2 * pr;
            int base_c = 2 * pc;

            if (count < 9) {
                /* Not enough points — initialize CPs with bilinear from bbox */
                for (int jj = 0; jj < 3; jj++) {
                    double vf = (double)jj * 0.5;
                    double wv = v0 + vf * (v1 - v0);
                    for (int ii = 0; ii < 3; ii++) {
                        double uf = (double)ii * 0.5;
                        double wu = u0 + uf * (u1 - u0);
                        double cp[3] = {0, 0, 0};
                        cp[u_axis] = wu;
                        cp[v_axis] = wv;
                        /* Height: average of nearby points or midpoint */
                        int h_axis = 3 - u_axis - v_axis;
                        cp[h_axis] = (gmin[h_axis] + gmax[h_axis]) * 0.5;
                        ts_bezier_mesh_set_cp(out, base_r + jj, base_c + ii,
                            ts_vec3_make(cp[0], cp[1], cp[2]));
                    }
                }
            } else {
                /* Least-squares fit: solve (B^T B) c = B^T p for each axis
                 * B is count×9, each row is [B0(u)*B0(v), B0(u)*B1(v), ..., B2(u)*B2(v)]
                 */
                double BtB[9][9];
                double Btpx[9], Btpy[9], Btpz[9];
                memset(BtB, 0, sizeof(BtB));
                memset(Btpx, 0, sizeof(Btpx));
                memset(Btpy, 0, sizeof(Btpy));
                memset(Btpz, 0, sizeof(Btpz));

                for (int i = 0; i < count; i++) {
                    double bu[3], bv[3];
                    ts_bernstein2(us[i], bu);
                    ts_bernstein2(vs[i], bv);

                    double basis[9];
                    for (int jj = 0; jj < 3; jj++)
                        for (int ii = 0; ii < 3; ii++)
                            basis[jj * 3 + ii] = bv[jj] * bu[ii];

                    /* Accumulate B^T B and B^T p */
                    for (int a = 0; a < 9; a++) {
                        for (int bb = 0; bb < 9; bb++)
                            BtB[a][bb] += basis[a] * basis[bb];
                        Btpx[a] += basis[a] * px[i];
                        Btpy[a] += basis[a] * py[i];
                        Btpz[a] += basis[a] * pz[i];
                    }
                }

                /* Solve for x, y, z independently */
                double cx[9], cy[9], cz[9];
                double A1[9][9], A2[9][9], A3[9][9];
                double b1[9], b2[9], b3[9];
                memcpy(A1, BtB, sizeof(BtB)); memcpy(b1, Btpx, sizeof(Btpx));
                memcpy(A2, BtB, sizeof(BtB)); memcpy(b2, Btpy, sizeof(Btpy));
                memcpy(A3, BtB, sizeof(BtB)); memcpy(b3, Btpz, sizeof(Btpz));

                int ok = 1;
                if (ts_solve_9x9(A1, b1, cx) != 0) ok = 0;
                if (ts_solve_9x9(A2, b2, cy) != 0) ok = 0;
                if (ts_solve_9x9(A3, b3, cz) != 0) ok = 0;

                if (ok) {
                    for (int jj = 0; jj < 3; jj++)
                        for (int ii = 0; ii < 3; ii++) {
                            int idx = jj * 3 + ii;
                            ts_bezier_mesh_set_cp(out, base_r + jj, base_c + ii,
                                ts_vec3_make(cx[idx], cy[idx], cz[idx]));
                        }
                } else {
                    /* Fallback: use averaged points */
                    double avg_x = 0, avg_y = 0, avg_z = 0;
                    for (int i = 0; i < count; i++) {
                        avg_x += px[i]; avg_y += py[i]; avg_z += pz[i];
                    }
                    avg_x /= count; avg_y /= count; avg_z /= count;
                    for (int jj = 0; jj < 3; jj++)
                        for (int ii = 0; ii < 3; ii++) {
                            double uf = (double)ii * 0.5;
                            double vf = (double)jj * 0.5;
                            double wu = u0 + uf * (u1 - u0);
                            double wv = v0 + vf * (v1 - v0);
                            double cp[3];
                            cp[u_axis] = wu;
                            cp[v_axis] = wv;
                            cp[3 - u_axis - v_axis] = avg_x + avg_y + avg_z -
                                wu - wv; /* rough projection */
                            ts_bezier_mesh_set_cp(out, base_r + jj, base_c + ii,
                                ts_vec3_make(cp[0], cp[1], cp[2]));
                        }
                }
            }

            free(us); free(vs); free(px); free(py); free(pz);
        }
    }

    /* Enforce C1 continuity at interior boundaries */
    ts_bezier_mesh_enforce_c1(out);

    return 0;
}

#endif /* TS_BEZIER_FIT_H */
