/*
 * gl_voxel.c — SDF raycast voxel renderer.
 *
 * No mesh geometry. No triangles.
 *
 * Two 3D textures:
 *   1. RGB8  — voxel color
 *   2. R32F  — signed distance in WORLD UNITS (no normalization)
 *
 * A fullscreen quad fires a ray per pixel. The fragment shader
 * marches through the SDF texture using fixed half-voxel steps.
 * When distance crosses zero — surface. Normal from gradient.
 */

#include "gl/gl_voxel.h"
#include "voxel/voxel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Shaders
 * ========================================================================= */

static const char *RAYMARCH_VERT_SRC =
    "#version 320 es\n"
    "precision highp float;\n"
    "layout(location=0) in vec2 aPos;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "    vUV = aPos * 0.5 + 0.5;\n"
    "}\n";

static const char *RAYMARCH_FRAG_SRC =
    "#version 320 es\n"
    "precision highp float;\n"
    "precision highp sampler3D;\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "\n"
    "uniform sampler3D uColor;\n"        /* RGB8 color texture */
    "uniform sampler3D uSDF;\n"          /* R32F signed distance */
    "uniform vec3 uBBoxMin;\n"
    "uniform vec3 uBBoxMax;\n"
    "uniform vec3 uEye;\n"
    "uniform vec3 uLightDir;\n"
    "uniform mat4 uInvVP;\n"
    "uniform float uCellSize;\n"
    "uniform int uBlocky;\n"
    "\n"
    "vec2 intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {\n"
    "    vec3 invRd = 1.0 / rd;\n"
    "    vec3 t0 = (bmin - ro) * invRd;\n"
    "    vec3 t1 = (bmax - ro) * invRd;\n"
    "    vec3 tmin = min(t0, t1);\n"
    "    vec3 tmax = max(t0, t1);\n"
    "    float tNear = max(max(tmin.x, tmin.y), tmin.z);\n"
    "    float tFar  = min(min(tmax.x, tmax.y), tmax.z);\n"
    "    return vec2(tNear, tFar);\n"
    "}\n"
    "\n"
    "vec3 worldToUV(vec3 p) {\n"
    "    return (p - uBBoxMin) / (uBBoxMax - uBBoxMin);\n"
    "}\n"
    "\n"
    "float sampleSDF(vec3 uvw) {\n"
    "    return texture(uSDF, uvw).r;\n"  /* actual world-space distance */
    "}\n"
    "\n"
    "vec3 calcNormal(vec3 uvw) {\n"
    "    vec3 texel = 1.0 / vec3(textureSize(uSDF, 0));\n"
    "    float dx = sampleSDF(uvw + vec3(texel.x,0,0)) - sampleSDF(uvw - vec3(texel.x,0,0));\n"
    "    float dy = sampleSDF(uvw + vec3(0,texel.y,0)) - sampleSDF(uvw - vec3(0,texel.y,0));\n"
    "    float dz = sampleSDF(uvw + vec3(0,0,texel.z)) - sampleSDF(uvw - vec3(0,0,texel.z));\n"
    "    return normalize(vec3(dx, dy, dz));\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 ndc_near = vec4(vUV * 2.0 - 1.0, -1.0, 1.0);\n"
    "    vec4 ndc_far  = vec4(vUV * 2.0 - 1.0,  1.0, 1.0);\n"
    "    vec4 world_near = uInvVP * ndc_near;\n"
    "    vec4 world_far  = uInvVP * ndc_far;\n"
    "    world_near /= world_near.w;\n"
    "    world_far  /= world_far.w;\n"
    "\n"
    "    vec3 ro = uEye;\n"
    "    vec3 rd = normalize(world_far.xyz - world_near.xyz);\n"
    "\n"
    "    vec2 tHit = intersectAABB(ro, rd, uBBoxMin, uBBoxMax);\n"
    "    if (tHit.x > tHit.y) discard;\n"
    "\n"
    "    float tNear = max(tHit.x, 0.0);\n"
    "    float tFar  = tHit.y;\n"
    "    float step = uCellSize * 0.5;\n"
    "\n"
    "    float t = tNear;\n"
    "    bool hit = false;\n"
    "    vec3 hitUVW;\n"
    "    vec3 hitPos;\n"
    "\n"
    "    for (int i = 0; i < 1024; i++) {\n"
    "        if (t > tFar) break;\n"
    "        vec3 p = ro + rd * t;\n"
    "        vec3 uvw = worldToUV(p);\n"
    "        if (any(lessThan(uvw, vec3(0))) || any(greaterThan(uvw, vec3(1)))) {\n"
    "            t += step;\n"
    "            continue;\n"
    "        }\n"
    "        float d = sampleSDF(uvw);\n"
    "        if (d <= 0.0) {\n"
    "            hit = true;\n"
    "            hitUVW = uvw;\n"
    "            hitPos = p;\n"
    "            break;\n"
    "        }\n"
    "        t += step;\n"
    "    }\n"
    "\n"
    "    if (!hit) discard;\n"
    "\n"
    "    vec3 N;\n"
    "    if (uBlocky == 1) {\n"
    "        /* Blocky normals: snap to nearest axis direction.\n"
    "         * Determine which voxel face we hit by checking which\n"
    "         * cell boundary the hit point is closest to. */\n"
    "        vec3 gridSize = vec3(textureSize(uSDF, 0));\n"
    "        vec3 cellPos = hitUVW * gridSize;\n"
    "        vec3 frac = fract(cellPos);\n"
    "        vec3 distToEdge = min(frac, 1.0 - frac);\n"
    "        if (distToEdge.x < distToEdge.y && distToEdge.x < distToEdge.z)\n"
    "            N = vec3(sign(frac.x - 0.5), 0.0, 0.0);\n"
    "        else if (distToEdge.y < distToEdge.z)\n"
    "            N = vec3(0.0, sign(frac.y - 0.5), 0.0);\n"
    "        else\n"
    "            N = vec3(0.0, 0.0, sign(frac.z - 0.5));\n"
    "    } else {\n"
    "        N = calcNormal(hitUVW);\n"
    "    }\n"
    "    vec3 color = texture(uColor, hitUVW).rgb;\n"
    "\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float diff2 = max(dot(N, -L), 0.0) * 0.3;\n"
    "    float ambient = 0.12;\n"
    "    vec3 V = normalize(uEye - hitPos);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.4;\n"
    "    vec3 result = color * (ambient + diff + diff2) + vec3(spec);\n"
    "\n"
    "    FragColor = vec4(result, 1.0);\n"
    "}\n";

/* =========================================================================
 * Internal structure
 * ========================================================================= */
struct DC_GlVoxelBuf {
    GLuint  color_tex;    /* 3D texture: RGB8 */
    GLuint  sdf_tex;      /* 3D texture: R32F — real distances */
    GLuint  quad_vao;
    GLuint  quad_vbo;
    GLuint  prog;
    int     uploaded;
    int     active_count;

    float   cell_size;
    float   bbox_min[3];
    float   bbox_max[3];
    int     blocky;       /* 0 = smooth (GL_LINEAR), 1 = blocky (GL_NEAREST) */
};

static const float QUAD_VERTS[] = {
    -1, -1,   1, -1,   1, 1,
    -1, -1,   1,  1,  -1, 1,
};

static GLuint
compile_sh(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, NULL, log);
        fprintf(stderr, "gl_voxel shader error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint
build_program(void)
{
    GLuint vs = compile_sh(GL_VERTEX_SHADER, RAYMARCH_VERT_SRC);
    GLuint fs = compile_sh(GL_FRAGMENT_SHADER, RAYMARCH_FRAG_SRC);
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
        fprintf(stderr, "gl_voxel link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_GlVoxelBuf *
dc_gl_voxel_buf_new(void)
{
    return calloc(1, sizeof(DC_GlVoxelBuf));
}

void
dc_gl_voxel_buf_free(DC_GlVoxelBuf *b)
{
    if (!b) return;
    if (b->color_tex) glDeleteTextures(1, &b->color_tex);
    if (b->sdf_tex)   glDeleteTextures(1, &b->sdf_tex);
    if (b->quad_vao)  glDeleteVertexArrays(1, &b->quad_vao);
    if (b->quad_vbo)  glDeleteBuffers(1, &b->quad_vbo);
    if (b->prog)      glDeleteProgram(b->prog);
    free(b);
}

/* =========================================================================
 * Upload — two textures: RGB8 color + R32F SDF (world-space distances)
 * ========================================================================= */
int
dc_gl_voxel_buf_upload(DC_GlVoxelBuf *b, const DC_VoxelGrid *grid)
{
    if (!b || !grid) return -1;

    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);
    float cs = dc_voxel_grid_cell_size(grid);

    b->cell_size = cs;
    b->bbox_min[0] = 0; b->bbox_min[1] = 0; b->bbox_min[2] = 0;
    b->bbox_max[0] = sx * cs; b->bbox_max[1] = sy * cs; b->bbox_max[2] = sz * cs;
    b->active_count = (int)dc_voxel_grid_active_count(grid);

    size_t total = (size_t)sx * sy * sz;

    /* Pack RGB color texture */
    unsigned char *rgb = malloc(total * 3);
    /* Pack SDF float texture */
    float *sdf = malloc(total * sizeof(float));
    if (!rgb || !sdf) { free(rgb); free(sdf); return -1; }

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        size_t idx = ((size_t)iz * sy + iy) * sx + ix;
        const DC_Voxel *v = dc_voxel_grid_get_const(grid, ix, iy, iz);
        if (v) {
            rgb[idx*3+0] = v->r;
            rgb[idx*3+1] = v->g;
            rgb[idx*3+2] = v->b;
            sdf[idx] = v->distance;  /* ACTUAL world-space distance */
        } else {
            rgb[idx*3+0] = 0;
            rgb[idx*3+1] = 0;
            rgb[idx*3+2] = 0;
            sdf[idx] = 1e6f;  /* far outside */
        }
    }

    /* Upload color texture */
    if (!b->color_tex) glGenTextures(1, &b->color_tex);
    glBindTexture(GL_TEXTURE_3D, b->color_tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, sx, sy, sz, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    /* Upload SDF texture — R32F, actual distances */
    if (!b->sdf_tex) glGenTextures(1, &b->sdf_tex);
    glBindTexture(GL_TEXTURE_3D, b->sdf_tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sx, sy, sz, 0,
                 GL_RED, GL_FLOAT, sdf);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, 0);
    free(rgb);
    free(sdf);

    /* Build fullscreen quad */
    if (!b->quad_vao) {
        glGenVertexArrays(1, &b->quad_vao);
        glGenBuffers(1, &b->quad_vbo);
        glBindVertexArray(b->quad_vao);
        glBindBuffer(GL_ARRAY_BUFFER, b->quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTS), QUAD_VERTS, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glBindVertexArray(0);
    }

    if (!b->prog)
        b->prog = build_program();

    b->uploaded = 1;
    return 0;
}

/* =========================================================================
 * Draw
 * ========================================================================= */
void
dc_gl_voxel_buf_draw(const DC_GlVoxelBuf *b,
                       const float *view_proj_inv,
                       const float *eye,
                       const float *light_dir,
                       int screen_w, int screen_h)
{
    if (!b || !b->uploaded || !b->prog || b->active_count <= 0) return;
    (void)screen_w; (void)screen_h;

    glUseProgram(b->prog);

    glUniformMatrix4fv(glGetUniformLocation(b->prog, "uInvVP"), 1, GL_FALSE, view_proj_inv);
    glUniform3fv(glGetUniformLocation(b->prog, "uEye"), 1, eye);
    glUniform3fv(glGetUniformLocation(b->prog, "uLightDir"), 1, light_dir);
    glUniform3fv(glGetUniformLocation(b->prog, "uBBoxMin"), 1, b->bbox_min);
    glUniform3fv(glGetUniformLocation(b->prog, "uBBoxMax"), 1, b->bbox_max);
    glUniform1f(glGetUniformLocation(b->prog, "uCellSize"), b->cell_size);
    glUniform1i(glGetUniformLocation(b->prog, "uBlocky"), b->blocky);

    /* Bind textures */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, b->color_tex);
    glUniform1i(glGetUniformLocation(b->prog, "uColor"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, b->sdf_tex);
    glUniform1i(glGetUniformLocation(b->prog, "uSDF"), 1);

    glBindVertexArray(b->quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, 0);
    glUseProgram(0);
}

/* =========================================================================
 * Queries
 * ========================================================================= */
int
dc_gl_voxel_buf_instance_count(const DC_GlVoxelBuf *b)
{
    return b ? b->active_count : 0;
}

void
dc_gl_voxel_buf_bounds(const DC_GlVoxelBuf *b, float *min_out, float *max_out)
{
    if (!b) return;
    if (min_out) { min_out[0] = b->bbox_min[0]; min_out[1] = b->bbox_min[1]; min_out[2] = b->bbox_min[2]; }
    if (max_out) { max_out[0] = b->bbox_max[0]; max_out[1] = b->bbox_max[1]; max_out[2] = b->bbox_max[2]; }
}

void
dc_gl_voxel_buf_set_blocky(DC_GlVoxelBuf *b, int blocky)
{
    if (!b) return;
    b->blocky = blocky ? 1 : 0;
    /* Update texture filter modes immediately if textures exist */
    GLint filter = blocky ? GL_NEAREST : GL_LINEAR;
    if (b->color_tex) {
        glBindTexture(GL_TEXTURE_3D, b->color_tex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
    }
    if (b->sdf_tex) {
        glBindTexture(GL_TEXTURE_3D, b->sdf_tex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
    }
    glBindTexture(GL_TEXTURE_3D, 0);
}

int
dc_gl_voxel_buf_get_blocky(const DC_GlVoxelBuf *b)
{
    return b ? b->blocky : 0;
}
