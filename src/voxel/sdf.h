#ifndef DC_SDF_H
#define DC_SDF_H

/*
 * sdf.h — Signed distance field operations on voxel grids.
 *
 * SDF primitives fill a grid with signed distance values.
 * CSG operations combine grids: union = min, intersect = max,
 * subtract = max(a, -b).
 *
 * After SDF operations, call dc_sdf_activate() to set active flags
 * based on distance < 0 (inside surface).
 *
 * No GTK dependency.
 */

#include "voxel/voxel.h"

/* =========================================================================
 * SDF transforms — position, rotate, and scale SDF primitives
 * ========================================================================= */

/* 4x4 column-major transform matrix with precomputed inverse.
 * The inverse transforms sample points back to local SDF space. */
typedef struct {
    float mat[16];  /* forward transform (column-major) */
    float inv[16];  /* inverse transform (column-major) */
    float scale;    /* max absolute scale factor (for distance correction) */
} DC_SdfTransform;

/* Initialize to identity. */
void dc_sdf_transform_identity(DC_SdfTransform *t);

/* Apply translation. Composes with existing transform. */
void dc_sdf_transform_translate(DC_SdfTransform *t, float tx, float ty, float tz);

/* Apply rotation around axis (ax,ay,az) by angle_deg degrees.
 * Axis need not be normalized. Composes with existing transform. */
void dc_sdf_transform_rotate(DC_SdfTransform *t,
                                float ax, float ay, float az, float angle_deg);

/* Apply non-uniform scale. Composes with existing transform. */
void dc_sdf_transform_scale(DC_SdfTransform *t, float sx, float sy, float sz);

/* Compose: out = a * b. out may alias a or b. */
void dc_sdf_transform_compose(const DC_SdfTransform *a,
                                 const DC_SdfTransform *b,
                                 DC_SdfTransform *out);

/* Transform a point by the inverse matrix (world -> local). */
void dc_sdf_transform_inv_point(const DC_SdfTransform *t,
                                  float wx, float wy, float wz,
                                  float *lx, float *ly, float *lz);

/* =========================================================================
 * SDF primitives — fill grid with signed distance values
 * ========================================================================= */

/* Sphere SDF: distance = |p - center| - radius */
void dc_sdf_sphere(DC_VoxelGrid *grid,
                     float cx, float cy, float cz, float radius);

/* Axis-aligned box SDF */
void dc_sdf_box(DC_VoxelGrid *grid,
                  float x0, float y0, float z0,
                  float x1, float y1, float z1);

/* Cylinder SDF: axis-aligned along Z, centered at (cx,cy), height z0..z1 */
void dc_sdf_cylinder(DC_VoxelGrid *grid,
                       float cx, float cy, float radius,
                       float z0, float z1);

/* Torus SDF: centered at origin in XY plane, major radius R, tube radius r */
void dc_sdf_torus(DC_VoxelGrid *grid,
                    float cx, float cy, float cz,
                    float major_r, float minor_r);

/* =========================================================================
 * SDF primitives with transforms — sample in transformed local space
 *
 * These MIN the new distance into each cell (CSG union with existing
 * content), so you can compose multiple primitives on one grid.
 * Pass NULL transform for identity (equivalent to non-_t version).
 * ========================================================================= */

void dc_sdf_sphere_t(DC_VoxelGrid *grid,
                       float cx, float cy, float cz, float radius,
                       const DC_SdfTransform *t);

void dc_sdf_box_t(DC_VoxelGrid *grid,
                    float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    const DC_SdfTransform *t);

void dc_sdf_cylinder_t(DC_VoxelGrid *grid,
                         float cx, float cy, float radius,
                         float z0, float z1,
                         const DC_SdfTransform *t);

void dc_sdf_torus_t(DC_VoxelGrid *grid,
                      float cx, float cy, float cz,
                      float major_r, float minor_r,
                      const DC_SdfTransform *t);

/* =========================================================================
 * CSG operations — combine two grids into output
 * ========================================================================= */

/* Union: out[i] = min(a[i], b[i]).
 * All three grids must have same dimensions.
 * out may alias a or b. Returns 0 on success, -1 on dimension mismatch. */
int dc_sdf_union(const DC_VoxelGrid *a, const DC_VoxelGrid *b,
                   DC_VoxelGrid *out);

/* Intersection: out[i] = max(a[i], b[i]). */
int dc_sdf_intersect(const DC_VoxelGrid *a, const DC_VoxelGrid *b,
                       DC_VoxelGrid *out);

/* Subtraction: out[i] = max(a[i], -b[i]). */
int dc_sdf_subtract(const DC_VoxelGrid *a, const DC_VoxelGrid *b,
                      DC_VoxelGrid *out);

/* =========================================================================
 * Activation — set active flags from SDF distance values
 * ========================================================================= */

/* Set active = (distance <= 0) for all cells. */
void dc_sdf_activate(DC_VoxelGrid *grid);

/* Set active = (distance <= 0) and assign uniform color. */
void dc_sdf_activate_color(DC_VoxelGrid *grid, uint8_t r, uint8_t g, uint8_t b);

/* Set color based on SDF gradient (normals → RGB). For visualization. */
void dc_sdf_color_by_normal(DC_VoxelGrid *grid);

#endif /* DC_SDF_H */
