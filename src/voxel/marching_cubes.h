#ifndef DC_MARCHING_CUBES_H
#define DC_MARCHING_CUBES_H

/*
 * marching_cubes.h — Isosurface extraction from SDF voxel grids.
 *
 * Standard marching cubes algorithm: walks each 2×2×2 cell neighborhood,
 * builds a case index from SDF signs, looks up edge/triangle tables,
 * interpolates vertex positions, computes normals from SDF gradient.
 *
 * Output is a ts_mesh (triangle mesh with normals).
 *
 * No GTK dependency — pure C, testable from CLI.
 */

#include "voxel/voxel.h"

/* Forward declare ts_mesh to avoid pulling in the full header here.
 * Callers must include ts_mesh.h before calling dc_marching_cubes(). */
struct ts_mesh_tag;  /* not used — ts_mesh is a typedef'd struct, not a tag */

/*
 * Extract isosurface at the given iso_level from an SDF grid.
 *
 * The SDF is stored in each voxel's `distance` field.
 * Vertices are placed at the iso_level crossing via linear interpolation.
 * Normals are computed from SDF gradient (central differences).
 *
 * Parameters:
 *   grid      - SDF voxel grid (read-only)
 *   iso_level - distance value of the isosurface (typically 0.0)
 *   out       - output triangle mesh (must be initialized with ts_mesh_init)
 *
 * Returns 0 on success, -1 on error (NULL grid).
 */
int dc_marching_cubes(const DC_VoxelGrid *grid, float iso_level, void *out);

#endif /* DC_MARCHING_CUBES_H */
