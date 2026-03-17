#ifndef DC_GL_VOXEL_H
#define DC_GL_VOXEL_H

/*
 * gl_voxel.h — SDF raycast voxel renderer.
 *
 * Pure volumetric rendering. No mesh geometry. The entire scene is a
 * 3D texture containing signed distance values. A fullscreen quad
 * invokes a fragment shader that marches rays through the volume,
 * finds surface intersections via SDF zero-crossings, computes normals
 * from the SDF gradient, and lights the result with Phong shading.
 *
 * The voxel grid is uploaded as a GL_TEXTURE_3D. The shader reads it
 * with trilinear interpolation, giving sub-voxel surface precision
 * for free. No cubes. No triangles. No instancing. Just math and light.
 *
 * Usage:
 *   DC_GlVoxelBuf *buf = dc_gl_voxel_buf_new();
 *   dc_gl_voxel_buf_upload(buf, grid);
 *   dc_gl_voxel_buf_draw(buf, view_proj_inv, eye, ...);
 *   dc_gl_voxel_buf_free(buf);
 */

#include <epoxy/gl.h>

struct DC_VoxelGrid;

typedef struct DC_GlVoxelBuf DC_GlVoxelBuf;

DC_GlVoxelBuf *dc_gl_voxel_buf_new(void);
void dc_gl_voxel_buf_free(DC_GlVoxelBuf *buf);

/* Upload voxel grid as 3D texture. Must be called from GL context. */
int dc_gl_voxel_buf_upload(DC_GlVoxelBuf *buf,
                             const struct DC_VoxelGrid *grid);

/* Get active voxel count (from last upload). */
int dc_gl_voxel_buf_instance_count(const DC_GlVoxelBuf *buf);

/* Draw via SDF raycast.
 * view_proj_inv: inverse of view*projection matrix (to reconstruct rays)
 * eye:           camera position in world space (3 floats)
 * light_dir:     normalized light direction (3 floats)
 * screen_w/h:    viewport pixel dimensions */
void dc_gl_voxel_buf_draw(const DC_GlVoxelBuf *buf,
                            const float *view_proj_inv,
                            const float *eye,
                            const float *light_dir,
                            int screen_w, int screen_h);

/* World-space bounding box of the volume. */
void dc_gl_voxel_buf_bounds(const DC_GlVoxelBuf *buf,
                              float *min_out, float *max_out);

#endif /* DC_GL_VOXEL_H */
