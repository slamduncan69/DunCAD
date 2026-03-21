#ifndef DC_GL_SDF_ANALYTICAL_H
#define DC_GL_SDF_ANALYTICAL_H

/*
 * gl_sdf_analytical.h — Analytical SDF raymarching renderer.
 *
 * Renders the Infinite Surface: evaluates SDF functions analytically
 * in the fragment shader. No voxel grid, no texture sampling, no
 * interpolation artifacts. Perfect edges on cubes, perfect curves
 * on spheres, at any resolution.
 *
 * The primitive tree is passed as shader uniforms. The shader walks
 * the tree per-pixel, evaluating exact SDF math.
 *
 * Usage:
 *   DC_GlSdfScene scene = {0};
 *   dc_gl_sdf_scene_add_sphere(&scene, cx,cy,cz, r);
 *   dc_gl_sdf_scene_add_box(&scene, ...);
 *   dc_gl_sdf_draw(&scene, inv_vp, eye, light_dir);
 */

#include <epoxy/gl.h>

/* Primitive types */
#define DC_SDF_SPHERE   1
#define DC_SDF_BOX      2
#define DC_SDF_CYLINDER 3
#define DC_SDF_TORUS    4

/* CSG operations */
#define DC_SDF_UNION    0
#define DC_SDF_SUBTRACT 1
#define DC_SDF_INTERSECT 2

/* A single SDF primitive */
typedef struct {
    int   type;         /* DC_SDF_SPHERE, BOX, CYLINDER, TORUS */
    int   csg_op;       /* DC_SDF_UNION, SUBTRACT, INTERSECT */
    float pos[3];       /* center position */
    float size[3];      /* half-extents (box), radius (sphere), etc. */
    float extra;        /* minor radius (torus), height (cylinder) */
    float color[3];     /* RGB 0-1 */
} DC_SdfPrim;

/* Scene: list of primitives */
#define DC_SDF_MAX_PRIMS 64

typedef struct {
    DC_SdfPrim prims[DC_SDF_MAX_PRIMS];
    int        count;
    float      bbox_min[3];
    float      bbox_max[3];

    /* GL resources (created on first draw) */
    GLuint     prog;
    GLuint     quad_vao;
    GLuint     quad_vbo;
    int        gl_ready;
} DC_GlSdfScene;

/* Add primitives to the scene */
void dc_gl_sdf_scene_clear(DC_GlSdfScene *s);
void dc_gl_sdf_scene_add_sphere(DC_GlSdfScene *s,
                                  float cx, float cy, float cz,
                                  float radius,
                                  float r, float g, float b);
void dc_gl_sdf_scene_add_box(DC_GlSdfScene *s,
                                float cx, float cy, float cz,
                                float hx, float hy, float hz,
                                float r, float g, float b);
void dc_gl_sdf_scene_add_cylinder(DC_GlSdfScene *s,
                                    float cx, float cy, float cz,
                                    float radius, float height,
                                    float r, float g, float b);
void dc_gl_sdf_scene_add_torus(DC_GlSdfScene *s,
                                 float cx, float cy, float cz,
                                 float major_r, float minor_r,
                                 float r, float g, float b);

/* Set CSG operation for the last added primitive */
void dc_gl_sdf_scene_set_csg(DC_GlSdfScene *s, int csg_op);

/* Compute bounding box from primitives */
void dc_gl_sdf_scene_compute_bbox(DC_GlSdfScene *s);

/* Draw the scene via analytical SDF raymarching.
 * Must be called from GL context. */
void dc_gl_sdf_draw(DC_GlSdfScene *s,
                      const float *view_proj_inv,
                      const float *eye,
                      const float *light_dir);

/* Free GL resources */
void dc_gl_sdf_scene_destroy(DC_GlSdfScene *s);

#endif /* DC_GL_SDF_ANALYTICAL_H */
