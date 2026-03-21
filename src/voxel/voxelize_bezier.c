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
    if (band_cells < 1) band_cells = 1;
    if (band_cells > 2) band_cells = 2; /* larger bands leak into interior */
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
    if (newton_iters < 1) newton_iters = 15;

    double band = band_cells * (double)cell_size;

    /* For each patch: narrow-band unsigned distance evaluation.
     * Use the mesh's closest-point function which tests ALL patches and
     * returns the globally closest point. This avoids the grid topology
     * issue where adjacent patches create false internal surfaces. */
    for (int iz = 0; iz < sz; iz++) {
        for (int iy = 0; iy < sy; iy++) {
            for (int ix = 0; ix < sx; ix++) {
                float wx, wy, wz;
                /* Compute world position manually — cell_center doesn't include origin */
                wx = ox + ((float)ix + 0.5f) * cell_size;
                wy = oy + ((float)iy + 0.5f) * cell_size;
                wz = oz + ((float)iz + 0.5f) * cell_size;
                ts_vec3 query = ts_vec3_make(wx, wy, wz);

                /* Find closest point on ANY patch in the mesh */
                double best_dist = 1e30;
                for (int pr = 0; pr < mesh->rows; pr++) {
                    for (int pc = 0; pc < mesh->cols; pc++) {
                        ts_bezier_patch patch = ts_bezier_mesh_get_patch(mesh, pr, pc);

                        /* Quick AABB rejection */
                        ts_vec3 pmin, pmax;
                        ts_bezier_patch_bbox(&patch, &pmin, &pmax);
                        double dx2 = fmax(pmin.v[0]-wx, fmax(0, wx-pmax.v[0]));
                        double dy2 = fmax(pmin.v[1]-wy, fmax(0, wy-pmax.v[1]));
                        double dz2 = fmax(pmin.v[2]-wz, fmax(0, wz-pmax.v[2]));
                        if (sqrt(dx2*dx2+dy2*dy2+dz2*dz2) > best_dist + band) continue;

                        double u, v;
                        ts_bezier_patch_closest_uv(&patch, query, &u, &v, newton_iters);
                        ts_vec3 closest = ts_bezier_patch_eval(&patch, u, v);
                        double dist = ts_vec3_distance(closest, query);
                        if (dist < best_dist) best_dist = dist;
                    }
                }

                DC_Voxel *vx = dc_voxel_grid_get(grid, ix, iy, iz);
                if (vx && (float)best_dist < fabsf(vx->distance)) {
                    vx->distance = (float)best_dist;
                }
            }
        }
    }

    /* --- Gap-closing pass ---
     * At mesh edges/corners, the surface sampling may leave single-cell
     * gaps where no surface sample was close enough. Close these gaps by
     * propagating distances: if a voxel has large distance but a neighbor
     * has small distance, the voxel is likely a gap in the surface shell.
     * Set its distance to neighbor + cell_size to seal the gap. */
    {
        int closed = 0;
        for (int pass = 0; pass < 3; pass++) { /* multiple passes for wider gaps */
            for (int iz2 = 0; iz2 < sz; iz2++)
            for (int iy2 = 0; iy2 < sy; iy2++)
            for (int ix2 = 0; ix2 < sx; ix2++) {
                DC_Voxel *vx = dc_voxel_grid_get(grid, ix2, iy2, iz2);
                if (!vx) continue;
                if (vx->distance <= cell_size * 1.5f) continue; /* already near surface */

                /* Check 6-connected neighbors for surface proximity */
                static const int ddx[] = {-1,1,0,0,0,0};
                static const int ddy[] = {0,0,-1,1,0,0};
                static const int ddz[] = {0,0,0,0,-1,1};
                for (int d = 0; d < 6; d++) {
                    int nx = ix2+ddx[d], ny = iy2+ddy[d], nz = iz2+ddz[d];
                    if (nx < 0 || ny < 0 || nz < 0 ||
                        nx >= sx || ny >= sy || nz >= sz) continue;
                    DC_Voxel *nv = dc_voxel_grid_get(grid, nx, ny, nz);
                    if (nv && nv->distance < cell_size) {
                        /* This voxel is next to a surface voxel but has large distance.
                         * It's a gap. Set its distance to propagate the surface shell. */
                        float prop = nv->distance + cell_size;
                        if (prop < vx->distance) {
                            vx->distance = prop;
                            closed++;
                        }
                        break;
                    }
                }
            }
        }
        if (closed > 0) {
            dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                   "voxelize_bezier: closed %d surface gaps", closed);
        }
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
