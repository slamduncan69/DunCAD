/*
 * gl_sdf_analytical.c — Analytical SDF raymarching renderer.
 *
 * The Infinite Surface: perfect edges, perfect curves, infinite resolution.
 * Evaluates SDF math directly in the fragment shader. No voxel grid.
 */

#include "gl/gl_sdf_analytical.h"
#include "core/log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Shader sources
 * ========================================================================= */

static const char *VERT_SRC =
    "#version 320 es\n"
    "precision highp float;\n"
    "layout(location=0) in vec2 aPos;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "    vUV = aPos * 0.5 + 0.5;\n"
    "}\n";

/* Fragment shader: analytical SDF evaluation.
 * Primitives are passed as uniform arrays. The shader evaluates the
 * exact SDF for each primitive and composes them with CSG operations. */
static const char *FRAG_SRC =
    "#version 320 es\n"
    "precision highp float;\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uEye;\n"
    "uniform vec3 uLightDir;\n"
    "uniform mat4 uInvVP;\n"
    "uniform vec3 uBBoxMin;\n"
    "uniform vec3 uBBoxMax;\n"
    "uniform int uPrimCount;\n"
    /* Per-primitive uniforms: type, csg, pos, size, extra, color */
    "uniform int uPrimType[64];\n"
    "uniform int uPrimCSG[64];\n"
    "uniform vec3 uPrimPos[64];\n"
    "uniform vec3 uPrimSize[64];\n"
    "uniform float uPrimExtra[64];\n"
    "uniform vec3 uPrimColor[64];\n"
    "\n"
    /* AABB intersection for ray clipping */
    "vec2 intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {\n"
    "    vec3 inv = 1.0 / rd;\n"
    "    vec3 t0 = (bmin - ro) * inv;\n"
    "    vec3 t1 = (bmax - ro) * inv;\n"
    "    vec3 mn = min(t0, t1);\n"
    "    vec3 mx = max(t0, t1);\n"
    "    return vec2(max(max(mn.x,mn.y),mn.z), min(min(mx.x,mx.y),mx.z));\n"
    "}\n"
    "\n"
    /* Analytical SDF functions — exact math, no approximation */
    "float sdSphere(vec3 p, vec3 c, float r) {\n"
    "    return length(p - c) - r;\n"
    "}\n"
    "float sdBox(vec3 p, vec3 c, vec3 h) {\n"
    "    vec3 d = abs(p - c) - h;\n"
    "    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);\n"
    "}\n"
    "float sdCylinder(vec3 p, vec3 c, float r, float h) {\n"
    "    vec2 d = vec2(length(p.xz - c.xz) - r, abs(p.y - c.y) - h);\n"
    "    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);\n"
    "}\n"
    "float sdTorus(vec3 p, vec3 c, float R, float r) {\n"
    "    vec3 q = p - c;\n"
    "    vec2 d = vec2(length(q.xz) - R, q.y);\n"
    "    return length(d) - r;\n"
    "}\n"
    "\n"
    /* Evaluate the full SDF scene */
    "float evalSDF(vec3 p, out vec3 col) {\n"
    "    float d = 1e10;\n"
    "    col = vec3(0.7);\n"
    "    for (int i = 0; i < uPrimCount; i++) {\n"
    "        float pd;\n"
    "        if (uPrimType[i] == 1) pd = sdSphere(p, uPrimPos[i], uPrimSize[i].x);\n"
    "        else if (uPrimType[i] == 2) pd = sdBox(p, uPrimPos[i], uPrimSize[i]);\n"
    "        else if (uPrimType[i] == 3) pd = sdCylinder(p, uPrimPos[i], uPrimSize[i].x, uPrimExtra[i]);\n"
    "        else if (uPrimType[i] == 4) pd = sdTorus(p, uPrimPos[i], uPrimSize[i].x, uPrimExtra[i]);\n"
    "        else continue;\n"
    "        if (uPrimCSG[i] == 1) {\n"
    "            d = max(d, -pd);\n"  /* subtract: carve away */
    "        } else if (uPrimCSG[i] == 2) {\n"
    "            d = max(d, pd);\n"  /* intersect */
    "        } else {\n"
    "            if (pd < d) { d = pd; col = uPrimColor[i]; }\n"  /* union */
    "        }\n"
    "    }\n"
    "    return d;\n"
    "}\n"
    "\n"
    /* Normal via central differences on the analytical SDF */
    "vec3 calcNormal(vec3 p) {\n"
    "    vec3 col;\n"
    "    float e = 0.001;\n"
    "    return normalize(vec3(\n"
    "        evalSDF(p+vec3(e,0,0),col) - evalSDF(p-vec3(e,0,0),col),\n"
    "        evalSDF(p+vec3(0,e,0),col) - evalSDF(p-vec3(0,e,0),col),\n"
    "        evalSDF(p+vec3(0,0,e),col) - evalSDF(p-vec3(0,0,e),col)\n"
    "    ));\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 nn = uInvVP * vec4(vUV*2.0-1.0,-1,1);\n"
    "    vec4 ff = uInvVP * vec4(vUV*2.0-1.0, 1,1);\n"
    "    nn /= nn.w; ff /= ff.w;\n"
    "    vec3 ro = uEye, rd = normalize(ff.xyz - nn.xyz);\n"
    "    vec2 th = intersectAABB(ro, rd, uBBoxMin, uBBoxMax);\n"
    "    if (th.x > th.y) discard;\n"
    "    float t = max(th.x, 0.0), tF = th.y;\n"
    "\n"
    "    vec3 hp, col;\n"
    "    bool hit = false;\n"
    "    for (int i = 0; i < 256; i++) {\n"
    "        if (t > tF) break;\n"
    "        vec3 p = ro + rd*t;\n"
    "        float d = evalSDF(p, col);\n"
    "        if (abs(d) < 0.0005) {\n"
    "            hp = p; hit = true; break;\n"
    "        }\n"
    "        t += max(abs(d), 0.001);\n"
    "    }\n"
    "    if (!hit) discard;\n"
    "\n"
    "    vec3 N = calcNormal(hp);\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    float diff = max(dot(N,L),0.0);\n"
    "    float diff2 = max(dot(N,-L),0.0)*0.2;\n"
    "    float ambient = 0.15;\n"
    "    vec3 V = normalize(uEye-hp);\n"
    "    vec3 H = normalize(L+V);\n"
    "    float spec = pow(max(dot(N,H),0.0),64.0)*0.1;\n"
    "    if (col.r < 0.01 && col.g < 0.01 && col.b < 0.01) col = vec3(0.7);\n"
    "    FragColor = vec4(col*(ambient+diff+diff2)+vec3(spec), 1.0);\n"
    "}\n";

static const float QUAD_VERTS[] = {
    -1, -1,   1, -1,   1, 1,
    -1, -1,   1,  1,  -1, 1,
};

/* =========================================================================
 * Shader compilation
 * ========================================================================= */
static GLuint
compile_sh(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, NULL, log);
        fprintf(stderr, "gl_sdf_analytical shader error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint
link_prog(GLuint vs, GLuint fs)
{
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, 512, NULL, log);
        fprintf(stderr, "gl_sdf_analytical link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* =========================================================================
 * Scene building
 * ========================================================================= */

void
dc_gl_sdf_scene_clear(DC_GlSdfScene *s)
{
    if (!s) return;
    s->count = 0;
    s->bbox_min[0] = s->bbox_min[1] = s->bbox_min[2] = 1e18f;
    s->bbox_max[0] = s->bbox_max[1] = s->bbox_max[2] = -1e18f;
}

static DC_SdfPrim *
add_prim(DC_GlSdfScene *s)
{
    if (!s || s->count >= DC_SDF_MAX_PRIMS) return NULL;
    DC_SdfPrim *p = &s->prims[s->count++];
    memset(p, 0, sizeof(*p));
    p->color[0] = p->color[1] = p->color[2] = 0.7f;
    return p;
}

void
dc_gl_sdf_scene_add_sphere(DC_GlSdfScene *s,
                              float cx, float cy, float cz,
                              float radius,
                              float r, float g, float b)
{
    DC_SdfPrim *p = add_prim(s);
    if (!p) return;
    p->type = DC_SDF_SPHERE;
    p->pos[0] = cx; p->pos[1] = cy; p->pos[2] = cz;
    p->size[0] = radius;
    p->color[0] = r; p->color[1] = g; p->color[2] = b;
}

void
dc_gl_sdf_scene_add_box(DC_GlSdfScene *s,
                           float cx, float cy, float cz,
                           float hx, float hy, float hz,
                           float r, float g, float b)
{
    DC_SdfPrim *p = add_prim(s);
    if (!p) return;
    p->type = DC_SDF_BOX;
    p->pos[0] = cx; p->pos[1] = cy; p->pos[2] = cz;
    p->size[0] = hx; p->size[1] = hy; p->size[2] = hz;
    p->color[0] = r; p->color[1] = g; p->color[2] = b;
}

void
dc_gl_sdf_scene_add_cylinder(DC_GlSdfScene *s,
                                float cx, float cy, float cz,
                                float radius, float height,
                                float r, float g, float b)
{
    DC_SdfPrim *p = add_prim(s);
    if (!p) return;
    p->type = DC_SDF_CYLINDER;
    p->pos[0] = cx; p->pos[1] = cy; p->pos[2] = cz;
    p->size[0] = radius;
    p->extra = height * 0.5f;
    p->color[0] = r; p->color[1] = g; p->color[2] = b;
}

void
dc_gl_sdf_scene_add_torus(DC_GlSdfScene *s,
                             float cx, float cy, float cz,
                             float major_r, float minor_r,
                             float r, float g, float b)
{
    DC_SdfPrim *p = add_prim(s);
    if (!p) return;
    p->type = DC_SDF_TORUS;
    p->pos[0] = cx; p->pos[1] = cy; p->pos[2] = cz;
    p->size[0] = major_r;
    p->extra = minor_r;
    p->color[0] = r; p->color[1] = g; p->color[2] = b;
}

void
dc_gl_sdf_scene_set_csg(DC_GlSdfScene *s, int csg_op)
{
    if (!s || s->count <= 0) return;
    s->prims[s->count - 1].csg_op = csg_op;
}

void
dc_gl_sdf_scene_compute_bbox(DC_GlSdfScene *s)
{
    if (!s) return;
    s->bbox_min[0] = s->bbox_min[1] = s->bbox_min[2] = 1e18f;
    s->bbox_max[0] = s->bbox_max[1] = s->bbox_max[2] = -1e18f;

    for (int i = 0; i < s->count; i++) {
        DC_SdfPrim *p = &s->prims[i];
        if (p->csg_op == DC_SDF_SUBTRACT) continue; /* subtracted prims don't expand bbox */
        float r;
        switch (p->type) {
        case DC_SDF_SPHERE:
            r = p->size[0];
            for (int a = 0; a < 3; a++) {
                if (p->pos[a] - r < s->bbox_min[a]) s->bbox_min[a] = p->pos[a] - r;
                if (p->pos[a] + r > s->bbox_max[a]) s->bbox_max[a] = p->pos[a] + r;
            }
            break;
        case DC_SDF_BOX:
            for (int a = 0; a < 3; a++) {
                if (p->pos[a] - p->size[a] < s->bbox_min[a]) s->bbox_min[a] = p->pos[a] - p->size[a];
                if (p->pos[a] + p->size[a] > s->bbox_max[a]) s->bbox_max[a] = p->pos[a] + p->size[a];
            }
            break;
        case DC_SDF_CYLINDER:
            r = p->size[0];
            if (p->pos[0] - r < s->bbox_min[0]) s->bbox_min[0] = p->pos[0] - r;
            if (p->pos[0] + r > s->bbox_max[0]) s->bbox_max[0] = p->pos[0] + r;
            if (p->pos[1] - p->extra < s->bbox_min[1]) s->bbox_min[1] = p->pos[1] - p->extra;
            if (p->pos[1] + p->extra > s->bbox_max[1]) s->bbox_max[1] = p->pos[1] + p->extra;
            if (p->pos[2] - r < s->bbox_min[2]) s->bbox_min[2] = p->pos[2] - r;
            if (p->pos[2] + r > s->bbox_max[2]) s->bbox_max[2] = p->pos[2] + r;
            break;
        case DC_SDF_TORUS:
            r = p->size[0] + p->extra;
            for (int a = 0; a < 3; a++) {
                float er = (a == 1) ? p->extra : r;
                if (p->pos[a] - er < s->bbox_min[a]) s->bbox_min[a] = p->pos[a] - er;
                if (p->pos[a] + er > s->bbox_max[a]) s->bbox_max[a] = p->pos[a] + er;
            }
            break;
        }
    }

    /* Add padding */
    float pad = 1.0f;
    for (int a = 0; a < 3; a++) {
        s->bbox_min[a] -= pad;
        s->bbox_max[a] += pad;
    }
}

/* =========================================================================
 * Drawing
 * ========================================================================= */

void
dc_gl_sdf_draw(DC_GlSdfScene *s,
                 const float *view_proj_inv,
                 const float *eye,
                 const float *light_dir)
{
    if (!s || s->count <= 0) return;

    /* Lazy GL init */
    if (!s->gl_ready) {
        GLuint vs = compile_sh(GL_VERTEX_SHADER, VERT_SRC);
        GLuint fs = compile_sh(GL_FRAGMENT_SHADER, FRAG_SRC);
        s->prog = link_prog(vs, fs);
        if (!s->prog) return;

        glGenVertexArrays(1, &s->quad_vao);
        glGenBuffers(1, &s->quad_vbo);
        glBindVertexArray(s->quad_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s->quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTS), QUAD_VERTS, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glBindVertexArray(0);

        s->gl_ready = 1;
        dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
               "gl_sdf_analytical: shader compiled, %d primitives", s->count);
    }

    glDisable(GL_DEPTH_TEST);

    glUseProgram(s->prog);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uInvVP"), 1, GL_FALSE, view_proj_inv);
    glUniform3fv(glGetUniformLocation(s->prog, "uEye"), 1, eye);
    glUniform3fv(glGetUniformLocation(s->prog, "uLightDir"), 1, light_dir);
    glUniform3fv(glGetUniformLocation(s->prog, "uBBoxMin"), 1, s->bbox_min);
    glUniform3fv(glGetUniformLocation(s->prog, "uBBoxMax"), 1, s->bbox_max);
    glUniform1i(glGetUniformLocation(s->prog, "uPrimCount"), s->count);

    /* Upload primitive arrays */
    int types[DC_SDF_MAX_PRIMS] = {0};
    int csgs[DC_SDF_MAX_PRIMS] = {0};
    float pos[DC_SDF_MAX_PRIMS * 3];
    float sizes[DC_SDF_MAX_PRIMS * 3];
    float extras[DC_SDF_MAX_PRIMS];
    float colors[DC_SDF_MAX_PRIMS * 3];

    for (int i = 0; i < s->count; i++) {
        types[i] = s->prims[i].type;
        csgs[i] = s->prims[i].csg_op;
        pos[i*3+0] = s->prims[i].pos[0];
        pos[i*3+1] = s->prims[i].pos[1];
        pos[i*3+2] = s->prims[i].pos[2];
        sizes[i*3+0] = s->prims[i].size[0];
        sizes[i*3+1] = s->prims[i].size[1];
        sizes[i*3+2] = s->prims[i].size[2];
        extras[i] = s->prims[i].extra;
        colors[i*3+0] = s->prims[i].color[0];
        colors[i*3+1] = s->prims[i].color[1];
        colors[i*3+2] = s->prims[i].color[2];
    }

    for (int di = 0; di < s->count; di++) {
        fprintf(stderr, "SDF prim[%d]: type=%d pos=(%.2f,%.2f,%.2f) size=(%.2f,%.2f,%.2f) color=(%.2f,%.2f,%.2f)\n",
                di, types[di], pos[di*3], pos[di*3+1], pos[di*3+2],
                sizes[di*3], sizes[di*3+1], sizes[di*3+2],
                colors[di*3], colors[di*3+1], colors[di*3+2]);
    }

    glUniform1iv(glGetUniformLocation(s->prog, "uPrimType"), s->count, types);
    glUniform1iv(glGetUniformLocation(s->prog, "uPrimCSG"), s->count, csgs);
    glUniform3fv(glGetUniformLocation(s->prog, "uPrimPos"), s->count, pos);
    glUniform3fv(glGetUniformLocation(s->prog, "uPrimSize"), s->count, sizes);
    glUniform1fv(glGetUniformLocation(s->prog, "uPrimExtra"), s->count, extras);
    glUniform3fv(glGetUniformLocation(s->prog, "uPrimColor"), s->count, colors);

    glBindVertexArray(s->quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);

    glEnable(GL_DEPTH_TEST);
}

void
dc_gl_sdf_scene_destroy(DC_GlSdfScene *s)
{
    if (!s) return;
    if (s->prog) glDeleteProgram(s->prog);
    if (s->quad_vao) glDeleteVertexArrays(1, &s->quad_vao);
    if (s->quad_vbo) glDeleteBuffers(1, &s->quad_vbo);
    s->prog = 0;
    s->quad_vao = 0;
    s->quad_vbo = 0;
    s->gl_ready = 0;
}
