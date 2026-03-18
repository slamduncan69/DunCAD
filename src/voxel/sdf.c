#define _POSIX_C_SOURCE 200809L
/*
 * sdf.c — Signed distance field operations on voxel grids.
 */

#include "voxel/sdf.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */
static inline float
maxf(float a, float b) { return a > b ? a : b; }

static inline float
minf(float a, float b) { return a < b ? a : b; }

static inline float
absf(float a) { return a < 0 ? -a : a; }

/* =========================================================================
 * 4x4 matrix helpers (column-major)
 * ========================================================================= */

/* Index into column-major 4x4: m[col*4 + row] */
#define M(m, r, c) ((m)[(c)*4 + (r)])

static void
mat4_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    M(m,0,0) = M(m,1,1) = M(m,2,2) = M(m,3,3) = 1.0f;
}

static void
mat4_mul(const float *a, const float *b, float *out)
{
    float tmp[16];
    for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
        tmp[c*4+r] = 0;
        for (int k = 0; k < 4; k++)
            tmp[c*4+r] += M(a,r,k) * M(b,k,c);
    }
    memcpy(out, tmp, 16 * sizeof(float));
}

/* Invert a 4x4 matrix. Returns 0 on success, -1 if singular. */
static int
mat4_invert(const float *m, float *inv)
{
    float tmp[16];
    tmp[0]  =  M(m,1,1)*(M(m,2,2)*M(m,3,3)-M(m,2,3)*M(m,3,2))
              -M(m,1,2)*(M(m,2,1)*M(m,3,3)-M(m,2,3)*M(m,3,1))
              +M(m,1,3)*(M(m,2,1)*M(m,3,2)-M(m,2,2)*M(m,3,1));
    tmp[4]  = -M(m,0,1)*(M(m,2,2)*M(m,3,3)-M(m,2,3)*M(m,3,2))
              +M(m,0,2)*(M(m,2,1)*M(m,3,3)-M(m,2,3)*M(m,3,1))
              -M(m,0,3)*(M(m,2,1)*M(m,3,2)-M(m,2,2)*M(m,3,1));
    tmp[8]  =  M(m,0,1)*(M(m,1,2)*M(m,3,3)-M(m,1,3)*M(m,3,2))
              -M(m,0,2)*(M(m,1,1)*M(m,3,3)-M(m,1,3)*M(m,3,1))
              +M(m,0,3)*(M(m,1,1)*M(m,3,2)-M(m,1,2)*M(m,3,1));
    tmp[12] = -M(m,0,1)*(M(m,1,2)*M(m,2,3)-M(m,1,3)*M(m,2,2))
              +M(m,0,2)*(M(m,1,1)*M(m,2,3)-M(m,1,3)*M(m,2,1))
              -M(m,0,3)*(M(m,1,1)*M(m,2,2)-M(m,1,2)*M(m,2,1));

    float det = M(m,0,0)*tmp[0] + M(m,1,0)*tmp[4] + M(m,2,0)*tmp[8] + M(m,3,0)*tmp[12];
    /* Gross but correct: only need first column of cofactor for det,
     * compute rest of cofactor matrix now. */

    tmp[1]  = -M(m,1,0)*(M(m,2,2)*M(m,3,3)-M(m,2,3)*M(m,3,2))
              +M(m,1,2)*(M(m,2,0)*M(m,3,3)-M(m,2,3)*M(m,3,0))
              -M(m,1,3)*(M(m,2,0)*M(m,3,2)-M(m,2,2)*M(m,3,0));
    tmp[5]  =  M(m,0,0)*(M(m,2,2)*M(m,3,3)-M(m,2,3)*M(m,3,2))
              -M(m,0,2)*(M(m,2,0)*M(m,3,3)-M(m,2,3)*M(m,3,0))
              +M(m,0,3)*(M(m,2,0)*M(m,3,2)-M(m,2,2)*M(m,3,0));
    tmp[9]  = -M(m,0,0)*(M(m,1,2)*M(m,3,3)-M(m,1,3)*M(m,3,2))
              +M(m,0,2)*(M(m,1,0)*M(m,3,3)-M(m,1,3)*M(m,3,0))
              -M(m,0,3)*(M(m,1,0)*M(m,3,2)-M(m,1,2)*M(m,3,0));
    tmp[13] =  M(m,0,0)*(M(m,1,2)*M(m,2,3)-M(m,1,3)*M(m,2,2))
              -M(m,0,2)*(M(m,1,0)*M(m,2,3)-M(m,1,3)*M(m,2,0))
              +M(m,0,3)*(M(m,1,0)*M(m,2,2)-M(m,1,2)*M(m,2,0));
    tmp[2]  =  M(m,1,0)*(M(m,2,1)*M(m,3,3)-M(m,2,3)*M(m,3,1))
              -M(m,1,1)*(M(m,2,0)*M(m,3,3)-M(m,2,3)*M(m,3,0))
              +M(m,1,3)*(M(m,2,0)*M(m,3,1)-M(m,2,1)*M(m,3,0));
    tmp[6]  = -M(m,0,0)*(M(m,2,1)*M(m,3,3)-M(m,2,3)*M(m,3,1))
              +M(m,0,1)*(M(m,2,0)*M(m,3,3)-M(m,2,3)*M(m,3,0))
              -M(m,0,3)*(M(m,2,0)*M(m,3,1)-M(m,2,1)*M(m,3,0));
    tmp[10] =  M(m,0,0)*(M(m,1,1)*M(m,3,3)-M(m,1,3)*M(m,3,1))
              -M(m,0,1)*(M(m,1,0)*M(m,3,3)-M(m,1,3)*M(m,3,0))
              +M(m,0,3)*(M(m,1,0)*M(m,3,1)-M(m,1,1)*M(m,3,0));
    tmp[14] = -M(m,0,0)*(M(m,1,1)*M(m,2,3)-M(m,1,3)*M(m,2,1))
              +M(m,0,1)*(M(m,1,0)*M(m,2,3)-M(m,1,3)*M(m,2,0))
              -M(m,0,3)*(M(m,1,0)*M(m,2,1)-M(m,1,1)*M(m,2,0));
    tmp[3]  = -M(m,1,0)*(M(m,2,1)*M(m,3,2)-M(m,2,2)*M(m,3,1))
              +M(m,1,1)*(M(m,2,0)*M(m,3,2)-M(m,2,2)*M(m,3,0))
              -M(m,1,2)*(M(m,2,0)*M(m,3,1)-M(m,2,1)*M(m,3,0));
    tmp[7]  =  M(m,0,0)*(M(m,2,1)*M(m,3,2)-M(m,2,2)*M(m,3,1))
              -M(m,0,1)*(M(m,2,0)*M(m,3,2)-M(m,2,2)*M(m,3,0))
              +M(m,0,2)*(M(m,2,0)*M(m,3,1)-M(m,2,1)*M(m,3,0));
    tmp[11] = -M(m,0,0)*(M(m,1,1)*M(m,3,2)-M(m,1,2)*M(m,3,1))
              +M(m,0,1)*(M(m,1,0)*M(m,3,2)-M(m,1,2)*M(m,3,0))
              -M(m,0,2)*(M(m,1,0)*M(m,3,1)-M(m,1,1)*M(m,3,0));
    tmp[15] =  M(m,0,0)*(M(m,1,1)*M(m,2,2)-M(m,1,2)*M(m,2,1))
              -M(m,0,1)*(M(m,1,0)*M(m,2,2)-M(m,1,2)*M(m,2,0))
              +M(m,0,2)*(M(m,1,0)*M(m,2,1)-M(m,1,1)*M(m,2,0));

    if (absf(det) < 1e-12f) return -1;
    float inv_det = 1.0f / det;
    for (int i = 0; i < 16; i++) inv[i] = tmp[i] * inv_det;
    return 0;
}

/* Compute max absolute scale factor from columns of upper-left 3x3. */
static float
mat4_max_scale(const float *m)
{
    float sx = sqrtf(M(m,0,0)*M(m,0,0) + M(m,1,0)*M(m,1,0) + M(m,2,0)*M(m,2,0));
    float sy = sqrtf(M(m,0,1)*M(m,0,1) + M(m,1,1)*M(m,1,1) + M(m,2,1)*M(m,2,1));
    float sz = sqrtf(M(m,0,2)*M(m,0,2) + M(m,1,2)*M(m,1,2) + M(m,2,2)*M(m,2,2));
    return maxf(sx, maxf(sy, sz));
}

/* =========================================================================
 * SDF Transform API
 * ========================================================================= */

void
dc_sdf_transform_identity(DC_SdfTransform *t)
{
    if (!t) return;
    mat4_identity(t->mat);
    mat4_identity(t->inv);
    t->scale = 1.0f;
}

/* Internal: recompute inverse and scale from mat. */
static void
transform_update(DC_SdfTransform *t)
{
    mat4_invert(t->mat, t->inv);
    t->scale = mat4_max_scale(t->mat);
    if (t->scale < 1e-6f) t->scale = 1.0f;
}

void
dc_sdf_transform_translate(DC_SdfTransform *t, float tx, float ty, float tz)
{
    if (!t) return;
    float tr[16];
    mat4_identity(tr);
    M(tr,0,3) = tx; M(tr,1,3) = ty; M(tr,2,3) = tz;
    float tmp[16];
    mat4_mul(t->mat, tr, tmp);
    memcpy(t->mat, tmp, 16 * sizeof(float));
    transform_update(t);
}

void
dc_sdf_transform_rotate(DC_SdfTransform *t,
                           float ax, float ay, float az, float angle_deg)
{
    if (!t) return;
    /* Normalize axis */
    float len = sqrtf(ax*ax + ay*ay + az*az);
    if (len < 1e-6f) return;
    ax /= len; ay /= len; az /= len;

    float rad = angle_deg * (float)(M_PI / 180.0);
    float c = cosf(rad), s = sinf(rad), ic = 1.0f - c;

    float rot[16];
    mat4_identity(rot);
    M(rot,0,0) = ax*ax*ic + c;      M(rot,0,1) = ax*ay*ic - az*s;  M(rot,0,2) = ax*az*ic + ay*s;
    M(rot,1,0) = ay*ax*ic + az*s;   M(rot,1,1) = ay*ay*ic + c;     M(rot,1,2) = ay*az*ic - ax*s;
    M(rot,2,0) = az*ax*ic - ay*s;   M(rot,2,1) = az*ay*ic + ax*s;  M(rot,2,2) = az*az*ic + c;

    float tmp[16];
    mat4_mul(t->mat, rot, tmp);
    memcpy(t->mat, tmp, 16 * sizeof(float));
    transform_update(t);
}

void
dc_sdf_transform_scale(DC_SdfTransform *t, float sx, float sy, float sz)
{
    if (!t) return;
    float sc[16];
    mat4_identity(sc);
    M(sc,0,0) = sx; M(sc,1,1) = sy; M(sc,2,2) = sz;
    float tmp[16];
    mat4_mul(t->mat, sc, tmp);
    memcpy(t->mat, tmp, 16 * sizeof(float));
    transform_update(t);
}

void
dc_sdf_transform_compose(const DC_SdfTransform *a,
                            const DC_SdfTransform *b,
                            DC_SdfTransform *out)
{
    if (!a || !b || !out) return;
    DC_SdfTransform tmp;
    mat4_mul(a->mat, b->mat, tmp.mat);
    transform_update(&tmp);
    memcpy(out, &tmp, sizeof(DC_SdfTransform));
}

void
dc_sdf_transform_inv_point(const DC_SdfTransform *t,
                              float wx, float wy, float wz,
                              float *lx, float *ly, float *lz)
{
    if (!t) { *lx = wx; *ly = wy; *lz = wz; return; }
    *lx = M(t->inv,0,0)*wx + M(t->inv,0,1)*wy + M(t->inv,0,2)*wz + M(t->inv,0,3);
    *ly = M(t->inv,1,0)*wx + M(t->inv,1,1)*wy + M(t->inv,1,2)*wz + M(t->inv,1,3);
    *lz = M(t->inv,2,0)*wx + M(t->inv,2,1)*wy + M(t->inv,2,2)*wz + M(t->inv,2,3);
}

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
        if (v) v->distance = minf(v->distance, dist);
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
        if (v) v->distance = minf(v->distance, dist);
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
        if (v) v->distance = minf(v->distance, dist);
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
        if (v) v->distance = minf(v->distance, dist);
    }
}

/* =========================================================================
 * SDF primitives with transforms
 *
 * These MIN the new distance into each cell, composing with existing
 * SDF content. This enables building complex shapes on a single grid.
 * ========================================================================= */

/* Helper: evaluate sphere SDF at a local-space point */
static inline float
sdf_sphere_eval(float px, float py, float pz,
                float cx, float cy, float cz, float radius)
{
    float dx = px - cx, dy = py - cy, dz = pz - cz;
    return sqrtf(dx*dx + dy*dy + dz*dz) - radius;
}

/* Helper: evaluate box SDF at a local-space point */
static inline float
sdf_box_eval(float px, float py, float pz,
             float x0, float y0, float z0,
             float x1, float y1, float z1)
{
    float dx = maxf(x0 - px, px - x1);
    float dy = maxf(y0 - py, py - y1);
    float dz = maxf(z0 - pz, pz - z1);
    float outside = sqrtf(maxf(dx,0)*maxf(dx,0) +
                           maxf(dy,0)*maxf(dy,0) +
                           maxf(dz,0)*maxf(dz,0));
    float inside = minf(maxf(dx, maxf(dy, dz)), 0.0f);
    return outside + inside;
}

/* Helper: evaluate cylinder SDF at a local-space point */
static inline float
sdf_cylinder_eval(float px, float py, float pz,
                  float cx, float cy, float radius,
                  float z0, float z1)
{
    float dx = px - cx, dy = py - cy;
    float d_radial = sqrtf(dx*dx + dy*dy) - radius;
    float d_axial = maxf(z0 - pz, pz - z1);
    return maxf(d_radial, d_axial);
}

/* Helper: evaluate torus SDF at a local-space point */
static inline float
sdf_torus_eval(float px, float py, float pz,
               float cx, float cy, float cz,
               float major_r, float minor_r)
{
    float dx = px - cx, dy = py - cy, dz = pz - cz;
    float q = sqrtf(dx*dx + dy*dy) - major_r;
    return sqrtf(q*q + dz*dz) - minor_r;
}

void
dc_sdf_sphere_t(DC_VoxelGrid *grid,
                  float cx, float cy, float cz, float radius,
                  const DC_SdfTransform *t)
{
    if (!grid || radius <= 0) return;
    int gsx = dc_voxel_grid_size_x(grid);
    int gsy = dc_voxel_grid_size_y(grid);
    int gsz = dc_voxel_grid_size_z(grid);
    float sc = t ? t->scale : 1.0f;

    for (int iz = 0; iz < gsz; iz++)
    for (int iy = 0; iy < gsy; iy++)
    for (int ix = 0; ix < gsx; ix++) {
        float wx, wy, wz;
        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);

        float lx, ly, lz;
        if (t) dc_sdf_transform_inv_point(t, wx, wy, wz, &lx, &ly, &lz);
        else { lx = wx; ly = wy; lz = wz; }

        float dist = sdf_sphere_eval(lx, ly, lz, cx, cy, cz, radius) * sc;

        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->distance = minf(v->distance, dist);
    }
}

void
dc_sdf_box_t(DC_VoxelGrid *grid,
               float x0, float y0, float z0,
               float x1, float y1, float z1,
               const DC_SdfTransform *t)
{
    if (!grid) return;
    if (x0 > x1) { float tmp = x0; x0 = x1; x1 = tmp; }
    if (y0 > y1) { float tmp = y0; y0 = y1; y1 = tmp; }
    if (z0 > z1) { float tmp = z0; z0 = z1; z1 = tmp; }

    int gsx = dc_voxel_grid_size_x(grid);
    int gsy = dc_voxel_grid_size_y(grid);
    int gsz = dc_voxel_grid_size_z(grid);
    float sc = t ? t->scale : 1.0f;

    for (int iz = 0; iz < gsz; iz++)
    for (int iy = 0; iy < gsy; iy++)
    for (int ix = 0; ix < gsx; ix++) {
        float wx, wy, wz;
        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);

        float lx, ly, lz;
        if (t) dc_sdf_transform_inv_point(t, wx, wy, wz, &lx, &ly, &lz);
        else { lx = wx; ly = wy; lz = wz; }

        float dist = sdf_box_eval(lx, ly, lz, x0, y0, z0, x1, y1, z1) * sc;

        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->distance = minf(v->distance, dist);
    }
}

void
dc_sdf_cylinder_t(DC_VoxelGrid *grid,
                    float cx, float cy, float radius,
                    float z0, float z1,
                    const DC_SdfTransform *t)
{
    if (!grid || radius <= 0) return;
    if (z0 > z1) { float tmp = z0; z0 = z1; z1 = tmp; }

    int gsx = dc_voxel_grid_size_x(grid);
    int gsy = dc_voxel_grid_size_y(grid);
    int gsz = dc_voxel_grid_size_z(grid);
    float sc = t ? t->scale : 1.0f;

    for (int iz = 0; iz < gsz; iz++)
    for (int iy = 0; iy < gsy; iy++)
    for (int ix = 0; ix < gsx; ix++) {
        float wx, wy, wz;
        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);

        float lx, ly, lz;
        if (t) dc_sdf_transform_inv_point(t, wx, wy, wz, &lx, &ly, &lz);
        else { lx = wx; ly = wy; lz = wz; }

        float dist = sdf_cylinder_eval(lx, ly, lz, cx, cy, radius, z0, z1) * sc;

        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->distance = minf(v->distance, dist);
    }
}

void
dc_sdf_torus_t(DC_VoxelGrid *grid,
                 float cx, float cy, float cz,
                 float major_r, float minor_r,
                 const DC_SdfTransform *t)
{
    if (!grid || major_r <= 0 || minor_r <= 0) return;

    int gsx = dc_voxel_grid_size_x(grid);
    int gsy = dc_voxel_grid_size_y(grid);
    int gsz = dc_voxel_grid_size_z(grid);
    float sc = t ? t->scale : 1.0f;

    for (int iz = 0; iz < gsz; iz++)
    for (int iy = 0; iy < gsy; iy++)
    for (int ix = 0; ix < gsx; ix++) {
        float wx, wy, wz;
        dc_voxel_grid_cell_center(grid, ix, iy, iz, &wx, &wy, &wz);

        float lx, ly, lz;
        if (t) dc_sdf_transform_inv_point(t, wx, wy, wz, &lx, &ly, &lz);
        else { lx = wx; ly = wy; lz = wz; }

        float dist = sdf_torus_eval(lx, ly, lz, cx, cy, cz, major_r, minor_r) * sc;

        DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
        if (v) v->distance = minf(v->distance, dist);
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
