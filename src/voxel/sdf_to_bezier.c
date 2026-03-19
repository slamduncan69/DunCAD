/*
 * sdf_to_bezier.c — SDF grid → marching cubes → bezier patch fitting.
 */

#include "voxel/sdf_to_bezier.h"
#include "voxel/marching_cubes.h"
#include "core/error.h"

/* Trinity Site headers (header-only) */
#include "../../talmud-main/talmud/sacred/trinity_site/ts_mesh.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_mesh.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_fit.h"

#include <stdlib.h>

void *dc_sdf_to_bezier(const DC_VoxelGrid *grid,
                         int patch_rows, int patch_cols,
                         DC_Error *err)
{
    if (!grid) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL grid");
        return NULL;
    }

    /* Step 1: Marching cubes — extract isosurface as triangle mesh */
    ts_mesh trimesh = ts_mesh_init();
    int mc_rc = dc_marching_cubes(grid, 0.0f, &trimesh);
    if (mc_rc != 0 || trimesh.vert_count == 0) {
        ts_mesh_free(&trimesh);
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY,
                              "marching cubes produced no geometry");
        return NULL;
    }

    /* Step 2: Fit bezier patches to the triangle mesh */
    ts_bezier_mesh *mesh = (ts_bezier_mesh *)malloc(sizeof(ts_bezier_mesh));
    if (!mesh) {
        ts_mesh_free(&trimesh);
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "allocation failed");
        return NULL;
    }

    int fit_rc = ts_bezier_fit_from_trimesh(&trimesh, patch_rows, patch_cols, mesh);
    ts_mesh_free(&trimesh);

    if (fit_rc != 0) {
        free(mesh);
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "bezier fitting failed");
        return NULL;
    }

    return mesh;
}
