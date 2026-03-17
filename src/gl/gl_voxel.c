/*
 * gl_voxel.c — SDF raycast voxel renderer.
 *
 * No mesh geometry. No triangles. No instancing.
 *
 * The entire volume is a 3D texture of RGBA:
 *   RGB = voxel color (0-255 mapped to 0.0-1.0)
 *   A   = signed distance, remapped to [0,1] range
 *       (0.5 = surface, <0.5 = inside, >0.5 = outside)
 *
 * A fullscreen quad fires a ray per pixel. The fragment shader
 * marches through the 3D texture using ray-AABB intersection to
 * find entry/exit, then steps through the volume sampling the SDF.
 * When the distance crosses zero, we've hit surface. Compute the
 * normal from the SDF gradient (central differences on the 3D texture).
 * Light with Phong. Done.
 *
 * Trilinear interpolation on the 3D texture gives sub-voxel
 * surface precision for free.
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
    "uniform sampler3D uVolume;\n"       /* 3D SDF texture */
    "uniform vec3 uBBoxMin;\n"           /* volume world-space AABB */
    "uniform vec3 uBBoxMax;\n"
    "uniform vec3 uEye;\n"               /* camera position */
    "uniform vec3 uLightDir;\n"          /* normalized light direction */
    "uniform mat4 uInvVP;\n"            /* inverse view-projection */
    "uniform float uStepSize;\n"        /* ray step in normalized coords */
    "\n"
    "/* Ray-AABB intersection (returns tmin, tmax) */\n"
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
    "/* World position to volume UV (0..1) */\n"
    "vec3 worldToUV(vec3 p) {\n"
    "    return (p - uBBoxMin) / (uBBoxMax - uBBoxMin);\n"
    "}\n"
    "\n"
    "/* Sample SDF: alpha channel, remapped from [0,1] to [-range,+range] */\n"
    "float sampleSDF(vec3 uvw) {\n"
    "    return texture(uVolume, uvw).a * 2.0 - 1.0;\n"
    "}\n"
    "\n"
    "/* Compute normal from SDF gradient via central differences */\n"
    "vec3 calcNormal(vec3 uvw) {\n"
    "    vec3 texel = 1.0 / vec3(textureSize(uVolume, 0));\n"
    "    float dx = sampleSDF(uvw + vec3(texel.x,0,0)) - sampleSDF(uvw - vec3(texel.x,0,0));\n"
    "    float dy = sampleSDF(uvw + vec3(0,texel.y,0)) - sampleSDF(uvw - vec3(0,texel.y,0));\n"
    "    float dz = sampleSDF(uvw + vec3(0,0,texel.z)) - sampleSDF(uvw - vec3(0,0,texel.z));\n"
    "    return normalize(vec3(dx, dy, dz));\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    /* Reconstruct ray from screen UV through inverse VP */\n"
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
    "    /* Intersect ray with volume bounding box */\n"
    "    vec2 tHit = intersectAABB(ro, rd, uBBoxMin, uBBoxMax);\n"
    "    if (tHit.x > tHit.y) discard;  /* ray misses volume */\n"
    "\n"
    "    float tNear = max(tHit.x, 0.0);  /* clamp to camera */\n"
    "    float tFar  = tHit.y;\n"
    "\n"
    "    /* March through the volume */\n"
    "    float t = tNear;\n"
    "    float step = uStepSize;\n"
    "    bool hit = false;\n"
    "    vec3 hitUVW;\n"
    "    vec3 hitPos;\n"
    "\n"
    "    for (int i = 0; i < 512; i++) {\n"
    "        if (t > tFar) break;\n"
    "        vec3 p = ro + rd * t;\n"
    "        vec3 uvw = worldToUV(p);\n"
    "\n"
    "        /* Bounds check */\n"
    "        if (any(lessThan(uvw, vec3(0))) || any(greaterThan(uvw, vec3(1)))) {\n"
    "            t += step;\n"
    "            continue;\n"
    "        }\n"
    "\n"
    "        float d = sampleSDF(uvw);\n"
    "        if (d <= 0.0) {\n"
    "            hit = true;\n"
    "            hitUVW = uvw;\n"
    "            hitPos = p;\n"
    "            break;\n"
    "        }\n"
    "\n"
    "        /* Adaptive step: use SDF distance for sphere tracing */\n"
    "        t += max(d * length(uBBoxMax - uBBoxMin) * 0.5, step);\n"
    "    }\n"
    "\n"
    "    if (!hit) discard;\n"
    "\n"
    "    /* Compute normal and color */\n"
    "    vec3 N = calcNormal(hitUVW);\n"
    "    vec3 color = texture(uVolume, hitUVW).rgb;\n"
    "\n"
    "    /* Phong lighting */\n"
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
    GLuint  vol_tex;      /* 3D texture: RGBA8 */
    GLuint  quad_vao;     /* fullscreen quad */
    GLuint  quad_vbo;
    GLuint  prog;         /* raymarch shader program */
    int     uploaded;
    int     active_count;

    int     res_x, res_y, res_z;
    float   cell_size;
    float   bbox_min[3];
    float   bbox_max[3];
};

/* Fullscreen quad: two triangles covering NDC [-1,1] */
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
    if (b->vol_tex)  glDeleteTextures(1, &b->vol_tex);
    if (b->quad_vao) glDeleteVertexArrays(1, &b->quad_vao);
    if (b->quad_vbo) glDeleteBuffers(1, &b->quad_vbo);
    if (b->prog)     glDeleteProgram(b->prog);
    free(b);
}

/* =========================================================================
 * Upload — pack voxel grid into RGBA 3D texture
 *   R,G,B = voxel color
 *   A     = SDF distance, remapped: 0.5 = surface, 0 = deep inside, 1 = far outside
 * ========================================================================= */
int
dc_gl_voxel_buf_upload(DC_GlVoxelBuf *b, const DC_VoxelGrid *grid)
{
    if (!b || !grid) return -1;

    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);
    float cs = dc_voxel_grid_cell_size(grid);

    b->res_x = sx;
    b->res_y = sy;
    b->res_z = sz;
    b->cell_size = cs;
    b->bbox_min[0] = 0; b->bbox_min[1] = 0; b->bbox_min[2] = 0;
    b->bbox_max[0] = sx * cs; b->bbox_max[1] = sy * cs; b->bbox_max[2] = sz * cs;

    /* Count active */
    b->active_count = (int)dc_voxel_grid_active_count(grid);

    /* Pack RGBA8 texture data */
    size_t total = (size_t)sx * sy * sz;
    unsigned char *texdata = malloc(total * 4);
    if (!texdata) return -1;

    /* Find distance range for normalization */
    float dmin = 1e9f, dmax = -1e9f;
    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        const DC_Voxel *v = dc_voxel_grid_get_const(grid, ix, iy, iz);
        if (!v) continue;
        if (v->distance < dmin) dmin = v->distance;
        if (v->distance > dmax && v->distance < 1e6f) dmax = v->distance;
    }
    float drange = (dmax - dmin);
    if (drange < 0.001f) drange = 1.0f;

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        size_t idx = ((size_t)iz * sy + iy) * sx + ix;
        const DC_Voxel *v = dc_voxel_grid_get_const(grid, ix, iy, iz);
        if (!v) {
            texdata[idx*4+0] = 0;
            texdata[idx*4+1] = 0;
            texdata[idx*4+2] = 0;
            texdata[idx*4+3] = 255; /* far outside */
            continue;
        }
        texdata[idx*4+0] = v->r;
        texdata[idx*4+1] = v->g;
        texdata[idx*4+2] = v->b;
        /* Remap distance: 0 = deep inside, 0.5 = surface, 1.0 = far outside.
         * Alpha = clamp((distance / range) * 0.5 + 0.5, 0, 1) * 255 */
        float norm = (v->distance / drange) * 0.5f + 0.5f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        texdata[idx*4+3] = (unsigned char)(norm * 255.0f);
    }

    /* Create/update 3D texture */
    if (!b->vol_tex)
        glGenTextures(1, &b->vol_tex);

    glBindTexture(GL_TEXTURE_3D, b->vol_tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8,
                 sx, sy, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE, texdata);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_3D, 0);

    free(texdata);

    /* Build fullscreen quad if not yet */
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

    /* Build shader if not yet */
    if (!b->prog)
        b->prog = build_program();

    b->uploaded = 1;
    return 0;
}

/* =========================================================================
 * Draw — the pure raycast
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

    /* Uniforms */
    glUniformMatrix4fv(glGetUniformLocation(b->prog, "uInvVP"), 1, GL_FALSE, view_proj_inv);
    glUniform3fv(glGetUniformLocation(b->prog, "uEye"), 1, eye);
    glUniform3fv(glGetUniformLocation(b->prog, "uLightDir"), 1, light_dir);
    glUniform3fv(glGetUniformLocation(b->prog, "uBBoxMin"), 1, b->bbox_min);
    glUniform3fv(glGetUniformLocation(b->prog, "uBBoxMax"), 1, b->bbox_max);

    /* Step size: ~1 voxel width in world space, divided by bbox diagonal */
    float step = b->cell_size * 0.5f /
        sqrtf((b->bbox_max[0]-b->bbox_min[0])*(b->bbox_max[0]-b->bbox_min[0]) +
              (b->bbox_max[1]-b->bbox_min[1])*(b->bbox_max[1]-b->bbox_min[1]) +
              (b->bbox_max[2]-b->bbox_min[2])*(b->bbox_max[2]-b->bbox_min[2]));
    glUniform1f(glGetUniformLocation(b->prog, "uStepSize"), step);

    /* Bind 3D texture */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, b->vol_tex);
    glUniform1i(glGetUniformLocation(b->prog, "uVolume"), 0);

    /* Draw fullscreen quad — every pixel fires a ray */
    glBindVertexArray(b->quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

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
