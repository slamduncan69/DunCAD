#ifndef DC_VOXEL_H
#define DC_VOXEL_H

/*
 * voxel.h — 3D voxel grid data model.
 *
 * A uniform grid of voxel cells in world space. Each cell stores an
 * active flag, RGB color, and signed distance value. The grid origin
 * is at (0,0,0); cell (ix,iy,iz) covers the axis-aligned box:
 *   [ix*cell_size, (ix+1)*cell_size] x [iy*...] x [iz*...]
 *
 * No GTK dependency — pure C, testable from CLI.
 *
 * Ownership: dc_voxel_grid_new() returns an owned grid.
 * dc_voxel_grid_free() releases all memory.
 */

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * DC_Voxel — a single voxel cell
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  active;    /* 1 = solid, 0 = empty */
    uint8_t  r, g, b;   /* color (0-255) */
    float    distance;   /* signed distance to nearest surface */
} DC_Voxel;

/* -------------------------------------------------------------------------
 * DC_VoxelGrid — opaque 3D grid
 * ---------------------------------------------------------------------- */
typedef struct DC_VoxelGrid DC_VoxelGrid;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Create a new grid with sx*sy*sz cells. cell_size is world units per cell.
 * All cells initialized to inactive, distance = +INF. */
DC_VoxelGrid *dc_voxel_grid_new(int sx, int sy, int sz, float cell_size);

/* Free the grid. Safe with NULL. */
void dc_voxel_grid_free(DC_VoxelGrid *grid);

/* =========================================================================
 * Dimensions
 * ========================================================================= */

int   dc_voxel_grid_size_x(const DC_VoxelGrid *grid);
int   dc_voxel_grid_size_y(const DC_VoxelGrid *grid);
int   dc_voxel_grid_size_z(const DC_VoxelGrid *grid);
float dc_voxel_grid_cell_size(const DC_VoxelGrid *grid);

/* World-space origin — position of cell (0,0,0) corner. */
void dc_voxel_grid_set_origin(DC_VoxelGrid *grid, float ox, float oy, float oz);
void dc_voxel_grid_get_origin(const DC_VoxelGrid *grid, float *ox, float *oy, float *oz);

/* World-space bounding box: min corner and max corner. */
void dc_voxel_grid_bounds(const DC_VoxelGrid *grid,
                            float *min_x, float *min_y, float *min_z,
                            float *max_x, float *max_y, float *max_z);

/* =========================================================================
 * Cell access
 * ========================================================================= */

/* Get a pointer to the voxel at (ix, iy, iz). Returns NULL if out of bounds.
 * Borrowed pointer — valid until grid is freed. */
DC_Voxel *dc_voxel_grid_get(DC_VoxelGrid *grid, int ix, int iy, int iz);
const DC_Voxel *dc_voxel_grid_get_const(const DC_VoxelGrid *grid,
                                          int ix, int iy, int iz);

/* Set a voxel. Returns 0 on success, -1 if out of bounds. */
int dc_voxel_grid_set(DC_VoxelGrid *grid, int ix, int iy, int iz,
                        DC_Voxel voxel);

/* Clear all cells to inactive, distance = +INF. */
void dc_voxel_grid_clear(DC_VoxelGrid *grid);

/* Count active voxels. */
size_t dc_voxel_grid_active_count(const DC_VoxelGrid *grid);

/* =========================================================================
 * World <-> grid coordinate conversion
 * ========================================================================= */

/* World position (wx,wy,wz) -> grid cell indices (ix,iy,iz).
 * Returns 0 if inside grid, -1 if outside. */
int dc_voxel_grid_world_to_cell(const DC_VoxelGrid *grid,
                                  float wx, float wy, float wz,
                                  int *ix, int *iy, int *iz);

/* Grid cell center in world coordinates. */
void dc_voxel_grid_cell_center(const DC_VoxelGrid *grid,
                                 int ix, int iy, int iz,
                                 float *wx, float *wy, float *wz);

/* =========================================================================
 * Primitive fill operations (convenience for testing)
 * ========================================================================= */

/* Fill a sphere: all cells within radius of center become active.
 * Color is applied to filled cells. */
void dc_voxel_grid_fill_sphere(DC_VoxelGrid *grid,
                                 float cx, float cy, float cz, float radius,
                                 uint8_t r, uint8_t g, uint8_t b);

/* Fill an axis-aligned box. */
void dc_voxel_grid_fill_box(DC_VoxelGrid *grid,
                              float x0, float y0, float z0,
                              float x1, float y1, float z1,
                              uint8_t r, uint8_t g, uint8_t b);

#endif /* DC_VOXEL_H */
