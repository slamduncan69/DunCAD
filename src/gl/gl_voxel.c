/*
 * gl_voxel.c — GPU-instanced voxel renderer.
 *
 * Renders active voxels as instanced cubes. A unit cube template (36 verts
 * with normals) is drawn once per active voxel via glDrawArraysInstanced.
 * Per-instance data: vec3 position + vec3 color, packed in an instance VBO.
 */

#include "gl/gl_voxel.h"
#include "voxel/voxel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Unit cube geometry — 36 vertices (6 faces * 2 triangles * 3 verts)
 * Interleaved: normal(3f) + position(3f) per vertex
 * ========================================================================= */
static const float CUBE_VERTS[] = {
    /* Front face (+Z) */
     0, 0, 1,  0,0,1,   0, 0, 1,  1,0,1,   0, 0, 1,  1,1,1,
     0, 0, 1,  0,0,1,   0, 0, 1,  1,1,1,   0, 0, 1,  0,1,1,
    /* Back face (-Z) */
     0, 0,-1,  1,0,0,   0, 0,-1,  0,0,0,   0, 0,-1,  0,1,0,
     0, 0,-1,  1,0,0,   0, 0,-1,  0,1,0,   0, 0,-1,  1,1,0,
    /* Right face (+X) */
     1, 0, 0,  1,0,1,   1, 0, 0,  1,0,0,   1, 0, 0,  1,1,0,
     1, 0, 0,  1,0,1,   1, 0, 0,  1,1,0,   1, 0, 0,  1,1,1,
    /* Left face (-X) */
    -1, 0, 0,  0,0,0,  -1, 0, 0,  0,0,1,  -1, 0, 0,  0,1,1,
    -1, 0, 0,  0,0,0,  -1, 0, 0,  0,1,1,  -1, 0, 0,  0,1,0,
    /* Top face (+Y) */
     0, 1, 0,  0,1,1,   0, 1, 0,  1,1,1,   0, 1, 0,  1,1,0,
     0, 1, 0,  0,1,1,   0, 1, 0,  1,1,0,   0, 1, 0,  0,1,0,
    /* Bottom face (-Y) */
     0,-1, 0,  0,0,0,   0,-1, 0,  1,0,0,   0,-1, 0,  1,0,1,
     0,-1, 0,  0,0,0,   0,-1, 0,  1,0,1,   0,-1, 0,  0,0,1,
};
#define CUBE_VERT_COUNT 36

/* =========================================================================
 * Internal structure
 * ========================================================================= */
struct DC_GlVoxelBuf {
    GLuint  cube_vao;
    GLuint  cube_vbo;       /* unit cube geometry */
    GLuint  inst_vbo;       /* per-instance data: pos(3f) + color(3f) */
    int     inst_count;     /* number of active voxel instances */
    int     uploaded;

    float   cell_size;      /* world units per voxel */
    float   bbox_min[3];
    float   bbox_max[3];
};

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_GlVoxelBuf *
dc_gl_voxel_buf_new(void)
{
    DC_GlVoxelBuf *b = calloc(1, sizeof(DC_GlVoxelBuf));
    return b;
}

void
dc_gl_voxel_buf_free(DC_GlVoxelBuf *b)
{
    if (!b) return;
    if (b->cube_vao) glDeleteVertexArrays(1, &b->cube_vao);
    if (b->cube_vbo) glDeleteBuffers(1, &b->cube_vbo);
    if (b->inst_vbo) glDeleteBuffers(1, &b->inst_vbo);
    free(b);
}

/* =========================================================================
 * Upload
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

    /* Count active voxels */
    size_t active = dc_voxel_grid_active_count(grid);
    b->inst_count = (int)active;

    if (active == 0) {
        b->uploaded = 1;
        return 0;
    }

    /* Build instance data: position (3f) + color (3f) per instance */
    float *inst_data = malloc(active * 6 * sizeof(float));
    if (!inst_data) return -1;

    float minx = 1e9f, miny = 1e9f, minz = 1e9f;
    float maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;

    size_t idx = 0;
    for (int iz = 0; iz < sz; iz++)
    for (int iy = 0; iy < sy; iy++)
    for (int ix = 0; ix < sx; ix++) {
        const DC_Voxel *v = dc_voxel_grid_get_const(grid, ix, iy, iz);
        if (!v || !v->active) continue;

        /* Position: cell origin (not center — cube verts go 0..1) */
        float wx = ix * cs;
        float wy = iy * cs;
        float wz = iz * cs;

        inst_data[idx * 6 + 0] = wx;
        inst_data[idx * 6 + 1] = wy;
        inst_data[idx * 6 + 2] = wz;
        inst_data[idx * 6 + 3] = v->r / 255.0f;
        inst_data[idx * 6 + 4] = v->g / 255.0f;
        inst_data[idx * 6 + 5] = v->b / 255.0f;
        idx++;

        if (wx < minx) minx = wx;
        if (wy < miny) miny = wy;
        if (wz < minz) minz = wz;
        if (wx + cs > maxx) maxx = wx + cs;
        if (wy + cs > maxy) maxy = wy + cs;
        if (wz + cs > maxz) maxz = wz + cs;
    }

    b->bbox_min[0] = minx; b->bbox_min[1] = miny; b->bbox_min[2] = minz;
    b->bbox_max[0] = maxx; b->bbox_max[1] = maxy; b->bbox_max[2] = maxz;

    /* Create/update GPU buffers */
    if (!b->cube_vao) {
        glGenVertexArrays(1, &b->cube_vao);
        glGenBuffers(1, &b->cube_vbo);
        glGenBuffers(1, &b->inst_vbo);
    }

    glBindVertexArray(b->cube_vao);

    /* Upload unit cube (scaled by cell_size in shader via model matrix) */
    glBindBuffer(GL_ARRAY_BUFFER, b->cube_vbo);
    /* Scale cube verts by cell_size */
    float *scaled = malloc(sizeof(CUBE_VERTS));
    if (!scaled) { free(inst_data); return -1; }
    memcpy(scaled, CUBE_VERTS, sizeof(CUBE_VERTS));
    for (int i = 0; i < CUBE_VERT_COUNT; i++) {
        /* Position is at offset 3,4,5 in each 6-float vertex */
        scaled[i * 6 + 3] *= cs;
        scaled[i * 6 + 4] *= cs;
        scaled[i * 6 + 5] *= cs;
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTS), scaled, GL_STATIC_DRAW);
    free(scaled);

    /* Attribute 0: normal (3f) */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

    /* Attribute 1: position (3f) — this is the per-VERTEX position within the cube */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));

    /* Upload instance data */
    glBindBuffer(GL_ARRAY_BUFFER, b->inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(active * 6 * sizeof(float)),
                 inst_data, GL_STATIC_DRAW);

    /* Attribute 2: instance position (3f) */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);  /* once per instance */

    /* Attribute 3: instance color (3f) */
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
    free(inst_data);
    b->uploaded = 1;
    return 0;
}

/* =========================================================================
 * Draw
 * ========================================================================= */

/* Instanced voxel shader — adds instance offset to vertex position,
 * uses instance color instead of uniform color. */
static const char *VOXEL_VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec3 aNormal;\n"
    "layout(location=1) in vec3 aPos;\n"
    "layout(location=2) in vec3 aInstPos;\n"
    "layout(location=3) in vec3 aInstColor;\n"
    "uniform mat4 uVP;\n"
    "out vec3 vNormal;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "    vec3 worldPos = aPos + aInstPos;\n"
    "    gl_Position = uVP * vec4(worldPos, 1.0);\n"
    "    vNormal = aNormal;\n"
    "    vWorldPos = worldPos;\n"
    "    vColor = aInstColor;\n"
    "}\n";

static const char *VOXEL_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 vNormal;\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vColor;\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uViewPos;\n"
    "void main() {\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float diff2 = max(dot(N, -L), 0.0) * 0.3;\n"
    "    float ambient = 0.15;\n"
    "    vec3 V = normalize(uViewPos - vWorldPos);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.3;\n"
    "    vec3 result = vColor * (ambient + diff + diff2) + vec3(spec);\n"
    "    FragColor = vec4(result, 1.0);\n"
    "}\n";

static GLuint s_voxel_prog = 0;

static GLuint
compile_shader_src(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

static void
ensure_voxel_prog(void)
{
    if (s_voxel_prog) return;
    GLuint vs = compile_shader_src(GL_VERTEX_SHADER, VOXEL_VERT_SRC);
    GLuint fs = compile_shader_src(GL_FRAGMENT_SHADER, VOXEL_FRAG_SRC);
    if (!vs || !fs) return;

    s_voxel_prog = glCreateProgram();
    glAttachShader(s_voxel_prog, vs);
    glAttachShader(s_voxel_prog, fs);
    glLinkProgram(s_voxel_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(s_voxel_prog, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(s_voxel_prog); s_voxel_prog = 0; }
}

void
dc_gl_voxel_buf_draw(const DC_GlVoxelBuf *b,
                       const float *view_proj,
                       const float *light_dir,
                       const float *view_pos,
                       GLuint mesh_prog)
{
    (void)mesh_prog; /* we use our own instanced shader */

    if (!b || !b->uploaded || b->inst_count <= 0) return;

    ensure_voxel_prog();
    if (!s_voxel_prog) return;

    glUseProgram(s_voxel_prog);
    glUniformMatrix4fv(glGetUniformLocation(s_voxel_prog, "uVP"),
                       1, GL_FALSE, view_proj);
    glUniform3fv(glGetUniformLocation(s_voxel_prog, "uLightDir"),
                 1, light_dir);
    glUniform3fv(glGetUniformLocation(s_voxel_prog, "uViewPos"),
                 1, view_pos);

    glBindVertexArray(b->cube_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, CUBE_VERT_COUNT, b->inst_count);
    glBindVertexArray(0);
    glUseProgram(0);
}

/* =========================================================================
 * Queries
 * ========================================================================= */
int
dc_gl_voxel_buf_instance_count(const DC_GlVoxelBuf *b)
{
    return b ? b->inst_count : 0;
}

void
dc_gl_voxel_buf_bounds(const DC_GlVoxelBuf *b, float *min_out, float *max_out)
{
    if (!b) return;
    if (min_out) { min_out[0] = b->bbox_min[0]; min_out[1] = b->bbox_min[1]; min_out[2] = b->bbox_min[2]; }
    if (max_out) { max_out[0] = b->bbox_max[0]; max_out[1] = b->bbox_max[1]; max_out[2] = b->bbox_max[2]; }
}
