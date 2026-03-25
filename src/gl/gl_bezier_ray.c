/*
 * gl_bezier_ray.c — Direct bezier surface raytracer.
 *
 * One fullscreen quad. One shader. Ray-surface intersection via Newton
 * iteration in the fragment shader. The surface is mathematically exact
 * at every pixel — no voxels, no triangles, no approximation.
 *
 * For each pixel:
 *   1. Cast ray from camera
 *   2. For each bezier patch, solve S(u,v) = O + tD (Newton's method)
 *   3. Normal = cross(dS/du, dS/dv) — exact from patch derivatives
 *   4. Shade with Phong lighting
 */

#include "gl/gl_bezier_ray.h"
#include "core/log.h"

#include <epoxy/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Trinity Site bezier mesh header */
#include "../../talmud-main/talmud/sacred/trinity_site/ts_vec.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_surface.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_mesh.h"

#define MAX_PATCHES 24

/* =========================================================================
 * Shaders
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

/* Fragment shader part 1: declarations, utility functions, bezier evaluation */
static const char *FRAG_SRC1 =
    "#version 320 es\n"
    "precision highp float;\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uBBoxMin;\n"
    "uniform vec3 uBBoxMax;\n"
    "uniform vec3 uEye;\n"
    "uniform vec3 uLightDir;\n"
    "uniform mat4 uInvVP;\n"
    "uniform int uNumPatches;\n"
    "uniform vec3 uCP[216];\n"  /* MAX_PATCHES * 9 */
    "\n"
    "vec2 intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {\n"
    "    vec3 inv = 1.0 / rd;\n"
    "    vec3 t0 = (bmin - ro) * inv;\n"
    "    vec3 t1 = (bmax - ro) * inv;\n"
    "    vec3 mn = min(t0, t1);\n"
    "    vec3 mx = max(t0, t1);\n"
    "    return vec2(max(max(mn.x,mn.y),mn.z), min(min(mx.x,mx.y),mx.z));\n"
    "}\n"
    "\n"
    /* Biquadratic Bezier patch evaluation */
    "vec3 evalP(int b, float u, float v) {\n"
    "    float u0=(1.0-u)*(1.0-u), u1=2.0*u*(1.0-u), u2=u*u;\n"
    "    float v0=(1.0-v)*(1.0-v), v1=2.0*v*(1.0-v), v2=v*v;\n"
    "    return v0*(u0*uCP[b]+u1*uCP[b+1]+u2*uCP[b+2])\n"
    "          +v1*(u0*uCP[b+3]+u1*uCP[b+4]+u2*uCP[b+5])\n"
    "          +v2*(u0*uCP[b+6]+u1*uCP[b+7]+u2*uCP[b+8]);\n"
    "}\n"
    "vec3 evalDu(int b, float u, float v) {\n"
    "    float d0=-2.0*(1.0-u), d1=2.0-4.0*u, d2=2.0*u;\n"
    "    float v0=(1.0-v)*(1.0-v), v1=2.0*v*(1.0-v), v2=v*v;\n"
    "    return v0*(d0*uCP[b]+d1*uCP[b+1]+d2*uCP[b+2])\n"
    "          +v1*(d0*uCP[b+3]+d1*uCP[b+4]+d2*uCP[b+5])\n"
    "          +v2*(d0*uCP[b+6]+d1*uCP[b+7]+d2*uCP[b+8]);\n"
    "}\n"
    "vec3 evalDv(int b, float u, float v) {\n"
    "    float u0=(1.0-u)*(1.0-u), u1=2.0*u*(1.0-u), u2=u*u;\n"
    "    float d0=-2.0*(1.0-v), d1=2.0-4.0*v, d2=2.0*v;\n"
    "    return d0*(u0*uCP[b]+u1*uCP[b+1]+u2*uCP[b+2])\n"
    "          +d1*(u0*uCP[b+3]+u1*uCP[b+4]+u2*uCP[b+5])\n"
    "          +d2*(u0*uCP[b+6]+u1*uCP[b+7]+u2*uCP[b+8]);\n"
    "}\n";

/* Fragment shader part 2: Newton intersection + main */
static const char *FRAG_SRC2 =
    /* Ray-patch intersection via Newton's method.
     * Solves S(u,v) - O - tD = 0 for (u,v,t). */
    "bool hitPatch(int b, vec3 ro, vec3 rd, out float ot, out vec3 oN) {\n"
    "    float best = 1e30;\n"
    "    bool found = false;\n"
    "    vec3 bN;\n"
    /* Try 9 starting (u,v) points — dense grid avoids missing edges */
    "    for (int s = 0; s < 9; s++) {\n"
    "        float u = float(s/3) * 0.4 + 0.1;\n"  /* 0.1, 0.5, 0.9 */
    "        float v = float(s-s/3*3) * 0.4 + 0.1;\n"
    "        vec3 sp = evalP(b, u, v);\n"
    "        float t = dot(sp - ro, rd) / dot(rd, rd);\n"
    "        if (t < 0.0001) t = 0.0001;\n"
    "        for (int i = 0; i < 30; i++) {\n"
    "            vec3 S = evalP(b, u, v);\n"
    "            vec3 Su = evalDu(b, u, v);\n"
    "            vec3 Sv = evalDv(b, u, v);\n"
    "            vec3 F = S - ro - t * rd;\n"
    "            if (dot(F,F) < 1e-14) break;\n"
    "            mat3 J = mat3(Su, Sv, -rd);\n"
    "            float det = determinant(J);\n"
    "            if (abs(det) < 1e-18) break;\n"
    "            vec3 d = inverse(J) * (-F);\n"
    "            u += d.x; v += d.y; t += d.z;\n"
    "            u = clamp(u, 0.0, 1.0);\n"
    "            v = clamp(v, 0.0, 1.0);\n"
    "        }\n"
    "        vec3 F2 = evalP(b, u, v) - ro - t * rd;\n"
    "        if (dot(F2,F2) < 1e-4 && t > 0.0001 && t < best) {\n"
    "            best = t;\n"
    /* Per-patch normal from analytical derivatives */
    "            vec3 Su = evalDu(b, clamp(u,0.05,0.95), clamp(v,0.05,0.95));\n"
    "            vec3 Sv = evalDv(b, clamp(u,0.05,0.95), clamp(v,0.05,0.95));\n"
    "            bN = normalize(cross(Su, Sv));\n"
    "            found = true;\n"
    "        }\n"
    "    }\n"
    "    ot = best; oN = bN;\n"
    "    return found;\n"
    "}\n";

/* Fragment shader part 3: main function */
static const char *FRAG_SRC3 =
    "void main() {\n"
    "    vec4 nn = uInvVP * vec4(vUV*2.0-1.0,-1,1);\n"
    "    vec4 ff = uInvVP * vec4(vUV*2.0-1.0, 1,1);\n"
    "    nn /= nn.w; ff /= ff.w;\n"
    "    vec3 ro = uEye, rd = normalize(ff.xyz - nn.xyz);\n"
    "    vec2 th = intersectAABB(ro, rd, uBBoxMin, uBBoxMax);\n"
    "    if (th.x > th.y) discard;\n"
    "\n"
    "    float bestT = 1e30;\n"
    "    vec3 bestN;\n"
    "    bool anyHit = false;\n"
    "    for (int p = 0; p < 24; p++) {\n"
    "        if (p >= uNumPatches) break;\n"
    "        float t; vec3 N;\n"
    "        if (hitPatch(p*9, ro, rd, t, N) && t < bestT) {\n"
    "            bestT = t; bestN = N; anyHit = true;\n"
    "        }\n"
    "    }\n"
    "    if (!anyHit) discard;\n"
    "\n"
    "    vec3 hp = ro + rd * bestT;\n"
    /* Screen-space normals from dFdx/dFdy of the hit position.
     * Automatically continuous across patch boundaries. */
    "    vec3 N = normalize(cross(dFdx(hp), dFdy(hp)));\n"
    "    if (dot(N, rd) > 0.0) N = -N;\n"
    "    vec3 col = vec3(0.706);\n"  /* 180/255 grey */
    "    vec3 L = normalize(uLightDir);\n"
    "    float df = max(dot(N,L),0.0);\n"
    "    float d2 = max(dot(N,-L),0.0)*0.2;\n"
    "    vec3 V = normalize(uEye-hp), H = normalize(L+V);\n"
    "    float sp = pow(max(dot(N,H),0.0),64.0)*0.1;\n"
    "    FragColor = vec4(col*(0.15+df+d2)+vec3(sp), 1.0);\n"
    "}\n";

/* =========================================================================
 * Internal structure
 * ========================================================================= */
struct DC_GlBezierRay {
    GLuint  quad_vao, quad_vbo;
    GLuint  prog;
    float   bbox_min[3], bbox_max[3];
    float   cp[MAX_PATCHES * 9 * 3]; /* flattened control points */
    int     num_patches;
    int     uploaded;
};

/* =========================================================================
 * Shader compilation (same as gl_voxel.c)
 * ========================================================================= */
static GLuint compile_sh(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "bezier_ray shader compile: %s", buf);
    }
    return s;
}

static GLuint link_prog(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(p, sizeof(buf), NULL, buf);
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "bezier_ray link: %s", buf);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

DC_GlBezierRay *
dc_gl_bezier_ray_new(void)
{
    DC_GlBezierRay *r = calloc(1, sizeof(*r));
    return r;
}

void
dc_gl_bezier_ray_free(DC_GlBezierRay *r)
{
    if (!r) return;
    if (r->quad_vao) glDeleteVertexArrays(1, &r->quad_vao);
    if (r->quad_vbo) glDeleteBuffers(1, &r->quad_vbo);
    if (r->prog) glDeleteProgram(r->prog);
    free(r);
}

int
dc_gl_bezier_ray_upload(DC_GlBezierRay *r, const void *mesh_ptr)
{
    if (!r || !mesh_ptr) return -1;

    const ts_bezier_mesh *mesh = (const ts_bezier_mesh *)mesh_ptr;
    int num_patches = mesh->rows * mesh->cols;
    if (num_patches <= 0 || num_patches > MAX_PATCHES) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "bezier_ray: %d patches exceeds max %d", num_patches, MAX_PATCHES);
        return -1;
    }

    r->num_patches = num_patches;

    /* Extract per-patch CPs (with duplication at shared edges) */
    int idx = 0;
    for (int pr = 0; pr < mesh->rows; pr++) {
        for (int pc = 0; pc < mesh->cols; pc++) {
            ts_bezier_patch patch = ts_bezier_mesh_get_patch(mesh, pr, pc);
            for (int j = 0; j < 3; j++) {
                for (int i = 0; i < 3; i++) {
                    r->cp[idx++] = (float)patch.cp[j][i].v[0];
                    r->cp[idx++] = (float)patch.cp[j][i].v[1];
                    r->cp[idx++] = (float)patch.cp[j][i].v[2];
                }
            }
        }
    }

    /* Compute bounding box from all CPs */
    ts_vec3 bmin, bmax;
    ts_bezier_mesh_bbox(mesh, &bmin, &bmax);
    /* Expand slightly to avoid clipping at edges */
    float pad = 0.1f;
    r->bbox_min[0] = (float)bmin.v[0] - pad;
    r->bbox_min[1] = (float)bmin.v[1] - pad;
    r->bbox_min[2] = (float)bmin.v[2] - pad;
    r->bbox_max[0] = (float)bmax.v[0] + pad;
    r->bbox_max[1] = (float)bmax.v[1] + pad;
    r->bbox_max[2] = (float)bmax.v[2] + pad;

    /* Fullscreen quad */
    if (!r->quad_vao) {
        static const float quad[] = {
            -1,-1,  1,-1,  1,1,  -1,-1,  1,1,  -1,1
        };
        glGenVertexArrays(1, &r->quad_vao);
        glGenBuffers(1, &r->quad_vbo);
        glBindVertexArray(r->quad_vao);
        glBindBuffer(GL_ARRAY_BUFFER, r->quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glBindVertexArray(0);
    }

    /* Compile shader */
    if (!r->prog) {
        GLuint vs = compile_sh(GL_VERTEX_SHADER, VERT_SRC);
        /* Concatenate 3-part fragment shader */
        size_t l1 = strlen(FRAG_SRC1), l2 = strlen(FRAG_SRC2), l3 = strlen(FRAG_SRC3);
        char *frag = malloc(l1 + l2 + l3 + 1);
        memcpy(frag, FRAG_SRC1, l1);
        memcpy(frag + l1, FRAG_SRC2, l2);
        memcpy(frag + l1 + l2, FRAG_SRC3, l3 + 1);
        GLuint fs = compile_sh(GL_FRAGMENT_SHADER, frag);
        free(frag);
        r->prog = link_prog(vs, fs);
    }

    r->uploaded = 1;

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "bezier_ray: uploaded %d patches (%d CPs)",
           r->num_patches, r->num_patches * 9);

    return 0;
}

void
dc_gl_bezier_ray_draw(DC_GlBezierRay *r,
                       const float *view_proj_inv,
                       const float *eye,
                       const float *light_dir)
{
    if (!r || !r->uploaded || !r->prog || r->num_patches <= 0) return;

    glDisable(GL_DEPTH_TEST);
    glUseProgram(r->prog);

    glUniformMatrix4fv(glGetUniformLocation(r->prog, "uInvVP"), 1, GL_FALSE, view_proj_inv);
    glUniform3fv(glGetUniformLocation(r->prog, "uEye"), 1, eye);
    glUniform3fv(glGetUniformLocation(r->prog, "uLightDir"), 1, light_dir);
    glUniform3fv(glGetUniformLocation(r->prog, "uBBoxMin"), 1, r->bbox_min);
    glUniform3fv(glGetUniformLocation(r->prog, "uBBoxMax"), 1, r->bbox_max);
    glUniform1i(glGetUniformLocation(r->prog, "uNumPatches"), r->num_patches);
    glUniform3fv(glGetUniformLocation(r->prog, "uCP"), r->num_patches * 9,
                 r->cp);

    glBindVertexArray(r->quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}

void
dc_gl_bezier_ray_bounds(const DC_GlBezierRay *r, float *min_out, float *max_out)
{
    if (!r) return;
    if (min_out) { min_out[0] = r->bbox_min[0]; min_out[1] = r->bbox_min[1]; min_out[2] = r->bbox_min[2]; }
    if (max_out) { max_out[0] = r->bbox_max[0]; max_out[1] = r->bbox_max[1]; max_out[2] = r->bbox_max[2]; }
}

int
dc_gl_bezier_ray_patch_count(const DC_GlBezierRay *r)
{
    return r ? r->num_patches : 0;
}
