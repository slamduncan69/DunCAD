/*
 * voxelize_bezier.c — Convert bezier surface mesh to SDF voxel grid.
 *
 * The Infinite Surface made material. Bezier math evaluated directly
 * into signed distance field — no intermediate triangles. The surface
 * is lossless at every resolution.
 *
 * Algorithm:
 *   For each patch in the mesh:
 *     1. Compute patch AABB, expand by narrowband
 *     2. For each voxel in the expanded AABB:
 *        a. Newton-solve for closest (u,v) on patch
 *        b. Compute signed distance via normal dot product
 *        c. Min-abs with existing (closest surface wins)
 *     3. Activate cells where distance <= 0
 *     4. Color by SDF gradient (normal-mapped)
 */

#include "voxel/voxelize_bezier.h"
#include "voxel/sdf.h"
#include "core/log.h"

#include <math.h>
#include <stdlib.h>
#include <float.h>

/* Include Trinity Site bezier headers directly.
 * These are header-only (static inline), no link dependency. */
#include "../../talmud-main/talmud/sacred/trinity_site/ts_vec.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_surface.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_mesh.h"

DC_VoxelGrid *
dc_voxelize_bezier(const void *mesh_ptr, int resolution,
                     int band_cells, int newton_iters,
                     DC_Error *err)
{
    const ts_bezier_mesh *mesh = (const ts_bezier_mesh *)mesh_ptr;

    if (!mesh || !mesh->cps || mesh->rows <= 0 || mesh->cols <= 0) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL or empty bezier mesh");
        return NULL;
    }
    if (resolution < 4) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "resolution must be >= 4");
        return NULL;
    }
    if (band_cells < 1) band_cells = 2;
    if (newton_iters < 1) newton_iters = 15;

    /* Compute mesh bounding box */
    ts_vec3 bmin, bmax;
    ts_bezier_mesh_bbox(mesh, &bmin, &bmax);

    ts_vec3 extent = ts_vec3_sub(bmax, bmin);
    double max_extent = extent.v[0];
    if (extent.v[1] > max_extent) max_extent = extent.v[1];
    if (extent.v[2] > max_extent) max_extent = extent.v[2];

    if (max_extent < 1e-10) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "degenerate mesh (zero extent)");
        return NULL;
    }

    /* Compute cell size and grid dimensions */
    float cell_size = (float)(max_extent / (double)(resolution - 2));
    double pad = cell_size * 2.0;

    int sx = (int)ceil((extent.v[0] + 2.0 * pad) / cell_size) + 1;
    int sy = (int)ceil((extent.v[1] + 2.0 * pad) / cell_size) + 1;
    int sz = (int)ceil((extent.v[2] + 2.0 * pad) / cell_size) + 1;

    /* Cap to prevent memory explosion */
    if (sx > 512) sx = 512;
    if (sy > 512) sy = 512;
    if (sz > 512) sz = 512;

    DC_VoxelGrid *grid = dc_voxel_grid_new(sx, sy, sz, cell_size);
    if (!grid) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY,
                              "voxel grid alloc %dx%dx%d", sx, sy, sz);
        return NULL;
    }

    /* Set origin so the mesh is centered in the grid */
    float ox = (float)(bmin.v[0] - pad);
    float oy = (float)(bmin.v[1] - pad);
    float oz = (float)(bmin.v[2] - pad);
    dc_voxel_grid_set_origin(grid, ox, oy, oz);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "voxelizing bezier mesh (%dx%d patches) → %dx%dx%d grid (cell=%.4f)",
           mesh->rows, mesh->cols, sx, sy, sz, cell_size);

    double band = band_cells * (double)cell_size;

    /* For each patch: narrow-band SDF evaluation */
    for (int pr = 0; pr < mesh->rows; pr++) {
        for (int pc = 0; pc < mesh->cols; pc++) {
            ts_bezier_patch patch = ts_bezier_mesh_get_patch(mesh, pr, pc);

            /* Patch AABB expanded by band */
            ts_vec3 pmin, pmax;
            ts_bezier_patch_bbox(&patch, &pmin, &pmax);
            pmin = ts_vec3_sub(pmin, ts_vec3_make(band, band, band));
            pmax = ts_vec3_add(pmax, ts_vec3_make(band, band, band));

            /* Convert to grid cell range */
            int ix0 = (int)floor((pmin.v[0] - ox) / cell_size);
            int iy0 = (int)floor((pmin.v[1] - oy) / cell_size);
            int iz0 = (int)floor((pmin.v[2] - oz) / cell_size);
            int ix1 = (int)ceil((pmax.v[0] - ox) / cell_size);
            int iy1 = (int)ceil((pmax.v[1] - oy) / cell_size);
            int iz1 = (int)ceil((pmax.v[2] - oz) / cell_size);

            if (ix0 < 0) ix0 = 0;
            if (iy0 < 0) iy0 = 0;
            if (iz0 < 0) iz0 = 0;
            if (ix1 >= sx) ix1 = sx - 1;
            if (iy1 >= sy) iy1 = sy - 1;
            if (iz1 >= sz) iz1 = sz - 1;

            for (int iz = iz0; iz <= iz1; iz++) {
                for (int iy = iy0; iy <= iy1; iy++) {
                    for (int ix = ix0; ix <= ix1; ix++) {
                        float wx, wy, wz;
                        dc_voxel_grid_cell_center(grid, ix, iy, iz,
                                                   &wx, &wy, &wz);
                        ts_vec3 center = ts_vec3_make(wx, wy, wz);

                        /* Newton solve for closest point on patch */
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

                        /* Min-abs composit: closest surface wins */
                        DC_Voxel *vx = dc_voxel_grid_get(grid, ix, iy, iz);
                        if (vx && fabsf(sdf) < fabsf(vx->distance)) {
                            vx->distance = sdf;
                        }
                    }
                }
            }
        }
    }

    /* Activate and color */
    dc_sdf_activate(grid);
    dc_sdf_color_by_normal(grid);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "voxelized bezier: %zu active voxels",
           dc_voxel_grid_active_count(grid));

    return grid;
}
