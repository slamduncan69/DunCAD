/*
 * gl_voxel.c — Dual-mode voxel renderer.
 *
 * Two rendering modes:
 *   BLOCKY: CPU-built face mesh. Each active voxel with an exposed face
 *           gets quad geometry. Standard triangle rasterization with Phong.
 *           True voxel cubes — no SDF artifacts.
 *
 *   SMOOTH: SDF raymarching via fullscreen quad. Sphere tracing finds
 *           the SDF zero-crossing, normals from gradient with large epsilon.
 *           Smooth surfaces from the distance field.
 */

#include "gl/gl_voxel.h"
#include "voxel/voxel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Blocky mode shaders — standard vertex/fragment for triangle mesh
 * ========================================================================= */

static const char *BLOCKY_VERT_SRC =
    "#version 320 es\n"
    "precision highp float;\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in vec3 aColor;\n"
    "uniform mat4 uVP;\n"
    "out vec3 vNormal;\n"
    "out vec3 vColor;\n"
    "out vec3 vWorldPos;\n"
    "void main() {\n"
    "    gl_Position = uVP * vec4(aPos, 1.0);\n"
    "    vNormal = aNormal;\n"
    "    vColor = aColor;\n"
    "    vWorldPos = aPos;\n"
    "}\n";

static const char *BLOCKY_FRAG_SRC =
    "#version 320 es\n"
    "precision highp float;\n"
    "in vec3 vNormal;\n"
    "in vec3 vColor;\n"
    "in vec3 vWorldPos;\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uEye;\n"
    "uniform vec3 uLightDir;\n"
    "void main() {\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float diff2 = max(dot(N, -L), 0.0) * 0.25;\n"
    "    float ambient = 0.18;\n"
    "    vec3 V = normalize(uEye - vWorldPos);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.2;\n"
    "    vec3 result = vColor * (ambient + diff + diff2) + vec3(spec);\n"
    "    FragColor = vec4(result, 1.0);\n"
    "}\n";

/* =========================================================================
 * Smooth mode shaders — SDF raymarching
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

/* Split into two string literals for C99 4095-char limit */
static const char *RAYMARCH_FRAG_SRC =
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
    /* second string literal to stay under 4095 chars */
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
    "    float df = max(dot(N,L),0.0), d2 = max(dot(N,-L),0.0)*0.25;\n"
    "    vec3 V = normalize(uEye-hp), H = normalize(L+V);\n"
    "    float sp = pow(max(dot(N,H),0.0),48.0)*0.3;\n"
    "    FragColor = vec4(col*(0.15+df+d2)+vec3(sp), 1.0);\n"
    "}\n";

/* =========================================================================
 * Internal structure
 * ========================================================================= */
struct DC_GlVoxelBuf {
    /* Shared state */
    int     uploaded;
    int     active_count;
    float   cell_size;
    float   bbox_min[3];
    float   bbox_max[3];
    int     blocky;

    /* Blocky mode: triangle mesh of visible voxel faces */
    GLuint  mesh_vao;
    GLuint  mesh_vbo;
    GLuint  mesh_prog;
    int     mesh_tri_count;

    /* Smooth mode: SDF raymarching */
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
 * Blocky mesh builder — extract visible faces from voxel grid
 *
 * For each active voxel, check its 6 neighbors. If a neighbor is empty
 * (inactive or out of bounds), emit a quad (2 triangles) for that face.
 * Each vertex: position(3) + normal(3) + color(3) = 9 floats.
 * ========================================================================= */

/* Face data: 6 faces × 4 vertices × 3 components (offset from cell origin) */
static const float FACE_VERTS[6][4][3] = {
    /* +X */ {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},
    /* -X */ {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},
    /* +Y */ {{0,1,0},{1,1,0},{1,1,1},{0,1,1}},
    /* -Y */ {{0,0,1},{1,0,1},{1,0,0},{0,0,0}},
    /* +Z */ {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},
    /* -Z */ {{1,0,0},{0,0,0},{0,1,0},{1,1,0}},
};
static const float FACE_NORMALS[6][3] = {
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
};
/* Neighbor offsets for each face */
static const int FACE_NEIGHBOR[6][3] = {
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
};

static int
is_active(const DC_VoxelGrid *grid, int ix, int iy, int iz)
{
    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);
    if (ix < 0 || iy < 0 || iz < 0 || ix >= sx || iy >= sy || iz >= sz)
        return 0;
    const DC_Voxel *v = dc_voxel_grid_get_const(grid, ix, iy, iz);
    return v && v->active;
}

static float *
build_face_mesh(const DC_VoxelGrid *grid, int *out_tri_count)
{
    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);
    float cs = dc_voxel_grid_cell_size(grid);
    float gox, goy, goz;
    dc_voxel_grid_get_origin(grid, &gox, &goy, &goz);

    /* Worst case: every active voxel has all 6 faces visible = 12 tris each */
    int active = (int)dc_voxel_grid_active_count(grid);
    size_t max_tris = (size_t)active * 12;
    /* 3 vertices per triangle, 9 floats per vertex (pos + normal + color) */
    float *buf = malloc(max_tris * 3 * 9 * sizeof(float));
    if (!buf) { *out_tri_count = 0; return NULL; }

    int tri_count = 0;
    float *ptr = buf;

    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        if (!is_active(grid, ix, iy, iz)) continue;

        const DC_Voxel *v = dc_voxel_grid_get_const(grid, ix, iy, iz);
        float cr = v->r / 255.0f, cg = v->g / 255.0f, cb = v->b / 255.0f;
        float ox = gox + ix * cs, oy = goy + iy * cs, oz = goz + iz * cs;

        for (int f = 0; f < 6; f++) {
            int nx = ix + FACE_NEIGHBOR[f][0];
            int ny = iy + FACE_NEIGHBOR[f][1];
            int nz = iz + FACE_NEIGHBOR[f][2];
            if (is_active(grid, nx, ny, nz)) continue;

            /* Emit 2 triangles for this face (quad: v0,v1,v2 + v0,v2,v3) */
            const int indices[6] = {0, 1, 2, 0, 2, 3};
            for (int t = 0; t < 6; t++) {
                int vi = indices[t];
                *ptr++ = ox + FACE_VERTS[f][vi][0] * cs;
                *ptr++ = oy + FACE_VERTS[f][vi][1] * cs;
                *ptr++ = oz + FACE_VERTS[f][vi][2] * cs;
                *ptr++ = FACE_NORMALS[f][0];
                *ptr++ = FACE_NORMALS[f][1];
                *ptr++ = FACE_NORMALS[f][2];
                *ptr++ = cr;
                *ptr++ = cg;
                *ptr++ = cb;
            }
            tri_count += 2;
        }
    }

    *out_tri_count = tri_count;
    return buf;
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
    if (b->mesh_vao)  glDeleteVertexArrays(1, &b->mesh_vao);
    if (b->mesh_vbo)  glDeleteBuffers(1, &b->mesh_vbo);
    if (b->mesh_prog) glDeleteProgram(b->mesh_prog);
    free(b);
}

/* =========================================================================
 * Upload — build both representations (mesh + textures)
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

    /* --- Blocky mesh --- */
    int tri_count = 0;
    float *mesh_data = build_face_mesh(grid, &tri_count);
    b->mesh_tri_count = tri_count;

    if (mesh_data && tri_count > 0) {
        if (!b->mesh_vao) {
            glGenVertexArrays(1, &b->mesh_vao);
            glGenBuffers(1, &b->mesh_vbo);
        }
        glBindVertexArray(b->mesh_vao);
        glBindBuffer(GL_ARRAY_BUFFER, b->mesh_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)tri_count * 3 * 9 * (GLsizeiptr)sizeof(float),
                     mesh_data, GL_STATIC_DRAW);
        /* pos(3) + normal(3) + color(3) = stride 36 bytes */
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6*sizeof(float)));
        glBindVertexArray(0);
    }
    free(mesh_data);

    if (!b->mesh_prog) {
        GLuint vs = compile_sh(GL_VERTEX_SHADER, BLOCKY_VERT_SRC);
        GLuint fs = compile_sh(GL_FRAGMENT_SHADER, BLOCKY_FRAG_SRC);
        b->mesh_prog = link_program(vs, fs);
    }

    /* --- SDF textures for smooth mode --- */
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

    if (!b->color_tex) glGenTextures(1, &b->color_tex);
    glBindTexture(GL_TEXTURE_3D, b->color_tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, sx, sy, sz, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    if (!b->sdf_tex) glGenTextures(1, &b->sdf_tex);
    glBindTexture(GL_TEXTURE_3D, b->sdf_tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sx, sy, sz, 0, GL_RED, GL_FLOAT, sdf);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, 0);
    free(rgb);
    free(sdf);

    /* Fullscreen quad for raymarching */
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
        GLuint vs = compile_sh(GL_VERTEX_SHADER, RAYMARCH_VERT_SRC);
        GLuint fs = compile_sh(GL_FRAGMENT_SHADER, RAYMARCH_FRAG_SRC);
        b->ray_prog = link_program(vs, fs);
    }

    b->uploaded = 1;
    return 0;
}

/* =========================================================================
 * Draw — dispatches to blocky (mesh) or smooth (raymarching)
 * ========================================================================= */
void
dc_gl_voxel_buf_draw(const DC_GlVoxelBuf *b,
                       const float *view_proj_inv,
                       const float *eye,
                       const float *light_dir,
                       int screen_w, int screen_h)
{
    if (!b || !b->uploaded || b->active_count <= 0) return;
    (void)screen_w; (void)screen_h;

    if (b->blocky) {
        /* --- Blocky: rasterize face mesh --- */
        if (!b->mesh_prog || b->mesh_tri_count <= 0) return;

        /* We receive inverse(VP). We need VP itself. Invert it back.
         * The viewport already computed VP; we just need to pass it.
         * For now, invert the inverse. The caller should be updated
         * to pass VP directly in the future. */
        /* Actually, compute VP from the inverse. 4x4 invert. */
        float vp[16];
        {
            /* Simple 4x4 inversion via cofactors — same as sdf.c */
            const float *m = view_proj_inv;
            float t[16];
            #define M(m,r,c) ((m)[(c)*4+(r)])
            t[0]  =  M(m,1,1)*(M(m,2,2)*M(m,3,3)-M(m,2,3)*M(m,3,2))
                     -M(m,1,2)*(M(m,2,1)*M(m,3,3)-M(m,2,3)*M(m,3,1))
                     +M(m,1,3)*(M(m,2,1)*M(m,3,2)-M(m,2,2)*M(m,3,1));
            t[4]  = -M(m,0,1)*(M(m,2,2)*M(m,3,3)-M(m,2,3)*M(m,3,2))
                     +M(m,0,2)*(M(m,2,1)*M(m,3,3)-M(m,2,3)*M(m,3,1))
                     -M(m,0,3)*(M(m,2,1)*M(m,3,2)-M(m,2,2)*M(m,3,1));
            t[8]  =  M(m,0,1)*(M(m,1,2)*M(m,3,3)-M(m,1,3)*M(m,3,2))
                     -M(m,0,2)*(M(m,1,1)*M(m,3,3)-M(m,1,3)*M(m,3,1))
                     +M(m,0,3)*(M(m,1,1)*M(m,3,2)-M(m,1,2)*M(m,3,1));
            t[12] = -M(m,0,1)*(M(m,1,2)*M(m,2,3)-M(m,1,3)*M(m,2,2))
                     +M(m,0,2)*(M(m,1,1)*M(m,2,3)-M(m,1,3)*M(m,2,1))
                     -M(m,0,3)*(M(m,1,1)*M(m,2,2)-M(m,1,2)*M(m,2,1));
            float det = M(m,0,0)*t[0] + M(m,1,0)*t[4] + M(m,2,0)*t[8] + M(m,3,0)*t[12];
            if (fabsf(det) < 1e-12f) return;
            t[1]  = -M(m,1,0)*(M(m,2,2)*M(m,3,3)-M(m,2,3)*M(m,3,2))+M(m,1,2)*(M(m,2,0)*M(m,3,3)-M(m,2,3)*M(m,3,0))-M(m,1,3)*(M(m,2,0)*M(m,3,2)-M(m,2,2)*M(m,3,0));
            t[5]  =  M(m,0,0)*(M(m,2,2)*M(m,3,3)-M(m,2,3)*M(m,3,2))-M(m,0,2)*(M(m,2,0)*M(m,3,3)-M(m,2,3)*M(m,3,0))+M(m,0,3)*(M(m,2,0)*M(m,3,2)-M(m,2,2)*M(m,3,0));
            t[9]  = -M(m,0,0)*(M(m,1,2)*M(m,3,3)-M(m,1,3)*M(m,3,2))+M(m,0,2)*(M(m,1,0)*M(m,3,3)-M(m,1,3)*M(m,3,0))-M(m,0,3)*(M(m,1,0)*M(m,3,2)-M(m,1,2)*M(m,3,0));
            t[13] =  M(m,0,0)*(M(m,1,2)*M(m,2,3)-M(m,1,3)*M(m,2,2))-M(m,0,2)*(M(m,1,0)*M(m,2,3)-M(m,1,3)*M(m,2,0))+M(m,0,3)*(M(m,1,0)*M(m,2,2)-M(m,1,2)*M(m,2,0));
            t[2]  =  M(m,1,0)*(M(m,2,1)*M(m,3,3)-M(m,2,3)*M(m,3,1))-M(m,1,1)*(M(m,2,0)*M(m,3,3)-M(m,2,3)*M(m,3,0))+M(m,1,3)*(M(m,2,0)*M(m,3,1)-M(m,2,1)*M(m,3,0));
            t[6]  = -M(m,0,0)*(M(m,2,1)*M(m,3,3)-M(m,2,3)*M(m,3,1))+M(m,0,1)*(M(m,2,0)*M(m,3,3)-M(m,2,3)*M(m,3,0))-M(m,0,3)*(M(m,2,0)*M(m,3,1)-M(m,2,1)*M(m,3,0));
            t[10] =  M(m,0,0)*(M(m,1,1)*M(m,3,3)-M(m,1,3)*M(m,3,1))-M(m,0,1)*(M(m,1,0)*M(m,3,3)-M(m,1,3)*M(m,3,0))+M(m,0,3)*(M(m,1,0)*M(m,3,1)-M(m,1,1)*M(m,3,0));
            t[14] = -M(m,0,0)*(M(m,1,1)*M(m,2,3)-M(m,1,3)*M(m,2,1))+M(m,0,1)*(M(m,1,0)*M(m,2,3)-M(m,1,3)*M(m,2,0))-M(m,0,3)*(M(m,1,0)*M(m,2,1)-M(m,1,1)*M(m,2,0));
            t[3]  = -M(m,1,0)*(M(m,2,1)*M(m,3,2)-M(m,2,2)*M(m,3,1))+M(m,1,1)*(M(m,2,0)*M(m,3,2)-M(m,2,2)*M(m,3,0))-M(m,1,2)*(M(m,2,0)*M(m,3,1)-M(m,2,1)*M(m,3,0));
            t[7]  =  M(m,0,0)*(M(m,2,1)*M(m,3,2)-M(m,2,2)*M(m,3,1))-M(m,0,1)*(M(m,2,0)*M(m,3,2)-M(m,2,2)*M(m,3,0))+M(m,0,2)*(M(m,2,0)*M(m,3,1)-M(m,2,1)*M(m,3,0));
            t[11] = -M(m,0,0)*(M(m,1,1)*M(m,3,2)-M(m,1,2)*M(m,3,1))+M(m,0,1)*(M(m,1,0)*M(m,3,2)-M(m,1,2)*M(m,3,0))-M(m,0,2)*(M(m,1,0)*M(m,3,1)-M(m,1,1)*M(m,3,0));
            t[15] =  M(m,0,0)*(M(m,1,1)*M(m,2,2)-M(m,1,2)*M(m,2,1))-M(m,0,1)*(M(m,1,0)*M(m,2,2)-M(m,1,2)*M(m,2,0))+M(m,0,2)*(M(m,1,0)*M(m,2,1)-M(m,1,1)*M(m,2,0));
            float inv_det = 1.0f / det;
            for (int i = 0; i < 16; i++) vp[i] = t[i] * inv_det;
            #undef M
        }

        glUseProgram(b->mesh_prog);
        glUniformMatrix4fv(glGetUniformLocation(b->mesh_prog, "uVP"), 1, GL_FALSE, vp);
        glUniform3fv(glGetUniformLocation(b->mesh_prog, "uEye"), 1, eye);
        glUniform3fv(glGetUniformLocation(b->mesh_prog, "uLightDir"), 1, light_dir);

        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(b->mesh_vao);
        glDrawArrays(GL_TRIANGLES, 0, b->mesh_tri_count * 3);
        glBindVertexArray(0);
        glUseProgram(0);

    } else {
        /* --- Smooth: SDF raymarching --- */
        if (!b->ray_prog) return;

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
    }
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
}

int
dc_gl_voxel_buf_get_blocky(const DC_GlVoxelBuf *b)
{
    return b ? b->blocky : 0;
}
