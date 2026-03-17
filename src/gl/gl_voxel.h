#ifndef DC_GL_VOXEL_H
#define DC_GL_VOXEL_H

/*
 * gl_voxel.h — GPU-instanced voxel renderer for DC_GlViewport.
 *
 * Renders active voxels as instanced cubes. Each active voxel becomes
 * one instance with position (cell center) and color (RGB). Uses the
 * existing mesh shader with a model matrix per instance.
 *
 * Usage:
 *   DC_GlVoxelBuf *buf = dc_gl_voxel_buf_new();
 *   dc_gl_voxel_buf_upload(buf, grid);   // builds GPU buffers
 *   dc_gl_voxel_buf_draw(buf, mvp, ...); // renders instanced cubes
 *   dc_gl_voxel_buf_free(buf);
 *
 * The buf must be created/drawn from the GL context thread.
 */

#include <epoxy/gl.h>

struct DC_VoxelGrid;

/* Opaque handle to GPU voxel buffers */
typedef struct DC_GlVoxelBuf DC_GlVoxelBuf;

/* Create a new voxel buffer (no GPU state yet — call upload). */
DC_GlVoxelBuf *dc_gl_voxel_buf_new(void);

/* Free GPU buffers and the struct. Safe with NULL. */
void dc_gl_voxel_buf_free(DC_GlVoxelBuf *buf);

/* Upload a voxel grid to GPU. Rebuilds all buffers.
 * Must be called from GL context. Returns 0 on success. */
int dc_gl_voxel_buf_upload(DC_GlVoxelBuf *buf,
                             const struct DC_VoxelGrid *grid);

/* Get the number of active voxel instances. */
int dc_gl_voxel_buf_instance_count(const DC_GlVoxelBuf *buf);

/* Draw the voxel instances.
 * view_proj: 4x4 view-projection matrix (column-major)
 * light_dir: 3-float normalized light direction
 * view_pos:  3-float camera position (for specular)
 * mesh_prog: the compiled mesh shader program from gl_viewport */
void dc_gl_voxel_buf_draw(const DC_GlVoxelBuf *buf,
                            const float *view_proj,
                            const float *light_dir,
                            const float *view_pos,
                            GLuint mesh_prog);

/* Get the world-space bounding box of uploaded voxels. */
void dc_gl_voxel_buf_bounds(const DC_GlVoxelBuf *buf,
                              float *min_out, float *max_out);

#endif /* DC_GL_VOXEL_H */
