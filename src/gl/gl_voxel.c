/*
 * gl_voxel.c — Pure SDF voxel renderer.
 *
 * Holy Path V, Phase 1: NO TRIANGLES.
 * One fullscreen quad. One shader. One 3D texture.
 * Blocky mode = GL_NEAREST. Smooth mode = GL_LINEAR.
 * The sin of triangles has been purged from this file.
 */

#include "gl/gl_voxel.h"
#include "voxel/voxel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * SDF raymarching shader — the ONLY rendering path
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

static const char *FRAG_SRC =
    "#version 320 es\n"
    "precision highp float;\n"
    "precision highp sampler3D;\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler3D uColor;\n"
    "uniform sampler3D uSDF;\n"
    "uniform vec3 uBBoxMin;\n"
    "uniform vec3 uBBoxMax;\n"
    "uniform vec3 uEye;\n"
    "uniform vec3 uLightDir;\n"
    "uniform mat4 uInvVP;\n"
    "uniform float uCellSize;\n"
    "\n"
    "vec2 intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {\n"
    "    vec3 inv = 1.0 / rd;\n"
    "    vec3 t0 = (bmin - ro) * inv;\n"
    "    vec3 t1 = (bmax - ro) * inv;\n"
    "    vec3 mn = min(t0, t1);\n"
    "    vec3 mx = max(t0, t1);\n"
    "    return vec2(max(max(mn.x,mn.y),mn.z), min(min(mx.x,mx.y),mx.z));\n"
    "}\n"
    "vec3 worldToUV(vec3 p) {\n"
    "    return (p - uBBoxMin) / (uBBoxMax - uBBoxMin);\n"
    "}\n"
    "float sdf(vec3 uvw) { return texture(uSDF, uvw).r; }\n"
    "vec3 calcNormal(vec3 uvw) {\n"
    "    vec3 e = 3.0 / vec3(textureSize(uSDF, 0));\n"
    "    float dx = sdf(uvw+vec3(e.x,0,0)) - sdf(uvw-vec3(e.x,0,0));\n"
    "    float dy = sdf(uvw+vec3(0,e.y,0)) - sdf(uvw-vec3(0,e.y,0));\n"
    "    float dz = sdf(uvw+vec3(0,0,e.z)) - sdf(uvw-vec3(0,0,e.z));\n"
    "    return normalize(vec3(dx,dy,dz) / (uBBoxMax - uBBoxMin));\n"
    "}\n"
    "void main() {\n"
    "    vec4 nn = uInvVP * vec4(vUV*2.0-1.0,-1,1);\n"
    "    vec4 ff = uInvVP * vec4(vUV*2.0-1.0, 1,1);\n"
    "    nn /= nn.w; ff /= ff.w;\n"
    "    vec3 ro = uEye, rd = normalize(ff.xyz - nn.xyz);\n"
    "    vec2 th = intersectAABB(ro, rd, uBBoxMin, uBBoxMax);\n"
    "    if (th.x > th.y) discard;\n"
    "    float t = max(th.x, 0.0), tF = th.y;\n"
    "    float stp = uCellSize*0.5, mn = uCellSize*0.2, mx = uCellSize*2.0;\n"
    "    vec3 hp, hu; bool hit = false;\n"
    "    for (int i = 0; i < 512; i++) {\n"
    "        if (t > tF) break;\n"
    "        vec3 p = ro + rd*t;\n"
    "        vec3 u = worldToUV(p);\n"
    "        if (any(lessThan(u,vec3(0)))||any(greaterThan(u,vec3(1)))) { t+=stp; continue; }\n"
    "        float d = sdf(u);\n"
    "        if (d <= 0.0) {\n"
    "            float lo=t-stp, hi=t;\n"
    "            for (int j=0;j<6;j++) {\n"
    "                float mid=(lo+hi)*0.5;\n"
    "                if (sdf(worldToUV(ro+rd*mid))<=0.0) hi=mid; else lo=mid;\n"
    "            }\n"
    "            t=(lo+hi)*0.5; hp=ro+rd*t; hu=worldToUV(hp); hit=true; break;\n"
    "        }\n"
    "        stp=clamp(d,mn,mx); t+=stp;\n"
    "    }\n"
    "    if (!hit) discard;\n"
    "    vec3 N = calcNormal(hu);\n"
    "    vec3 col = texture(uColor, hu).rgb;\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    float df = max(dot(N,L),0.0), d2 = max(dot(N,-L),0.0)*0.2;\n"
    "    vec3 V = normalize(uEye-hp), H = normalize(L+V);\n"
    "    float sp = pow(max(dot(N,H),0.0),64.0)*0.1;\n"
    "    FragColor = vec4(col*(0.15+df+d2)+vec3(sp), 1.0);\n"
    "}\n";

/* =========================================================================
 * Internal structure — NO triangle mesh fields
 * ========================================================================= */
struct DC_GlVoxelBuf {
    int     uploaded;
    int     active_count;
    float   cell_size;
    float   bbox_min[3];
    float   bbox_max[3];
    int     blocky;

    /* SDF raymarching — the ONLY rendering path */
    GLuint  color_tex;
    GLuint  sdf_tex;
    GLuint  quad_vao;
    GLuint  quad_vbo;
    GLuint  ray_prog;
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
link_program(GLuint vs, GLuint fs)
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
    if (b->ray_prog)  glDeleteProgram(b->ray_prog);
    free(b);
}

/* =========================================================================
 * Upload — 3D textures only. No triangle mesh.
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
    float ox, oy, oz;
    dc_voxel_grid_get_origin(grid, &ox, &oy, &oz);
    b->bbox_min[0] = ox; b->bbox_min[1] = oy; b->bbox_min[2] = oz;
    b->bbox_max[0] = ox + sx * cs; b->bbox_max[1] = oy + sy * cs; b->bbox_max[2] = oz + sz * cs;
    b->active_count = (int)dc_voxel_grid_active_count(grid);

    /* --- 3D textures: SDF + color --- */
    size_t total = (size_t)sx * sy * sz;
    unsigned char *rgb = malloc(total * 3);
    float *sdf = malloc(total * sizeof(float));
    if (!rgb || !sdf) { free(rgb); free(sdf); b->uploaded = 1; return 0; }

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        size_t idx = ((size_t)iz * sy + iy) * sx + ix;
        const DC_Voxel *v = dc_voxel_grid_get_const(grid, ix, iy, iz);
        if (v) {
            rgb[idx*3+0] = v->r; rgb[idx*3+1] = v->g; rgb[idx*3+2] = v->b;
            sdf[idx] = v->distance;
        } else {
            rgb[idx*3+0] = rgb[idx*3+1] = rgb[idx*3+2] = 0;
            sdf[idx] = 1e6f;
        }
    }

    /* Texture filter depends on blocky flag */
    GLenum filter = b->blocky ? GL_NEAREST : GL_LINEAR;

    if (!b->color_tex) glGenTextures(1, &b->color_tex);
    glBindTexture(GL_TEXTURE_3D, b->color_tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, sx, sy, sz, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    if (!b->sdf_tex) glGenTextures(1, &b->sdf_tex);
    glBindTexture(GL_TEXTURE_3D, b->sdf_tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sx, sy, sz, 0, GL_RED, GL_FLOAT, sdf);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, 0);
    free(rgb);
    free(sdf);

    /* Fullscreen quad */
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

    if (!b->ray_prog) {
        GLuint vs = compile_sh(GL_VERTEX_SHADER, VERT_SRC);
        GLuint fs = compile_sh(GL_FRAGMENT_SHADER, FRAG_SRC);
        b->ray_prog = link_program(vs, fs);
    }

    b->uploaded = 1;
    return 0;
}

/* =========================================================================
 * Draw — ONE path. SDF raymarching. Always.
 * ========================================================================= */
void
dc_gl_voxel_buf_draw(const DC_GlVoxelBuf *b,
                       const float *view_proj_inv,
                       const float *eye,
                       const float *light_dir,
                       int screen_w, int screen_h)
{
    if (!b || !b->uploaded || b->active_count <= 0) return;
    if (!b->ray_prog) return;
    (void)screen_w; (void)screen_h;

    glDisable(GL_DEPTH_TEST);

    glUseProgram(b->ray_prog);
    glUniformMatrix4fv(glGetUniformLocation(b->ray_prog, "uInvVP"), 1, GL_FALSE, view_proj_inv);
    glUniform3fv(glGetUniformLocation(b->ray_prog, "uEye"), 1, eye);
    glUniform3fv(glGetUniformLocation(b->ray_prog, "uLightDir"), 1, light_dir);
    glUniform3fv(glGetUniformLocation(b->ray_prog, "uBBoxMin"), 1, b->bbox_min);
    glUniform3fv(glGetUniformLocation(b->ray_prog, "uBBoxMax"), 1, b->bbox_max);
    glUniform1f(glGetUniformLocation(b->ray_prog, "uCellSize"), b->cell_size);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, b->color_tex);
    glUniform1i(glGetUniformLocation(b->ray_prog, "uColor"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, b->sdf_tex);
    glUniform1i(glGetUniformLocation(b->ray_prog, "uSDF"), 1);

    glBindVertexArray(b->quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, 0);
    glUseProgram(0);

    glEnable(GL_DEPTH_TEST);
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

/* Blocky toggle: changes texture filtering between GL_NEAREST and GL_LINEAR.
 * GL_NEAREST = discrete voxel materialization (blocky).
 * GL_LINEAR  = smooth interpolated surface.
 * Same shader. Same quad. Same math. Different truth. */
void
dc_gl_voxel_buf_set_blocky(DC_GlVoxelBuf *b, int blocky)
{
    if (!b) return;
    b->blocky = blocky ? 1 : 0;
    GLenum filter = b->blocky ? GL_NEAREST : GL_LINEAR;
    if (b->sdf_tex) {
        glBindTexture(GL_TEXTURE_3D, b->sdf_tex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
    }
    if (b->color_tex) {
        glBindTexture(GL_TEXTURE_3D, b->color_tex);
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
