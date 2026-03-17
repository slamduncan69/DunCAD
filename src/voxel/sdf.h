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
