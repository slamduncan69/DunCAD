#define _POSIX_C_SOURCE 200809L
/*
 * voxelize_stl.c — Convert STL triangle mesh to SDF voxel grid.
 *
 * Algorithm:
 *   1. Load STL (binary or ASCII) into triangle array
 *   2. Compute bounding box, create grid with padding
 *   3. For each voxel, compute signed distance to nearest triangle
 *   4. Sign determination: ray casting (count intersections for inside/outside)
 *   5. Set active flags and normal-based colors
 *
 * The signed distance to a triangle is computed via point-to-triangle
 * projection. This is O(voxels * triangles) — acceptable for moderate
 * grids. For large meshes, a BVH would accelerate this.
 */

#include "voxel/voxelize_stl.h"
#include "voxel/sdf.h"
#include "core/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Triangle geometry helpers
 * ========================================================================= */

static float
vec3_dot(const float *a, const float *b)
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static void
vec3_sub(float *out, const float *a, const float *b)
{
    out[0] = a[0]-b[0]; out[1] = a[1]-b[1]; out[2] = a[2]-b[2];
}

static void
vec3_cross(float *out, const float *a, const float *b)
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static float
clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Unsigned distance from point p to triangle (v0, v1, v2) */
static float
point_triangle_dist(const float *p, const float *v0, const float *v1, const float *v2)
{
    float e0[3], e1[3], v[3];
    vec3_sub(e0, v1, v0);
    vec3_sub(e1, v2, v0);
    vec3_sub(v, p, v0);

    float d00 = vec3_dot(e0, e0);
    float d01 = vec3_dot(e0, e1);
    float d11 = vec3_dot(e1, e1);
    float d20 = vec3_dot(v, e0);
    float d21 = vec3_dot(v, e1);

    float denom = d00 * d11 - d01 * d01;
    if (fabsf(denom) < 1e-12f) {
        /* Degenerate triangle — distance to v0 */
        float d[3]; vec3_sub(d, p, v0);
        return sqrtf(vec3_dot(d, d));
    }

    float s = (d11 * d20 - d01 * d21) / denom;
    float t = (d00 * d21 - d01 * d20) / denom;

    /* Clamp to triangle */
    s = clampf(s, 0.0f, 1.0f);
    t = clampf(t, 0.0f, 1.0f);
    if (s + t > 1.0f) {
        float scale = 1.0f / (s + t);
        s *= scale;
        t *= scale;
    }

    /* Closest point on triangle */
    float closest[3];
    closest[0] = v0[0] + s * e0[0] + t * e1[0];
    closest[1] = v0[1] + s * e0[1] + t * e1[1];
    closest[2] = v0[2] + s * e0[2] + t * e1[2];

    float d[3];
    vec3_sub(d, p, closest);
    return sqrtf(vec3_dot(d, d));
}

/* Ray-triangle intersection for inside/outside determination.
 * Ray from p in +X direction. Returns 1 if intersects. */
static int
ray_triangle_intersect(const float *p, const float *v0, const float *v1, const float *v2)
{
    float e1[3], e2[3], h[3], s[3], q[3];
    float dir[3] = {1, 0, 0}; /* +X ray */

    vec3_sub(e1, v1, v0);
    vec3_sub(e2, v2, v0);
    vec3_cross(h, dir, e2);
    float a = vec3_dot(e1, h);
    if (fabsf(a) < 1e-8f) return 0;

    float f = 1.0f / a;
    vec3_sub(s, p, v0);
    float u = f * vec3_dot(s, h);
    if (u < 0.0f || u > 1.0f) return 0;

    vec3_cross(q, s, e1);
    float v = f * vec3_dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return 0;

    float t = f * vec3_dot(e2, q);
    return t > 1e-6f ? 1 : 0;
}

/* =========================================================================
 * STL loading (binary format)
 * ========================================================================= */
static float *
load_stl_triangles(const char *path, int *out_count)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    /* Skip 80-byte header */
    fseek(f, 80, SEEK_SET);

    uint32_t num_tris = 0;
    if (fread(&num_tris, 4, 1, f) != 1) { fclose(f); return NULL; }

    /* Each triangle: normal(3f) + v1(3f) + v2(3f) + v3(3f) + attr(2b) = 50 bytes */
    float *data = malloc((size_t)num_tris * 12 * sizeof(float));
    if (!data) { fclose(f); return NULL; }

    for (uint32_t i = 0; i < num_tris; i++) {
        float tri[12];
        if (fread(tri, sizeof(float), 12, f) != 12) {
            free(data);
            fclose(f);
            return NULL;
        }
        memcpy(data + i * 12, tri, 12 * sizeof(float));
        uint16_t attr;
        if (fread(&attr, 2, 1, f) != 1) { /* ignore */ }
    }

    fclose(f);
    *out_count = (int)num_tris;
    return data;
}

/* =========================================================================
 * Core voxelization
 * ========================================================================= */
DC_VoxelGrid *
dc_voxelize_triangles(const float *data, int num_triangles,
                        int resolution, DC_Error *err)
{
    if (!data || num_triangles <= 0 || resolution < 4) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "bad voxelize args");
        return NULL;
    }

    /* Compute bounding box */
    float bmin[3] = { 1e18f, 1e18f, 1e18f};
    float bmax[3] = {-1e18f,-1e18f,-1e18f};

    for (int i = 0; i < num_triangles; i++) {
        const float *tri = data + i * 12;
        for (int v = 0; v < 3; v++) {
            const float *p = tri + 3 + v * 3; /* skip normal */
            for (int a = 0; a < 3; a++) {
                if (p[a] < bmin[a]) bmin[a] = p[a];
                if (p[a] > bmax[a]) bmax[a] = p[a];
            }
        }
    }

    /* Add padding */
    float extent[3] = { bmax[0]-bmin[0], bmax[1]-bmin[1], bmax[2]-bmin[2] };
    float max_extent = extent[0];
    if (extent[1] > max_extent) max_extent = extent[1];
    if (extent[2] > max_extent) max_extent = extent[2];

    float pad = max_extent * 0.05f;
    for (int a = 0; a < 3; a++) { bmin[a] -= pad; bmax[a] += pad; }

    float cell_size = max_extent / (float)(resolution - 2);
    int sx = (int)ceilf((bmax[0] - bmin[0]) / cell_size) + 1;
    int sy = (int)ceilf((bmax[1] - bmin[1]) / cell_size) + 1;
    int sz = (int)ceilf((bmax[2] - bmin[2]) / cell_size) + 1;

    /* Cap grid size to prevent memory explosion */
    if (sx > 512) sx = 512;
    if (sy > 512) sy = 512;
    if (sz > 512) sz = 512;

    DC_VoxelGrid *grid = dc_voxel_grid_new(sx, sy, sz, cell_size);
    if (!grid) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "voxel grid alloc %dx%dx%d", sx, sy, sz);
        return NULL;
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "voxelizing %d triangles → %dx%dx%d grid (cell=%.4f)",
           num_triangles, sx, sy, sz, cell_size);

    /* For each voxel: compute unsigned distance to nearest triangle,
     * then determine sign via ray casting */
    for (int iz = 0; iz < sz; iz++) {
        for (int iy = 0; iy < sy; iy++) {
            for (int ix = 0; ix < sx; ix++) {
                float wx = bmin[0] + (ix + 0.5f) * cell_size;
                float wy = bmin[1] + (iy + 0.5f) * cell_size;
                float wz = bmin[2] + (iz + 0.5f) * cell_size;
                float p[3] = {wx, wy, wz};

                /* Find minimum unsigned distance */
                float min_dist = 1e18f;
                for (int t = 0; t < num_triangles; t++) {
                    const float *tri = data + t * 12;
                    const float *v0 = tri + 3;
                    const float *v1 = tri + 6;
                    const float *v2 = tri + 9;
                    float d = point_triangle_dist(p, v0, v1, v2);
                    if (d < min_dist) min_dist = d;
                }

                /* Determine sign: count ray intersections for inside/outside */
                int crossings = 0;
                for (int t = 0; t < num_triangles; t++) {
                    const float *tri = data + t * 12;
                    crossings += ray_triangle_intersect(p, tri+3, tri+6, tri+9);
                }
                float sign = (crossings % 2 == 1) ? -1.0f : 1.0f;

                DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
                if (v) {
                    v->distance = sign * min_dist;
                    v->active = (v->distance <= 0.0f) ? 1 : 0;
                }
            }
        }
    }

    /* Color by normal */
    dc_sdf_color_by_normal(grid);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "voxelized: %zu active voxels", dc_voxel_grid_active_count(grid));

    return grid;
}

DC_VoxelGrid *
dc_voxelize_stl(const char *stl_path, int resolution, DC_Error *err)
{
    if (!stl_path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL path");
        return NULL;
    }

    int num_tris = 0;
    float *data = load_stl_triangles(stl_path, &num_tris);
    if (!data || num_tris <= 0) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "failed to load STL: %s", stl_path);
        free(data);
        return NULL;
    }

    DC_VoxelGrid *grid = dc_voxelize_triangles(data, num_tris, resolution, err);
    free(data);
    return grid;
}
