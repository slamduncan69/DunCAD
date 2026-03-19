#ifndef DC_SDF_TO_BEZIER_H
#define DC_SDF_TO_BEZIER_H

/*
 * sdf_to_bezier.h — Convert SDF voxel grid to editable bezier mesh.
 *
 * Bridge module combining marching cubes + bezier patch fitting.
 * SDF grid → triangle mesh (MC) → bezier patch grid (least-squares fit).
 *
 * No GTK dependency.
 */

#include "voxel/voxel.h"
#include "core/error.h"

/*
 * Convert SDF grid to editable bezier mesh.
 *
 * Runs marching cubes to extract the isosurface, then fits a
 * (patch_rows × patch_cols) grid of quadratic bezier patches.
 *
 * Parameters:
 *   grid       - SDF voxel grid (read-only)
 *   patch_rows - number of patch rows in output
 *   patch_cols - number of patch columns in output
 *   err        - error output (may be NULL)
 *
 * Returns: pointer to a newly allocated ts_bezier_mesh (caller must
 *          free with ts_bezier_mesh_free + free). NULL on error.
 */
void *dc_sdf_to_bezier(const DC_VoxelGrid *grid,
                         int patch_rows, int patch_cols,
                         DC_Error *err);

#endif /* DC_SDF_TO_BEZIER_H */
