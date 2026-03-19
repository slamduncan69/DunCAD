/*
 * gl_bezier_wire.c — Wireframe rendering for bezier patch meshes.
 *
 * Tessellates ts_bezier_mesh into GL_LINES vertex data:
 *   - Patch boundary curves (white)
 *   - Internal iso-curves (light gray)
 *   - CP lattice (dim gray)
 *   - Selected loop highlight (cyan)
 *
 * Vertex format: [x, y, z, r, g, b] x 6 floats per vertex.
 */

#include "gl/gl_bezier_wire.h"
#include <stdlib.h>
#include <string.h>

/* Include Trinity Site bezier headers — header-only, no link dep */
#include "../../talmud-main/talmud/sacred/trinity_site/ts_vec.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_surface.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_mesh.h"

/* --- Dynamic float buffer --- */
typedef struct {
    float *data;
    int    count;  /* floats used */
    int    cap;    /* floats allocated */
} FloatBuf;

static void fb_init(FloatBuf *fb, int initial_cap) {
    fb->data = (float *)malloc((size_t)initial_cap * sizeof(float));
    fb->count = 0;
    fb->cap = initial_cap;
}

static void fb_ensure(FloatBuf *fb, int need) {
    if (fb->count + need > fb->cap) {
        while (fb->cap < fb->count + need) fb->cap *= 2;
        fb->data = (float *)realloc(fb->data, (size_t)fb->cap * sizeof(float));
    }
}

static void fb_push6(FloatBuf *fb, float x, float y, float z,
                      float r, float g, float b) {
    fb_ensure(fb, 6);
    fb->data[fb->count++] = x;
    fb->data[fb->count++] = y;
    fb->data[fb->count++] = z;
    fb->data[fb->count++] = r;
    fb->data[fb->count++] = g;
    fb->data[fb->count++] = b;
}

static void fb_free(FloatBuf *fb) {
    free(fb->data);
    fb->data = NULL;
    fb->count = fb->cap = 0;
}

/* --- Tessellate a quadratic bezier curve in 3D --- */
/* Evaluates a row or column of patches at fixed u or v, emitting GL_LINES pairs. */

/* Emit a tessellated curve along u at fixed v, across all patches in a row. */
static void tess_u_curve(FloatBuf *fb, const ts_bezier_mesh *m,
                          int patch_row, double v, int tess_steps,
                          float r, float g, float b) {
    for (int pc = 0; pc < m->cols; pc++) {
        ts_bezier_patch p = ts_bezier_mesh_get_patch(m, patch_row, pc);
        for (int s = 0; s < tess_steps; s++) {
            double u0 = (double)s / (double)tess_steps;
            double u1 = (double)(s + 1) / (double)tess_steps;
            ts_vec3 a = ts_bezier_patch_eval(&p, u0, v);
            ts_vec3 b_pt = ts_bezier_patch_eval(&p, u1, v);
            fb_push6(fb, (float)a.v[0], (float)a.v[1], (float)a.v[2], r, g, b);
            fb_push6(fb, (float)b_pt.v[0], (float)b_pt.v[1], (float)b_pt.v[2], r, g, b);
        }
    }
}

/* Emit a tessellated curve along v at fixed u, across all patches in a column. */
static void tess_v_curve(FloatBuf *fb, const ts_bezier_mesh *m,
                          int patch_col, double u, int tess_steps,
                          float r, float g, float b) {
    for (int pr = 0; pr < m->rows; pr++) {
        ts_bezier_patch p = ts_bezier_mesh_get_patch(m, pr, patch_col);
        for (int s = 0; s < tess_steps; s++) {
            double v0 = (double)s / (double)tess_steps;
            double v1 = (double)(s + 1) / (double)tess_steps;
            ts_vec3 a = ts_bezier_patch_eval(&p, u, v0);
            ts_vec3 b_pt = ts_bezier_patch_eval(&p, u, v1);
            fb_push6(fb, (float)a.v[0], (float)a.v[1], (float)a.v[2], r, g, b);
            fb_push6(fb, (float)b_pt.v[0], (float)b_pt.v[1], (float)b_pt.v[2], r, g, b);
        }
    }
}

void
dc_gl_bezier_wire_init(DC_GlBezierWire *wire) {
    memset(wire, 0, sizeof(*wire));
}

void
dc_gl_bezier_wire_build(DC_GlBezierWire *wire,
                          const void *mesh_ptr,
                          int iso_density, int tess_steps)
{
    const ts_bezier_mesh *m = (const ts_bezier_mesh *)mesh_ptr;
    if (!m || !m->cps || m->rows <= 0 || m->cols <= 0) return;

    if (iso_density < 1) iso_density = 4;
    if (tess_steps < 2) tess_steps = 16;

    /* Destroy old */
    dc_gl_bezier_wire_destroy(wire);

    /* Estimate capacity: generous overallocation is fine */
    int est_lines = (m->rows + 1 + m->rows * iso_density) * m->cols * tess_steps * 2
                  + (m->cols + 1 + m->cols * iso_density) * m->rows * tess_steps * 2
                  + m->cp_rows * (m->cp_cols - 1) * 2
                  + m->cp_cols * (m->cp_rows - 1) * 2;
    FloatBuf fb;
    fb_init(&fb, est_lines * 6);

    /* --- Patch boundary curves (white) --- */
    float bnd_r = 1.0f, bnd_g = 1.0f, bnd_b = 1.0f;

    /* U-direction boundaries: at v=0 for each patch row, plus v=1 for last row */
    for (int pr = 0; pr < m->rows; pr++) {
        tess_u_curve(&fb, m, pr, 0.0, tess_steps, bnd_r, bnd_g, bnd_b);
    }
    tess_u_curve(&fb, m, m->rows - 1, 1.0, tess_steps, bnd_r, bnd_g, bnd_b);

    /* V-direction boundaries: at u=0 for each patch col, plus u=1 for last col */
    for (int pc = 0; pc < m->cols; pc++) {
        tess_v_curve(&fb, m, pc, 0.0, tess_steps, bnd_r, bnd_g, bnd_b);
    }
    tess_v_curve(&fb, m, m->cols - 1, 1.0, tess_steps, bnd_r, bnd_g, bnd_b);

    /* --- Internal iso-curves (light gray) --- */
    float iso_r = 0.6f, iso_g = 0.6f, iso_b = 0.6f;

    /* U-direction iso-curves within each patch row */
    for (int pr = 0; pr < m->rows; pr++) {
        for (int i = 1; i < iso_density; i++) {
            double v = (double)i / (double)iso_density;
            tess_u_curve(&fb, m, pr, v, tess_steps, iso_r, iso_g, iso_b);
        }
    }

    /* V-direction iso-curves within each patch col */
    for (int pc = 0; pc < m->cols; pc++) {
        for (int i = 1; i < iso_density; i++) {
            double u = (double)i / (double)iso_density;
            tess_v_curve(&fb, m, pc, u, tess_steps, iso_r, iso_g, iso_b);
        }
    }

    /* --- CP lattice (dim gray) --- */
    float cp_r = 0.35f, cp_g = 0.35f, cp_b = 0.35f;

    /* Horizontal CP connections */
    for (int cr = 0; cr < m->cp_rows; cr++) {
        for (int cc = 0; cc < m->cp_cols - 1; cc++) {
            ts_vec3 a = ts_bezier_mesh_get_cp(m, cr, cc);
            ts_vec3 b_pt = ts_bezier_mesh_get_cp(m, cr, cc + 1);
            fb_push6(&fb, (float)a.v[0], (float)a.v[1], (float)a.v[2],
                     cp_r, cp_g, cp_b);
            fb_push6(&fb, (float)b_pt.v[0], (float)b_pt.v[1], (float)b_pt.v[2],
                     cp_r, cp_g, cp_b);
        }
    }

    /* Vertical CP connections */
    for (int cc = 0; cc < m->cp_cols; cc++) {
        for (int cr = 0; cr < m->cp_rows - 1; cr++) {
            ts_vec3 a = ts_bezier_mesh_get_cp(m, cr, cc);
            ts_vec3 b_pt = ts_bezier_mesh_get_cp(m, cr + 1, cc);
            fb_push6(&fb, (float)a.v[0], (float)a.v[1], (float)a.v[2],
                     cp_r, cp_g, cp_b);
            fb_push6(&fb, (float)b_pt.v[0], (float)b_pt.v[1], (float)b_pt.v[2],
                     cp_r, cp_g, cp_b);
        }
    }

    /* Upload to GPU */
    wire->vert_count = fb.count / 6;
    if (wire->vert_count > 0) {
        glGenVertexArrays(1, &wire->vao);
        glGenBuffers(1, &wire->vbo);
        glBindVertexArray(wire->vao);
        glBindBuffer(GL_ARRAY_BUFFER, wire->vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)((size_t)fb.count * sizeof(float)),
                     fb.data, GL_STATIC_DRAW);
        /* aPos at location 0 */
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              6 * (GLsizei)sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        /* aColor at location 1 */
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              6 * (GLsizei)sizeof(float),
                              (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    wire->built = 1;
    fb_free(&fb);
}

void
dc_gl_bezier_wire_build_loop(GLuint *vao, GLuint *vbo, int *vert_count,
                               const void *mesh_ptr,
                               int loop_type, int loop_index,
                               int tess_steps)
{
    const ts_bezier_mesh *m = (const ts_bezier_mesh *)mesh_ptr;
    if (!m || !m->cps) return;
    if (tess_steps < 2) tess_steps = 16;

    /* Clean up old */
    dc_gl_bezier_wire_destroy_loop(vao, vbo, vert_count);

    float cyan_r = 0.0f, cyan_g = 1.0f, cyan_b = 1.0f;
    FloatBuf fb;
    fb_init(&fb, 1024);

    if (loop_type == 0) {
        /* Row loop: CP grid row at index 2*loop_index.
         * Tessellate the quadratic curves across all patches. */
        int cp_row = 2 * loop_index;
        if (cp_row < 0 || cp_row >= m->cp_rows) { fb_free(&fb); return; }

        /* Which patch row does this boundary belong to? */
        int pr = loop_index;
        if (pr >= m->rows) pr = m->rows - 1;
        double v = (loop_index < m->rows) ? 0.0 : 1.0;
        if (loop_index > 0 && loop_index < m->rows) {
            /* Interior boundary — belongs to patch above at v=1, or below at v=0.
             * Use patch below (index = loop_index) at v=0 */
            pr = loop_index;
            v = 0.0;
        } else if (loop_index == 0) {
            pr = 0;
            v = 0.0;
        } else {
            pr = m->rows - 1;
            v = 1.0;
        }
        tess_u_curve(&fb, m, pr, v, tess_steps, cyan_r, cyan_g, cyan_b);

    } else {
        /* Col loop: CP grid column at index 2*loop_index. */
        int cp_col = 2 * loop_index;
        if (cp_col < 0 || cp_col >= m->cp_cols) { fb_free(&fb); return; }

        int pc = loop_index;
        if (pc >= m->cols) pc = m->cols - 1;
        double u = (loop_index < m->cols) ? 0.0 : 1.0;
        if (loop_index > 0 && loop_index < m->cols) {
            pc = loop_index;
            u = 0.0;
        } else if (loop_index == 0) {
            pc = 0;
            u = 0.0;
        } else {
            pc = m->cols - 1;
            u = 1.0;
        }
        tess_v_curve(&fb, m, pc, u, tess_steps, cyan_r, cyan_g, cyan_b);
    }

    *vert_count = fb.count / 6;
    if (*vert_count > 0) {
        glGenVertexArrays(1, vao);
        glGenBuffers(1, vbo);
        glBindVertexArray(*vao);
        glBindBuffer(GL_ARRAY_BUFFER, *vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)((size_t)fb.count * sizeof(float)),
                     fb.data, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              6 * (GLsizei)sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              6 * (GLsizei)sizeof(float),
                              (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    fb_free(&fb);
}

void
dc_gl_bezier_wire_destroy(DC_GlBezierWire *wire) {
    if (wire->vbo) { glDeleteBuffers(1, &wire->vbo); wire->vbo = 0; }
    if (wire->vao) { glDeleteVertexArrays(1, &wire->vao); wire->vao = 0; }
    wire->vert_count = 0;
    wire->built = 0;
}

void
dc_gl_bezier_wire_destroy_loop(GLuint *vao, GLuint *vbo, int *vert_count) {
    if (*vbo) { glDeleteBuffers(1, vbo); *vbo = 0; }
    if (*vao) { glDeleteVertexArrays(1, vao); *vao = 0; }
    *vert_count = 0;
}
