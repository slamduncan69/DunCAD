#ifndef DC_VOXELIZE_STL_H
#define DC_VOXELIZE_STL_H

/*
 * voxelize_stl.h — Convert STL triangle mesh to SDF voxel grid.
 *
 * Loads an STL file, computes the signed distance field on a uniform grid,
 * and returns a DC_VoxelGrid ready for raycast rendering.
 *
 * The triangle mesh is only used as an intermediate — it is consumed
 * and discarded. No triangles survive into the rendering pipeline.
 *
 * No GTK dependency.
 */

#include "voxel/voxel.h"
#include "core/error.h"

/* Voxelize an STL file into an SDF grid.
 *
 * resolution: grid cells per longest axis dimension (e.g. 64, 128, 256)
 * The grid is sized to encompass the mesh bbox with 1-cell padding.
 *
 * Returns an owned DC_VoxelGrid with SDF distance values and active flags set.
 * Colors are set by normal direction. Returns NULL on error. */
DC_VoxelGrid *dc_voxelize_stl(const char *stl_path, int resolution,
                                DC_Error *err);

/* Voxelize from an in-memory triangle array (interleaved normal+vertex).
 * data: array of floats, 12 per triangle (nx,ny,nz, v1x,v1y,v1z, v2..., v3...)
 * num_triangles: triangle count
 * resolution: grid cells per longest axis */
DC_VoxelGrid *dc_voxelize_triangles(const float *data, int num_triangles,
                                      int resolution, DC_Error *err);

#endif /* DC_VOXELIZE_STL_H */
