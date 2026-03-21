/*
 * voxel.c — 3D voxel grid data model.
 */

#include "voxel/voxel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */
struct DC_VoxelGrid {
    int       sx, sy, sz;   /* grid dimensions in cells */
    float     cell_size;    /* world units per cell */
    float     origin[3];    /* world-space position of cell (0,0,0) */
    DC_Voxel *cells;        /* flat array: cells[ix + iy*sx + iz*sx*sy] */
};

static inline int
in_bounds(const DC_VoxelGrid *g, int ix, int iy, int iz)
{
    return ix >= 0 && ix < g->sx &&
           iy >= 0 && iy < g->sy &&
           iz >= 0 && iz < g->sz;
}

static inline size_t
cell_index(const DC_VoxelGrid *g, int ix, int iy, int iz)
{
    return (size_t)ix + (size_t)iy * (size_t)g->sx +
           (size_t)iz * (size_t)g->sx * (size_t)g->sy;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_VoxelGrid *
dc_voxel_grid_new(int sx, int sy, int sz, float cell_size)
{
    if (sx <= 0 || sy <= 0 || sz <= 0 || cell_size <= 0.0f)
        return NULL;

    DC_VoxelGrid *g = calloc(1, sizeof(DC_VoxelGrid));
    if (!g) return NULL;

    g->sx = sx;
    g->sy = sy;
    g->sz = sz;
    g->cell_size = cell_size;

    size_t total = (size_t)sx * (size_t)sy * (size_t)sz;
    g->cells = calloc(total, sizeof(DC_Voxel));
    if (!g->cells) { free(g); return NULL; }

    /* Initialize all distances to +INF */
    for (size_t i = 0; i < total; i++)
        g->cells[i].distance = HUGE_VALF;

    return g;
}

void
dc_voxel_grid_free(DC_VoxelGrid *g)
{
    if (!g) return;
    free(g->cells);
    free(g);
}

/* =========================================================================
 * Dimensions
 * ========================================================================= */
int   dc_voxel_grid_size_x(const DC_VoxelGrid *g) { return g ? g->sx : 0; }
int   dc_voxel_grid_size_y(const DC_VoxelGrid *g) { return g ? g->sy : 0; }
int   dc_voxel_grid_size_z(const DC_VoxelGrid *g) { return g ? g->sz : 0; }
float dc_voxel_grid_cell_size(const DC_VoxelGrid *g) { return g ? g->cell_size : 0; }

void dc_voxel_grid_set_origin(DC_VoxelGrid *g, float ox, float oy, float oz)
{
    if (!g) return;
    g->origin[0] = ox; g->origin[1] = oy; g->origin[2] = oz;
}

void dc_voxel_grid_get_origin(const DC_VoxelGrid *g, float *ox, float *oy, float *oz)
{
    if (!g) return;
    if (ox) *ox = g->origin[0];
    if (oy) *oy = g->origin[1];
    if (oz) *oz = g->origin[2];
}

void
dc_voxel_grid_bounds(const DC_VoxelGrid *g,
                       float *min_x, float *min_y, float *min_z,
                       float *max_x, float *max_y, float *max_z)
{
    if (!g) return;
    if (min_x) *min_x = g->origin[0];
    if (min_y) *min_y = g->origin[1];
    if (min_z) *min_z = g->origin[2];
    if (max_x) *max_x = g->origin[0] + g->sx * g->cell_size;
    if (max_y) *max_y = g->origin[1] + g->sy * g->cell_size;
    if (max_z) *max_z = g->origin[2] + g->sz * g->cell_size;
}

/* =========================================================================
 * Cell access
 * ========================================================================= */
DC_Voxel *
dc_voxel_grid_get(DC_VoxelGrid *g, int ix, int iy, int iz)
{
    if (!g || !in_bounds(g, ix, iy, iz)) return NULL;
    return &g->cells[cell_index(g, ix, iy, iz)];
}

const DC_Voxel *
dc_voxel_grid_get_const(const DC_VoxelGrid *g, int ix, int iy, int iz)
{
    if (!g || !in_bounds(g, ix, iy, iz)) return NULL;
    return &g->cells[cell_index(g, ix, iy, iz)];
}

int
dc_voxel_grid_set(DC_VoxelGrid *g, int ix, int iy, int iz, DC_Voxel voxel)
{
    if (!g || !in_bounds(g, ix, iy, iz)) return -1;
    g->cells[cell_index(g, ix, iy, iz)] = voxel;
    return 0;
}

void
dc_voxel_grid_clear(DC_VoxelGrid *g)
{
    if (!g) return;
    size_t total = (size_t)g->sx * (size_t)g->sy * (size_t)g->sz;
    for (size_t i = 0; i < total; i++) {
        g->cells[i].active = 0;
        g->cells[i].r = g->cells[i].g = g->cells[i].b = 0;
        g->cells[i].distance = HUGE_VALF;
    }
}

size_t
dc_voxel_grid_active_count(const DC_VoxelGrid *g)
{
    if (!g) return 0;
    size_t total = (size_t)g->sx * (size_t)g->sy * (size_t)g->sz;
    size_t count = 0;
    for (size_t i = 0; i < total; i++)
        if (g->cells[i].active) count++;
    return count;
}

/* =========================================================================
 * Coordinate conversion
 * ========================================================================= */
int
dc_voxel_grid_world_to_cell(const DC_VoxelGrid *g,
                              float wx, float wy, float wz,
                              int *ix, int *iy, int *iz)
{
    if (!g) return -1;
    int cx = (int)floorf(wx / g->cell_size);
    int cy = (int)floorf(wy / g->cell_size);
    int cz = (int)floorf(wz / g->cell_size);
    if (ix) *ix = cx;
    if (iy) *iy = cy;
    if (iz) *iz = cz;
    return in_bounds(g, cx, cy, cz) ? 0 : -1;
}

void
dc_voxel_grid_cell_center(const DC_VoxelGrid *g,
                            int ix, int iy, int iz,
                            float *wx, float *wy, float *wz)
{
    if (!g) return;
    float half = g->cell_size * 0.5f;
    if (wx) *wx = ix * g->cell_size + half;
    if (wy) *wy = iy * g->cell_size + half;
    if (wz) *wz = iz * g->cell_size + half;
}

/* =========================================================================
 * Primitive fills
 * ========================================================================= */
void
dc_voxel_grid_fill_sphere(DC_VoxelGrid *g,
                            float cx, float cy, float cz, float radius,
                            uint8_t r, uint8_t gb, uint8_t b)
{
    if (!g || radius <= 0) return;

    for (int iz = 0; iz < g->sz; iz++) {
        for (int iy = 0; iy < g->sy; iy++) {
            for (int ix = 0; ix < g->sx; ix++) {
                float wx, wy, wz;
                dc_voxel_grid_cell_center(g, ix, iy, iz, &wx, &wy, &wz);

                float dx = wx - cx;
                float dy = wy - cy;
                float dz = wz - cz;
                float dist = sqrtf(dx*dx + dy*dy + dz*dz) - radius;

                DC_Voxel *v = &g->cells[cell_index(g, ix, iy, iz)];
                /* Store minimum distance (for SDF compositing) */
                if (dist < v->distance) {
                    v->distance = dist;
                }
                if (dist <= 0.0f) {
                    v->active = 1;
                    v->r = r;
                    v->g = gb;
                    v->b = b;
                }
            }
        }
    }
}

void
dc_voxel_grid_fill_box(DC_VoxelGrid *g,
                         float x0, float y0, float z0,
                         float x1, float y1, float z1,
                         uint8_t r, uint8_t gb, uint8_t b)
{
    if (!g) return;

    /* Ensure min < max */
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
    if (z0 > z1) { float t = z0; z0 = z1; z1 = t; }

    for (int iz = 0; iz < g->sz; iz++) {
        for (int iy = 0; iy < g->sy; iy++) {
            for (int ix = 0; ix < g->sx; ix++) {
                float wx, wy, wz;
                dc_voxel_grid_cell_center(g, ix, iy, iz, &wx, &wy, &wz);

                /* SDF for box: max of distances to each face */
                float dx = fmaxf(x0 - wx, wx - x1);
                float dy = fmaxf(y0 - wy, wy - y1);
                float dz = fmaxf(z0 - wz, wz - z1);
                float outside = sqrtf(fmaxf(dx, 0)*fmaxf(dx, 0) +
                                       fmaxf(dy, 0)*fmaxf(dy, 0) +
                                       fmaxf(dz, 0)*fmaxf(dz, 0));
                float inside = fminf(fmaxf(dx, fmaxf(dy, dz)), 0.0f);
                float dist = outside + inside;

                DC_Voxel *v = &g->cells[cell_index(g, ix, iy, iz)];
                if (dist < v->distance) {
                    v->distance = dist;
                }
                if (dist <= 0.0f) {
                    v->active = 1;
                    v->r = r;
                    v->g = gb;
                    v->b = b;
                }
            }
        }
    }
}
