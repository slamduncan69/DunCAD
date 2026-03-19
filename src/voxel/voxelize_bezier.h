#ifndef DC_VOXELIZE_BEZIER_H
#define DC_VOXELIZE_BEZIER_H

/*
 * voxelize_bezier.h — Convert bezier surface mesh to SDF voxel grid.
 *
 * Takes a Trinity Site ts_bezier_mesh and produces a DC_VoxelGrid with
 * signed distance values. The bezier math is evaluated directly — no
 * intermediate triangle mesh. The surface is lossless at every scale.
 *
 * Algorithm (per patch):
 *   1. Compute patch AABB, expand by narrowband width
 *   2. For each voxel in the expanded AABB:
 *      a. Newton-solve for closest (u,v) on the patch
 *      b. Compute signed distance via surface normal
 *      c. Min with existing value (closest surface wins)
 *   3. Activate cells where distance <= 0
 *
 * No GTK dependency.
 */

#include "voxel/voxel.h"
#include "core/error.h"

/* Forward-declare Trinity Site types to avoid header path issues.
 * The actual implementation includes the full headers. */
struct ts_bezier_mesh_fwd;

/* Voxelize a bezier mesh into an SDF grid.
 *
 * mesh: the bezier mesh (ts_bezier_mesh*)
 * resolution: grid cells per longest axis dimension
 * band_cells: narrowband width in cells (typically 2-3)
 * newton_iters: Newton iterations for closest-point solve (10-20)
 *
 * Returns an owned DC_VoxelGrid with SDF distances and active flags.
 * Colors are set by SDF gradient (normal-mapped). Returns NULL on error.
 */
DC_VoxelGrid *dc_voxelize_bezier(const void *mesh, int resolution,
                                   int band_cells, int newton_iters,
                                   DC_Error *err);

#endif /* DC_VOXELIZE_BEZIER_H */
