/*
 * ts_bezier_voxel.h — Bezier Surface Mesh → SDF Voxelization
 *
 * Converts a ts_bezier_mesh into a 3D signed distance field grid.
 * For each voxel near the surface:
 *   1. Find closest patch via AABB culling
 *   2. Newton solve for closest (u,v) on that patch
 *   3. Compute signed distance via surface normal
 *   4. Write to SDF grid
 *
 * This is V2.3 of the Infinite Surface pipeline.
 * The grid is a simple flat array — no DC_VoxelGrid dependency.
 * Integration with DC_VoxelGrid happens in src/voxel/voxelize_bezier.c.
 *
 * Depends on: ts_vec.h, ts_bezier_surface.h, ts_bezier_mesh.h
 *
 * GPU: per-voxel evaluation is embarrassingly parallel.
 * Parallelism: GPU (each voxel independent).
 */
#ifndef TS_BEZIER_VOXEL_H
#define TS_BEZIER_VOXEL_H

#include <stdlib.h>
#include <math.h>
#include <float.h>
#include "ts_vec.h"
#include "ts_bezier_surface.h"
#include "ts_bezier_mesh.h"

/* --- SDF Grid --- */
/* Simple 3D grid of signed distance values.
 * grid[ix + iy*sx + iz*sx*sy] = signed distance at voxel center.
 * Positive = outside surface, negative = inside. */

typedef struct {
    float  *distances;   /* flat array: sx * sy * sz */
    int     sx, sy, sz;  /* grid dimensions */
    double  cell_size;   /* world units per cell */
    double  origin[3];   /* world position of cell (0,0,0) corner */
} ts_sdf_grid;

static inline ts_sdf_grid ts_sdf_grid_new(int sx, int sy, int sz,
                                            double cell_size,
                                            double ox, double oy, double oz) {
    ts_sdf_grid g;
    g.sx = sx;
    g.sy = sy;
    g.sz = sz;
    g.cell_size = cell_size;
    g.origin[0] = ox;
    g.origin[1] = oy;
    g.origin[2] = oz;
    size_t total = (size_t)sx * (size_t)sy * (size_t)sz;
    g.distances = (float *)malloc(total * sizeof(float));
    if (g.distances) {
        for (size_t i = 0; i < total; i++)
            g.distances[i] = FLT_MAX;
    }
    return g;
}

static inline void ts_sdf_grid_free(ts_sdf_grid *g) {
    free(g->distances);
    g->distances = NULL;
}

static inline ts_vec3 ts_sdf_grid_cell_center(const ts_sdf_grid *g,
                                               int ix, int iy, int iz) {
    double half = g->cell_size * 0.5;
    return ts_vec3_make(
        g->origin[0] + ix * g->cell_size + half,
        g->origin[1] + iy * g->cell_size + half,
        g->origin[2] + iz * g->cell_size + half
    );
}

static inline float ts_sdf_grid_get(const ts_sdf_grid *g,
                                     int ix, int iy, int iz) {
    return g->distances[ix + iy * g->sx + iz * g->sx * g->sy];
}

static inline void ts_sdf_grid_set(ts_sdf_grid *g,
                                    int ix, int iy, int iz, float val) {
    g->distances[ix + iy * g->sx + iz * g->sx * g->sy] = val;
}

/* --- Narrowband voxelization --- */
/* Only evaluates voxels within `band` world-units of any patch AABB.
 * This is the "adaptive" part — far-from-surface voxels stay at FLT_MAX.
 *
 * For each patch: expand AABB by band, clamp to grid, evaluate SDF.
 * Uses min to composit (closest surface wins).
 *
 * newton_iters: iterations for closest-point solve (10-20 typical).
 * band: narrowband width in world units (e.g., 2*cell_size).
 *
 * Returns number of voxels evaluated.
 */
static inline int ts_bezier_mesh_voxelize(const ts_bezier_mesh *mesh,
                                           ts_sdf_grid *grid,
                                           double band,
                                           int newton_iters) {
    int evaluated = 0;

    for (int pr = 0; pr < mesh->rows; pr++) {
        for (int pc = 0; pc < mesh->cols; pc++) {
            ts_bezier_patch patch = ts_bezier_mesh_get_patch(mesh, pr, pc);

            /* Patch AABB expanded by band */
            ts_vec3 pmin, pmax;
            ts_bezier_patch_bbox(&patch, &pmin, &pmax);
            pmin = ts_vec3_sub(pmin, ts_vec3_make(band, band, band));
            pmax = ts_vec3_add(pmax, ts_vec3_make(band, band, band));

            /* Clamp to grid extents */
            int ix0 = (int)floor((pmin.v[0] - grid->origin[0]) / grid->cell_size);
            int iy0 = (int)floor((pmin.v[1] - grid->origin[1]) / grid->cell_size);
            int iz0 = (int)floor((pmin.v[2] - grid->origin[2]) / grid->cell_size);
            int ix1 = (int)ceil((pmax.v[0] - grid->origin[0]) / grid->cell_size);
            int iy1 = (int)ceil((pmax.v[1] - grid->origin[1]) / grid->cell_size);
            int iz1 = (int)ceil((pmax.v[2] - grid->origin[2]) / grid->cell_size);

            if (ix0 < 0) ix0 = 0;
            if (iy0 < 0) iy0 = 0;
            if (iz0 < 0) iz0 = 0;
            if (ix1 >= grid->sx) ix1 = grid->sx - 1;
            if (iy1 >= grid->sy) iy1 = grid->sy - 1;
            if (iz1 >= grid->sz) iz1 = grid->sz - 1;

            for (int iz = iz0; iz <= iz1; iz++) {
                for (int iy = iy0; iy <= iy1; iy++) {
                    for (int ix = ix0; ix <= ix1; ix++) {
                        ts_vec3 center = ts_sdf_grid_cell_center(grid, ix, iy, iz);

                        /* Compute SDF for this patch */
                        double u, v;
                        ts_bezier_patch_closest_uv(&patch, center, &u, &v,
                                                    newton_iters);
                        ts_vec3 closest = ts_bezier_patch_eval(&patch, u, v);
                        ts_vec3 normal  = ts_bezier_patch_normal(&patch, u, v);
                        ts_vec3 diff    = ts_vec3_sub(center, closest);
                        double dist     = ts_vec3_norm(diff);
                        double sign     = ts_vec3_dot(diff, normal) >= 0.0 ?
                                          1.0 : -1.0;
                        float sdf = (float)(sign * dist);

                        /* Min with existing value (closest surface wins) */
                        float existing = ts_sdf_grid_get(grid, ix, iy, iz);
                        if (fabs(sdf) < fabs(existing)) {
                            ts_sdf_grid_set(grid, ix, iy, iz, sdf);
                        }

                        evaluated++;
                    }
                }
            }
        }
    }

    return evaluated;
}

/* --- Count active (inside) voxels --- */
static inline int ts_sdf_grid_count_inside(const ts_sdf_grid *g) {
    int count = 0;
    size_t total = (size_t)g->sx * (size_t)g->sy * (size_t)g->sz;
    for (size_t i = 0; i < total; i++) {
        if (g->distances[i] <= 0.0f) count++;
    }
    return count;
}

/* --- Count voxels near surface (|distance| < threshold) --- */
static inline int ts_sdf_grid_count_near_surface(const ts_sdf_grid *g,
                                                   float threshold) {
    int count = 0;
    size_t total = (size_t)g->sx * (size_t)g->sy * (size_t)g->sz;
    for (size_t i = 0; i < total; i++) {
        if (g->distances[i] != FLT_MAX && fabsf(g->distances[i]) < threshold) {
            count++;
        }
    }
    return count;
}

/* --- SDF gradient (approximate normal from finite differences) --- */
static inline ts_vec3 ts_sdf_grid_gradient(const ts_sdf_grid *g,
                                            int ix, int iy, int iz) {
    float dx = 0, dy = 0, dz = 0;

    if (ix > 0 && ix < g->sx - 1) {
        dx = ts_sdf_grid_get(g, ix+1, iy, iz) -
             ts_sdf_grid_get(g, ix-1, iy, iz);
    }
    if (iy > 0 && iy < g->sy - 1) {
        dy = ts_sdf_grid_get(g, ix, iy+1, iz) -
             ts_sdf_grid_get(g, ix, iy-1, iz);
    }
    if (iz > 0 && iz < g->sz - 1) {
        dz = ts_sdf_grid_get(g, ix, iy, iz+1) -
             ts_sdf_grid_get(g, ix, iy, iz-1);
    }

    return ts_vec3_normalize(ts_vec3_make((double)dx, (double)dy, (double)dz));
}

/* --- Voxelize a closed bezier sphere --- */
/* Narrowband SDF evaluation across all 6 faces. */
static inline int ts_bezier_sphere_voxelize(const ts_bezier_sphere *s,
                                             ts_sdf_grid *grid,
                                             double band,
                                             int newton_iters) {
    int evaluated = 0;

    for (int face = 0; face < 6; face++) {
        const ts_bezier_patch *patch = &s->faces[face];

        ts_vec3 pmin, pmax;
        ts_bezier_patch_bbox(patch, &pmin, &pmax);
        pmin = ts_vec3_sub(pmin, ts_vec3_make(band, band, band));
        pmax = ts_vec3_add(pmax, ts_vec3_make(band, band, band));

        int ix0 = (int)floor((pmin.v[0] - grid->origin[0]) / grid->cell_size);
        int iy0 = (int)floor((pmin.v[1] - grid->origin[1]) / grid->cell_size);
        int iz0 = (int)floor((pmin.v[2] - grid->origin[2]) / grid->cell_size);
        int ix1 = (int)ceil((pmax.v[0] - grid->origin[0]) / grid->cell_size);
        int iy1 = (int)ceil((pmax.v[1] - grid->origin[1]) / grid->cell_size);
        int iz1 = (int)ceil((pmax.v[2] - grid->origin[2]) / grid->cell_size);

        if (ix0 < 0) ix0 = 0;
        if (iy0 < 0) iy0 = 0;
        if (iz0 < 0) iz0 = 0;
        if (ix1 >= grid->sx) ix1 = grid->sx - 1;
        if (iy1 >= grid->sy) iy1 = grid->sy - 1;
        if (iz1 >= grid->sz) iz1 = grid->sz - 1;

        for (int iz = iz0; iz <= iz1; iz++) {
            for (int iy = iy0; iy <= iy1; iy++) {
                for (int ix = ix0; ix <= ix1; ix++) {
                    ts_vec3 center = ts_sdf_grid_cell_center(grid, ix, iy, iz);

                    double u, v;
                    ts_bezier_patch_closest_uv(patch, center, &u, &v,
                                                newton_iters);
                    ts_vec3 closest = ts_bezier_patch_eval(patch, u, v);
                    ts_vec3 normal  = ts_bezier_patch_normal(patch, u, v);
                    ts_vec3 diff    = ts_vec3_sub(center, closest);
                    double dist     = ts_vec3_norm(diff);
                    double sign     = ts_vec3_dot(diff, normal) >= 0.0 ?
                                      1.0 : -1.0;
                    float sdf = (float)(sign * dist);

                    float existing = ts_sdf_grid_get(grid, ix, iy, iz);
                    if (fabsf(sdf) < fabsf(existing)) {
                        ts_sdf_grid_set(grid, ix, iy, iz, sdf);
                    }
                    evaluated++;
                }
            }
        }
    }
    return evaluated;
}

/* --- Voxelize a closed bezier torus --- */
static inline int ts_bezier_torus_voxelize(const ts_bezier_torus *t,
                                            ts_sdf_grid *grid,
                                            double band,
                                            int newton_iters) {
    int evaluated = 0;
    int total = t->rows * t->cols;

    for (int f = 0; f < total; f++) {
        const ts_bezier_patch *patch = &t->faces[f];

        ts_vec3 pmin, pmax;
        ts_bezier_patch_bbox(patch, &pmin, &pmax);
        pmin = ts_vec3_sub(pmin, ts_vec3_make(band, band, band));
        pmax = ts_vec3_add(pmax, ts_vec3_make(band, band, band));

        int ix0 = (int)floor((pmin.v[0] - grid->origin[0]) / grid->cell_size);
        int iy0 = (int)floor((pmin.v[1] - grid->origin[1]) / grid->cell_size);
        int iz0 = (int)floor((pmin.v[2] - grid->origin[2]) / grid->cell_size);
        int ix1 = (int)ceil((pmax.v[0] - grid->origin[0]) / grid->cell_size);
        int iy1 = (int)ceil((pmax.v[1] - grid->origin[1]) / grid->cell_size);
        int iz1 = (int)ceil((pmax.v[2] - grid->origin[2]) / grid->cell_size);

        if (ix0 < 0) ix0 = 0;
        if (iy0 < 0) iy0 = 0;
        if (iz0 < 0) iz0 = 0;
        if (ix1 >= grid->sx) ix1 = grid->sx - 1;
        if (iy1 >= grid->sy) iy1 = grid->sy - 1;
        if (iz1 >= grid->sz) iz1 = grid->sz - 1;

        for (int iz = iz0; iz <= iz1; iz++) {
            for (int iy = iy0; iy <= iy1; iy++) {
                for (int ix = ix0; ix <= ix1; ix++) {
                    ts_vec3 center = ts_sdf_grid_cell_center(grid, ix, iy, iz);

                    double u, v;
                    ts_bezier_patch_closest_uv(patch, center, &u, &v,
                                                newton_iters);
                    ts_vec3 closest = ts_bezier_patch_eval(patch, u, v);
                    ts_vec3 normal  = ts_bezier_patch_normal(patch, u, v);
                    ts_vec3 diff    = ts_vec3_sub(center, closest);
                    double dist     = ts_vec3_norm(diff);
                    double sign     = ts_vec3_dot(diff, normal) >= 0.0 ?
                                      1.0 : -1.0;
                    float sdf = (float)(sign * dist);

                    float existing = ts_sdf_grid_get(grid, ix, iy, iz);
                    if (fabsf(sdf) < fabsf(existing)) {
                        ts_sdf_grid_set(grid, ix, iy, iz, sdf);
                    }
                    evaluated++;
                }
            }
        }
    }
    return evaluated;
}

#endif /* TS_BEZIER_VOXEL_H */
