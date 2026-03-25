/*
 * gl_bezier_ray.h — Direct bezier surface raytracer.
 *
 * Renders bezier patch meshes by solving ray-surface intersection
 * in the fragment shader. No voxels, no triangles, no approximation.
 * The surface IS the surface — infinite resolution, mathematically exact.
 */

#ifndef DC_GL_BEZIER_RAY_H
#define DC_GL_BEZIER_RAY_H

typedef struct DC_GlBezierRay DC_GlBezierRay;

DC_GlBezierRay *dc_gl_bezier_ray_new(void);
void             dc_gl_bezier_ray_free(DC_GlBezierRay *r);

/* Upload bezier mesh control points. mesh_ptr is a ts_bezier_mesh*. */
int  dc_gl_bezier_ray_upload(DC_GlBezierRay *r, const void *mesh_ptr);

/* Draw the surface. Same interface as gl_voxel_buf_draw. */
void dc_gl_bezier_ray_draw(DC_GlBezierRay *r,
                            const float *view_proj_inv,
                            const float *eye,
                            const float *light_dir);

void dc_gl_bezier_ray_bounds(const DC_GlBezierRay *r,
                              float *min_out, float *max_out);

int  dc_gl_bezier_ray_patch_count(const DC_GlBezierRay *r);

#endif /* DC_GL_BEZIER_RAY_H */
