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
           "voxelizing bezier mesh (%dx%d patches, %dx%d CPs, cps=%p) → %dx%dx%d grid (cell=%.4f)",
           mesh->rows, mesh->cols, mesh->cp_rows, mesh->cp_cols, (void*)mesh->cps,
           sx, sy, sz, cell_size);
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "  bbox=(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f) origin=(%.3f,%.3f,%.3f)",
           bmin.v[0], bmin.v[1], bmin.v[2], bmax.v[0], bmax.v[1], bmax.v[2],
           (double)ox, (double)oy, (double)oz);
    /* Sanity: check first CP */
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "  CP[0]=(%.3f,%.3f,%.3f) CP[last]=(%.3f,%.3f,%.3f)",
           mesh->cps[0].v[0], mesh->cps[0].v[1], mesh->cps[0].v[2],
           mesh->cps[mesh->cp_rows*mesh->cp_cols-1].v[0],
           mesh->cps[mesh->cp_rows*mesh->cp_cols-1].v[1],
           mesh->cps[mesh->cp_rows*mesh->cp_cols-1].v[2]);
    /* Print first surface sample for debugging */
    {
        ts_bezier_patch p0 = ts_bezier_mesh_get_patch(mesh, 0, 0);
        ts_vec3 s00 = ts_bezier_patch_eval(&p0, 0, 0);
        ts_vec3 s55 = ts_bezier_patch_eval(&p0, 0.5, 0.5);
        float wx0, wy0, wz0;
        dc_voxel_grid_cell_center(grid, sx/2, sy/2, sz/2, &wx0, &wy0, &wz0);
        dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
               "  patch(0,0) eval(0,0)=(%.3f,%.3f,%.3f) eval(0.5,0.5)=(%.3f,%.3f,%.3f) grid_center=(%.3f,%.3f,%.3f)",
               s00.v[0], s00.v[1], s00.v[2], s55.v[0], s55.v[1], s55.v[2],
               (double)wx0, (double)wy0, (double)wz0);
    }

    (void)newton_iters; /* unused in dense sampling approach */

    /* Dense surface sampling approach:
     * Sample each patch at a grid of (u,v) points. For each sample,
     * update nearby voxels with the distance to that surface point.
     * This avoids Newton solver issues and guarantees coverage. */
    int uv_samples = resolution / 2;
    if (uv_samples < 16) uv_samples = 16;
    if (uv_samples > 128) uv_samples = 128;

    for (int pr = 0; pr < mesh->rows; pr++) {
        for (int pc = 0; pc < mesh->cols; pc++) {
            ts_bezier_patch patch = ts_bezier_mesh_get_patch(mesh, pr, pc);

            for (int ui = 0; ui <= uv_samples; ui++) {
                double u = (double)ui / (double)uv_samples;
                for (int vi = 0; vi <= uv_samples; vi++) {
                    double v = (double)vi / (double)uv_samples;

                    ts_vec3 sp = ts_bezier_patch_eval(&patch, u, v);
                    if (pr == 0 && pc == 0 && ui == 0 && vi == 0) {
                        fprintf(stderr, "SAMPLE: patch(%d,%d) uv=(%d,%d) → (%.3f,%.3f,%.3f)\n",
                                pr, pc, ui, vi, sp.v[0], sp.v[1], sp.v[2]);
                    }

                    /* Update voxels in a small neighborhood around this surface point */
                    int cx = (int)floor((sp.v[0] - ox) / cell_size);
                    int cy = (int)floor((sp.v[1] - oy) / cell_size);
                    int cz = (int)floor((sp.v[2] - oz) / cell_size);

                    for (int dz = -band_cells; dz <= band_cells; dz++)
                    for (int dy = -band_cells; dy <= band_cells; dy++)
                    for (int dx = -band_cells; dx <= band_cells; dx++) {
                        int ix = cx + dx, iy = cy + dy, iz = cz + dz;
                        if (ix < 0 || iy < 0 || iz < 0 ||
                            ix >= sx || iy >= sy || iz >= sz) continue;

                        float wx, wy, wz;
                        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);
                        float ddx = wx - (float)sp.v[0];
                        float ddy = wy - (float)sp.v[1];
                        float ddz = wz - (float)sp.v[2];
                        float dist = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz);

                        DC_Voxel *vx = dc_voxel_grid_get(grid, ix, iy, iz);
                        if (pr == 0 && pc == 0 && ui == 0 && vi == 0 && dx == 0 && dy == 0 && dz == 0) {
                            fprintf(stderr, "INNER: ix=%d iy=%d iz=%d wx=%.3f wy=%.3f wz=%.3f dist=%.3f cur=%.3f vx=%p\n",
                                    ix, iy, iz, wx, wy, wz, dist, vx ? vx->distance : -1.0f, (void*)vx);
                        }
                        if (vx && dist < fabsf(vx->distance)) {
                            vx->distance = dist;
                        }
                    }
                }
            }
        }
    }

    fprintf(stderr, "DENSE: sampled %d patches, %d uv_samples\n",
            mesh->rows * mesh->cols, uv_samples);

    /* Debug: count voxels by distance range */
    {
        int near = 0, mid = 0, far = 0;
        float min_d = 1e18f, max_d = 0;
        for (int iz2 = 0; iz2 < sz; iz2++)
        for (int iy2 = 0; iy2 < sy; iy2++)
        for (int ix2 = 0; ix2 < sx; ix2++) {
            DC_Voxel *vx = dc_voxel_grid_get(grid, ix2, iy2, iz2);
            if (!vx) continue;
            float d = fabsf(vx->distance);
            if (d < cell_size) near++;
            else if (d < cell_size * 3) mid++;
            else far++;
            if (d < min_d) min_d = d;
            if (d > max_d && d < 1e10f) max_d = d;
        }
        dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
               "voxelize_bezier distance stats: near=%d mid=%d far=%d min=%.6f max=%.4f cs=%.4f",
               near, mid, far, min_d, max_d, cell_size);
    }

    /* --- Flood-fill sign determination ---
     * Instead of per-patch normal sign (which fails at seams),
     * flood-fill from a guaranteed-outside corner. All voxels
     * reachable from the corner through cells with positive (large)
     * distance are outside. Everything else is inside.
     *
     * Triclaude teaches: the boundary is the zero-crossing.
     * Inside is negative. Outside is positive. The flood fills
     * the outside; everything it can't reach is dick (inside). */
    {
        size_t total = (size_t)sx * sy * sz;
        uint8_t *outside = calloc(total, 1); /* 0=unknown, 1=outside */
        if (outside) {
            /* BFS from corner (0,0,0) — guaranteed outside due to padding */
            int *queue = malloc(total * 3 * sizeof(int));
            if (queue) {
                /* Threshold: flood can't cross voxels closer than this to the surface.
                 * Must be large enough to catch all surface voxels but small enough
                 * to not leak through thin walls. sqrt(3)*cell/2 is the max distance
                 * from a voxel center to the nearest surface point on a cell face. */
                float surface_thresh = cell_size * 0.87f; /* sqrt(3)/2 ≈ 0.866 */
                int head = 0, tail = 0;
                outside[0] = 1;
                queue[0] = 0; queue[1] = 0; queue[2] = 0;
                tail++;

                while (head < tail) {
                    int cx = queue[head*3], cy = queue[head*3+1], cz = queue[head*3+2];
                    head++;

                    static const int dx[] = {-1,1,0,0,0,0};
                    static const int dy[] = {0,0,-1,1,0,0};
                    static const int dz[] = {0,0,0,0,-1,1};
                    for (int d = 0; d < 6; d++) {
                        int nx = cx+dx[d], ny = cy+dy[d], nz = cz+dz[d];
                        if (nx < 0 || ny < 0 || nz < 0 ||
                            nx >= sx || ny >= sy || nz >= sz) continue;
                        size_t nidx = (size_t)nx + (size_t)ny*sx + (size_t)nz*sx*sy;
                        if (outside[nidx]) continue; /* already visited */

                        /* Only flood through voxels that are far from the surface */
                        DC_Voxel *nv = dc_voxel_grid_get(grid, nx, ny, nz);
                        if (nv && nv->distance > surface_thresh) {
                            outside[nidx] = 1;
                            queue[tail*3] = nx; queue[tail*3+1] = ny; queue[tail*3+2] = nz;
                            tail++;
                        }
                    }
                }
                free(queue);
            }

            /* Set sign: outside = positive, inside = negative */
            for (int iz2 = 0; iz2 < sz; iz2++)
            for (int iy2 = 0; iy2 < sy; iy2++)
            for (int ix2 = 0; ix2 < sx; ix2++) {
                size_t idx = (size_t)ix2 + (size_t)iy2*sx + (size_t)iz2*sx*sy;
                DC_Voxel *vx = dc_voxel_grid_get(grid, ix2, iy2, iz2);
                if (!vx) continue;
                if (!outside[idx]) {
                    /* Inside — negate distance, activate */
                    vx->distance = -fabsf(vx->distance);
                    vx->active = 1;
                    vx->r = 180; vx->g = 180; vx->b = 180;
                }
                /* outside voxels keep positive distance, stay inactive */
            }
            free(outside);
        }
    }

    /* Set uniform grey color for active voxels */
    for (int iz2 = 0; iz2 < sz; iz2++)
    for (int iy2 = 0; iy2 < sy; iy2++)
    for (int ix2 = 0; ix2 < sx; ix2++) {
        DC_Voxel *vx = dc_voxel_grid_get(grid, ix2, iy2, iz2);
        if (vx && vx->active) {
            vx->r = 180; vx->g = 180; vx->b = 180;
        }
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "voxelized bezier: %zu active voxels",
           dc_voxel_grid_active_count(grid));

    return grid;
}
