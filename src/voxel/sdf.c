/*
 * sdf.c — Signed distance field operations on voxel grids.
 */

#include "voxel/sdf.h"

#include <math.h>
#include <stdlib.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */
static inline float
maxf(float a, float b) { return a > b ? a : b; }

static inline float
minf(float a, float b) { return a < b ? a : b; }

/* =========================================================================
 * SDF primitives
 * ========================================================================= */
void
dc_sdf_sphere(DC_VoxelGrid *grid,
                float cx, float cy, float cz, float radius)
{
    if (!grid || radius <= 0) return;
    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        float wx, wy, wz;
        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);
        float dx = wx - cx, dy = wy - cy, dz = wz - cz;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz) - radius;

        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->distance = dist;
    }
}

void
dc_sdf_box(DC_VoxelGrid *grid,
              float x0, float y0, float z0,
              float x1, float y1, float z1)
{
    if (!grid) return;
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
    if (z0 > z1) { float t = z0; z0 = z1; z1 = t; }

    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        float wx, wy, wz;
        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);

        float dx = maxf(x0 - wx, wx - x1);
        float dy = maxf(y0 - wy, wy - y1);
        float dz = maxf(z0 - wz, wz - z1);
        float outside = sqrtf(maxf(dx,0)*maxf(dx,0) +
                                maxf(dy,0)*maxf(dy,0) +
                                maxf(dz,0)*maxf(dz,0));
        float inside = minf(maxf(dx, maxf(dy, dz)), 0.0f);
        float dist = outside + inside;

        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->distance = dist;
    }
}

void
dc_sdf_cylinder(DC_VoxelGrid *grid,
                  float cx, float cy, float radius,
                  float z0, float z1)
{
    if (!grid || radius <= 0) return;
    if (z0 > z1) { float t = z0; z0 = z1; z1 = t; }

    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        float wx, wy, wz;
        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);

        /* Radial distance in XY */
        float dx = wx - cx, dy = wy - cy;
        float d_radial = sqrtf(dx*dx + dy*dy) - radius;

        /* Axial distance along Z */
        float d_axial = maxf(z0 - wz, wz - z1);

        /* SDF: intersection of infinite cylinder and Z slab */
        float dist = maxf(d_radial, d_axial);

        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->distance = dist;
    }
}

void
dc_sdf_torus(DC_VoxelGrid *grid,
               float cx, float cy, float cz,
               float major_r, float minor_r)
{
    if (!grid || major_r <= 0 || minor_r <= 0) return;

    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        float wx, wy, wz;
        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);

        float dx = wx - cx, dy = wy - cy, dz = wz - cz;
        float q = sqrtf(dx*dx + dy*dy) - major_r;
        float dist = sqrtf(q*q + dz*dz) - minor_r;

        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->distance = dist;
    }
}

/* =========================================================================
 * CSG operations
 * ========================================================================= */
static int
grids_match(const DC_VoxelGrid *a, const DC_VoxelGrid *b)
{
    return dc_voxel_grid_size_x(a) == dc_voxel_grid_size_x(b) &&
           dc_voxel_grid_size_y(a) == dc_voxel_grid_size_y(b) &&
           dc_voxel_grid_size_z(a) == dc_voxel_grid_size_z(b);
}

int
dc_sdf_union(const DC_VoxelGrid *a, const DC_VoxelGrid *b,
               DC_VoxelGrid *out)
{
    if (!a || !b || !out) return -1;
    if (!grids_match(a, b) || !grids_match(a, out)) return -1;

    int sx = dc_voxel_grid_size_x(a);
    int sy = dc_voxel_grid_size_y(a);
    int sz = dc_voxel_grid_size_z(a);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        const DC_Voxel *va = dc_voxel_grid_get_const(a, ix, iy, iz);
        const DC_Voxel *vb = dc_voxel_grid_get_const(b, ix, iy, iz);
        DC_Voxel *vo = dc_voxel_grid_get(out, ix, iy, iz);
        if (!va || !vb || !vo) continue;
        vo->distance = minf(va->distance, vb->distance);
    }
    return 0;
}

int
dc_sdf_intersect(const DC_VoxelGrid *a, const DC_VoxelGrid *b,
                   DC_VoxelGrid *out)
{
    if (!a || !b || !out) return -1;
    if (!grids_match(a, b) || !grids_match(a, out)) return -1;

    int sx = dc_voxel_grid_size_x(a);
    int sy = dc_voxel_grid_size_y(a);
    int sz = dc_voxel_grid_size_z(a);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        const DC_Voxel *va = dc_voxel_grid_get_const(a, ix, iy, iz);
        const DC_Voxel *vb = dc_voxel_grid_get_const(b, ix, iy, iz);
        DC_Voxel *vo = dc_voxel_grid_get(out, ix, iy, iz);
        if (!va || !vb || !vo) continue;
        vo->distance = maxf(va->distance, vb->distance);
    }
    return 0;
}

int
dc_sdf_subtract(const DC_VoxelGrid *a, const DC_VoxelGrid *b,
                  DC_VoxelGrid *out)
{
    if (!a || !b || !out) return -1;
    if (!grids_match(a, b) || !grids_match(a, out)) return -1;

    int sx = dc_voxel_grid_size_x(a);
    int sy = dc_voxel_grid_size_y(a);
    int sz = dc_voxel_grid_size_z(a);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        const DC_Voxel *va = dc_voxel_grid_get_const(a, ix, iy, iz);
        const DC_Voxel *vb = dc_voxel_grid_get_const(b, ix, iy, iz);
        DC_Voxel *vo = dc_voxel_grid_get(out, ix, iy, iz);
        if (!va || !vb || !vo) continue;
        vo->distance = maxf(va->distance, -vb->distance);
    }
    return 0;
}

/* =========================================================================
 * Activation
 * ========================================================================= */
void
dc_sdf_activate(DC_VoxelGrid *grid)
{
    if (!grid) return;
    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->active = (v->distance <= 0.0f) ? 1 : 0;
    }
}

void
dc_sdf_activate_color(DC_VoxelGrid *grid, uint8_t r, uint8_t g, uint8_t b)
{
    if (!grid) return;
    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (!v) continue;
        if (v->distance <= 0.0f) {
            v->active = 1;
            v->r = r; v->g = g; v->b = b;
        } else {
            v->active = 0;
        }
    }
}

void
dc_sdf_color_by_normal(DC_VoxelGrid *grid)
{
    if (!grid) return;
    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (!v || !v->active) continue;

        /* Approximate gradient (normal) via central differences */
        const DC_Voxel *xp = dc_voxel_grid_get_const(grid, ix+1, iy, iz);
        const DC_Voxel *xm = dc_voxel_grid_get_const(grid, ix-1, iy, iz);
        const DC_Voxel *yp = dc_voxel_grid_get_const(grid, ix, iy+1, iz);
        const DC_Voxel *ym = dc_voxel_grid_get_const(grid, ix, iy-1, iz);
        const DC_Voxel *zp = dc_voxel_grid_get_const(grid, ix, iy, iz+1);
        const DC_Voxel *zm = dc_voxel_grid_get_const(grid, ix, iy, iz-1);

        float gx = (xp ? xp->distance : v->distance) -
                    (xm ? xm->distance : v->distance);
        float gy = (yp ? yp->distance : v->distance) -
                    (ym ? ym->distance : v->distance);
        float gz = (zp ? zp->distance : v->distance) -
                    (zm ? zm->distance : v->distance);

        float len = sqrtf(gx*gx + gy*gy + gz*gz);
        if (len > 1e-6f) { gx /= len; gy /= len; gz /= len; }

        /* Map normal to color: [-1,1] → [0,255] */
        v->r = (uint8_t)((gx * 0.5f + 0.5f) * 255);
        v->g = (uint8_t)((gy * 0.5f + 0.5f) * 255);
        v->b = (uint8_t)((gz * 0.5f + 0.5f) * 255);
    }
}
