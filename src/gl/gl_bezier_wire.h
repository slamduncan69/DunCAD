#ifndef DC_GL_BEZIER_WIRE_H
#define DC_GL_BEZIER_WIRE_H

/*
 * gl_bezier_wire.h — Wireframe rendering for bezier patch meshes.
 *
 * Tessellates a ts_bezier_mesh into GL_LINES:
 *   - Patch boundary curves (u/v = 0, 1) — white
 *   - Internal iso-curves (configurable density) — light gray
 *   - CP lattice lines (connecting adjacent CPs) — dim gray
 *
 * Vertex format: [x, y, z, r, g, b] x 6 floats — matches line_prog.
 *
 * Also supports a separate highlight buffer for selected loops (cyan, 3px).
 */

#include <epoxy/gl.h>

/* Forward-declare — actual struct in ts_bezier_mesh.h */
struct ts_bezier_mesh_tag;

typedef struct {
    GLuint vao;
    GLuint vbo;
    int    vert_count;
    int    built;
} DC_GlBezierWire;

/* Initialize wire struct to zero state. */
void dc_gl_bezier_wire_init(DC_GlBezierWire *wire);

/* Build wireframe from mesh. iso_density = iso-curves per patch per axis (default 4).
 * tess_steps = tessellation segments per quadratic curve (default 16).
 * Must be called from GL context. */
void dc_gl_bezier_wire_build(DC_GlBezierWire *wire,
                              const void *mesh_ptr,
                              int iso_density, int tess_steps);

/* Build highlight VBO for a loop (row or column of CPs).
 * type: 0=row, 1=col. index: row/col index in CP grid.
 * tess_steps: tessellation per quadratic segment.
 * Returns vert_count in highlight_vert_count. */
void dc_gl_bezier_wire_build_loop(GLuint *vao, GLuint *vbo, int *vert_count,
                                   const void *mesh_ptr,
                                   int loop_type, int loop_index,
                                   int tess_steps);

/* Free GPU resources. */
void dc_gl_bezier_wire_destroy(DC_GlBezierWire *wire);

/* Free loop highlight resources. */
void dc_gl_bezier_wire_destroy_loop(GLuint *vao, GLuint *vbo, int *vert_count);

#endif /* DC_GL_BEZIER_WIRE_H */
