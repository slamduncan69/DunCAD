#ifndef DC_VOXELIZE_GPU_H
#define DC_VOXELIZE_GPU_H

/*
 * voxelize_gpu.h — GPU-accelerated bezier mesh voxelization.
 *
 * Uses compute shaders when available (GLES 3.1+), falls back to
 * background thread with progressive rendering otherwise.
 *
 * Both paths produce a DC_VoxelGrid. The caller provides a callback
 * that fires when voxelization is complete (may be async).
 */

#include "voxel/voxel.h"
#include "core/error.h"

/* Completion callback: called on the main thread when done.
 * grid is owned by the caller after callback. */
typedef void (*DC_VoxelizeDoneCb)(DC_VoxelGrid *grid, void *userdata);

/* Check if GPU compute is available. Must be called from GL context. */
int dc_voxelize_gpu_available(void);

/* Voxelize a bezier mesh asynchronously.
 * If GPU compute is available, uses compute shader.
 * Otherwise, spawns a background thread.
 * Calls done_cb on the main thread when complete.
 * mesh_ptr is a ts_bezier_mesh* — OWNERSHIP TRANSFERRED.
 * The mesh will be freed after voxelization completes.
 * Returns 0 on success (async started), -1 on error. */
int dc_voxelize_async(void *mesh_ptr, int resolution,
                        DC_VoxelizeDoneCb done_cb, void *userdata,
                        DC_Error *err);

/* Cancel any in-progress async voxelization. */
void dc_voxelize_async_cancel(void);

#endif /* DC_VOXELIZE_GPU_H */
