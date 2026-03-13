#define _POSIX_C_SOURCE 200809L
#include "gl/gl_viewport.h"
#include "gl/stl_loader.h"
#include "gl/dc_topo.h"
#include "core/log.h"

#include <epoxy/gl.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cairo.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Inline vector/matrix math
 * ========================================================================= */

static inline void vec3_sub(float *out, const float *a, const float *b)
{ out[0]=a[0]-b[0]; out[1]=a[1]-b[1]; out[2]=a[2]-b[2]; }

static inline void vec3_cross(float *out, const float *a, const float *b)
{ out[0]=a[1]*b[2]-a[2]*b[1]; out[1]=a[2]*b[0]-a[0]*b[2]; out[2]=a[0]*b[1]-a[1]*b[0]; }

static inline float vec3_dot(const float *a, const float *b)
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

static inline void vec3_normalize(float *v)
{
    float len = sqrtf(vec3_dot(v,v));
    if (len > 1e-8f) { v[0]/=len; v[1]/=len; v[2]/=len; }
}

/* Column-major 4x4 matrix: m[col*4+row] */
static void mat4_identity(float *m)
{
    memset(m, 0, 16*sizeof(float));
    m[0]=m[5]=m[10]=m[15]=1.0f;
}

static void mat4_mul(float *out, const float *a, const float *b)
{
    float tmp[16];
    for (int c=0;c<4;c++)
        for (int r=0;r<4;r++) {
            tmp[c*4+r] = 0;
            for (int k=0;k<4;k++)
                tmp[c*4+r] += a[k*4+r]*b[c*4+k];
        }
    memcpy(out, tmp, sizeof(tmp));
}

static void mat4_lookat(float *m, const float *eye, const float *center, const float *up)
{
    float f[3], s[3], u[3];
    vec3_sub(f, center, eye);
    vec3_normalize(f);
    vec3_cross(s, f, up);
    vec3_normalize(s);
    vec3_cross(u, s, f);

    mat4_identity(m);
    m[0]=s[0]; m[4]=s[1]; m[8]=s[2];
    m[1]=u[0]; m[5]=u[1]; m[9]=u[2];
    m[2]=-f[0]; m[6]=-f[1]; m[10]=-f[2];
    m[12]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
    m[13]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    m[14]= (f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2]);
}

static void mat4_perspective(float *m, float fov_deg, float aspect, float near, float far)
{
    memset(m, 0, 16*sizeof(float));
    float f = 1.0f / tanf(fov_deg * (float)M_PI / 360.0f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far+near)/(near-far);
    m[11] = -1.0f;
    m[14] = 2.0f*far*near/(near-far);
}

static void mat4_ortho(float *m, float l, float r, float b, float t, float n, float f)
{
    memset(m, 0, 16*sizeof(float));
    m[0]  = 2.0f/(r-l);
    m[5]  = 2.0f/(t-b);
    m[10] = -2.0f/(f-n);
    m[12] = -(r+l)/(r-l);
    m[13] = -(t+b)/(t-b);
    m[14] = -(f+n)/(f-n);
    m[15] = 1.0f;
}

/* =========================================================================
 * Shader sources
 * ========================================================================= */

static const char *MESH_VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec3 aNormal;\n"
    "layout(location=1) in vec3 aPos;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "uniform mat3 uNormalMat;\n"
    "out vec3 vNormal;\n"
    "out vec3 vWorldPos;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "    vNormal = normalize(uNormalMat * aNormal);\n"
    "    vWorldPos = vec3(uModel * vec4(aPos, 1.0));\n"
    "}\n";

static const char *MESH_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 vNormal;\n"
    "in vec3 vWorldPos;\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uViewPos;\n"
    "uniform vec3 uColor;\n"
    "void main() {\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float diff2 = max(dot(N, -L), 0.0) * 0.3;\n"  /* back-face fill */
    "    float ambient = 0.15;\n"
    "    vec3 V = normalize(uViewPos - vWorldPos);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.3;\n"
    "    vec3 result = uColor * (ambient + diff + diff2) + vec3(spec);\n"
    "    FragColor = vec4(result, 1.0);\n"
    "}\n";

/* Flat-color shader for color-ID picking pass */
static const char *PICK_VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec3 aNormal;\n"
    "layout(location=1) in vec3 aPos;\n"
    "uniform mat4 uMVP;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "}\n";

static const char *PICK_FRAG_SRC =
    "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uPickColor;\n"
    "void main() {\n"
    "    FragColor = vec4(uPickColor, 1.0);\n"
    "}\n";

static const char *LINE_VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aColor;\n"
    "uniform mat4 uMVP;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "    vColor = aColor;\n"
    "}\n";

static const char *LINE_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 vColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vec4(vColor, 1.0);\n"
    "}\n";

/* =========================================================================
 * Internal structure
 * ========================================================================= */

struct DC_GlViewport {
    GtkWidget   *gl_area;

    /* Camera */
    float        cam_center[3];   /* orbit center */
    float        cam_dist;        /* distance from center */
    float        cam_theta;       /* azimuth angle (degrees) */
    float        cam_phi;         /* elevation angle (degrees) */
    int          ortho;           /* 0 = perspective, 1 = ortho */

    /* Visibility */
    int          show_grid;
    int          show_axes;

    /* Legacy single mesh (still used for single-STL loads) */
    DC_StlMesh  *mesh;
    GLuint       mesh_vao;
    GLuint       mesh_vbo;
    int          mesh_uploaded;

    /* Multi-object support */
    struct {
        DC_StlMesh *mesh;
        GLuint      vao;
        GLuint      vbo;
        int         uploaded;
        int         line_start;
        int         line_end;
        float       translate[3]; /* viewport-space offset for live preview */
        /* Topology (built lazily on first face/edge pick) */
        DC_Topo    *topo;
        /* Face-group EBO for per-face drawing (built with topology) */
        GLuint      face_ebo;
        int        *face_draw_start; /* [face_count] index offset per group */
        int        *face_draw_count; /* [face_count] vertex count per group */
        int         face_draw_built;
        /* Edge wireframe VBO (built with topology) */
        GLuint      wire_vao;
        GLuint      wire_vbo;
        int         wire_vert_count;
        int         wire_built;
    }            objects[256];
    int          obj_count;
    int          selected_obj;   /* -1 = none */

    /* Selection mode */
    DC_SelectMode sel_mode;     /* DC_SEL_OBJECT / FACE / EDGE */
    int          selected_face; /* face group index in selected obj (-1 = none) */
    int          selected_edge; /* edge index in selected obj (-1 = none) */
    int          show_wireframe;

    /* Color-ID pick framebuffer */
    GLuint       pick_fbo;
    GLuint       pick_rbo_color;
    GLuint       pick_rbo_depth;
    int          pick_w, pick_h; /* current FBO size */

    /* Pick callback */
    DC_GlPickCb  pick_cb;
    void        *pick_cb_data;

    /* Grid */
    GLuint       grid_vao;
    GLuint       grid_vbo;
    int          grid_vert_count;

    /* Axes */
    GLuint       axes_vao;
    GLuint       axes_vbo;

    /* Shaders */
    GLuint       mesh_prog;
    GLuint       line_prog;
    GLuint       pick_prog;
    int          gl_ready;

    /* Drag state */
    double       drag_x, drag_y;
    float        drag_theta, drag_phi;
    float        drag_center[3];    /* cam_center at drag start */
    int          dragging;  /* 0=none, 1=orbit, 2=pan, 3=move object */

    /* Object move state */
    DC_GlMoveCb  move_cb;
    void        *move_cb_data;
    int          move_constraint;   /* 0=free, 1=X, 2=Y, 3=Z */
    int          last_pick_reselect; /* 1 if last click re-selected same obj */

    /* Screenshot capture (set path, render reads pixels after drawing) */
    char        *capture_path;      /* non-NULL = capture on next render */
    int          capture_result;    /* 0=success after capture */

    /* Interaction lock (AI working — block picking/moving) */
    int          locked;
};

/* Forward declarations for lazy topology builders */
static void ensure_topo(DC_GlViewport *vp, int obj_idx);
static void ensure_face_ebo(DC_GlViewport *vp, int obj_idx);
static void ensure_wire_vbo(DC_GlViewport *vp, int obj_idx);

/* =========================================================================
 * Shader compilation helpers
 * ========================================================================= */

static GLuint
compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP, "gl_viewport: shader error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint
link_program(const char *vert_src, const char *frag_src)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP, "gl_viewport: link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* =========================================================================
 * Grid and axes geometry
 * ========================================================================= */

static void
build_grid(DC_GlViewport *vp, float size, float step)
{
    int lines = (int)(size / step) * 2 + 1;
    int vert_count = lines * 2 * 2;  /* 2 directions, 2 verts per line */
    float *data = malloc((size_t)vert_count * 6 * sizeof(float));
    if (!data) return;

    float *p = data;
    float half = size;
    float gray[3] = {0.35f, 0.35f, 0.35f};
    float dark[3] = {0.2f, 0.2f, 0.2f};

    for (float v = -half; v <= half + step * 0.5f; v += step) {
        float *col = (fabsf(v) < step * 0.01f) ? gray : dark;

        /* Line along X at this Z */
        p[0]=-half; p[1]=0; p[2]=v;   p[3]=col[0]; p[4]=col[1]; p[5]=col[2]; p+=6;
        p[0]= half; p[1]=0; p[2]=v;   p[3]=col[0]; p[4]=col[1]; p[5]=col[2]; p+=6;

        /* Line along Z at this X */
        p[0]=v; p[1]=0; p[2]=-half;   p[3]=col[0]; p[4]=col[1]; p[5]=col[2]; p+=6;
        p[0]=v; p[1]=0; p[2]= half;   p[3]=col[0]; p[4]=col[1]; p[5]=col[2]; p+=6;
    }

    int actual = (int)(p - data) / 6;
    vp->grid_vert_count = actual;

    glGenVertexArrays(1, &vp->grid_vao);
    glGenBuffers(1, &vp->grid_vbo);
    glBindVertexArray(vp->grid_vao);
    glBindBuffer(GL_ARRAY_BUFFER, vp->grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(actual * 6 * sizeof(float)), data, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    free(data);
}

static void
build_axes(DC_GlViewport *vp, float len)
{
    /* 3 axes, 2 verts each, pos(3) + color(3) */
    float data[] = {
        /* X axis — red */
        0,0,0,   1,0,0,
        len,0,0, 1,0,0,
        /* Y axis — green */
        0,0,0,   0,1,0,
        0,len,0, 0,1,0,
        /* Z axis — blue */
        0,0,0,   0,0,1,
        0,0,len, 0,0,1,
    };

    glGenVertexArrays(1, &vp->axes_vao);
    glGenBuffers(1, &vp->axes_vbo);
    glBindVertexArray(vp->axes_vao);
    glBindBuffer(GL_ARRAY_BUFFER, vp->axes_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

/* Forward declarations */
static void upload_object(DC_GlViewport *vp, int idx);

/* =========================================================================
 * Mesh upload
 * ========================================================================= */

static void
upload_mesh(DC_GlViewport *vp)
{
    if (!vp->mesh || vp->mesh_uploaded) return;

    if (!vp->mesh_vao) {
        glGenVertexArrays(1, &vp->mesh_vao);
        glGenBuffers(1, &vp->mesh_vbo);
    }

    glBindVertexArray(vp->mesh_vao);
    glBindBuffer(GL_ARRAY_BUFFER, vp->mesh_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vp->mesh->num_vertices * 6 * sizeof(float)),
                 vp->mesh->data, GL_STATIC_DRAW);

    /* layout(location=0) = normal (3 floats), layout(location=1) = position (3 floats) */
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    vp->mesh_uploaded = 1;
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "gl_viewport: uploaded %d triangles", vp->mesh->num_triangles);
}

/* =========================================================================
 * Camera math
 * ========================================================================= */

static void
camera_eye(const DC_GlViewport *vp, float *eye)
{
    float theta = vp->cam_theta * (float)M_PI / 180.0f;
    float phi   = vp->cam_phi   * (float)M_PI / 180.0f;
    eye[0] = vp->cam_center[0] + vp->cam_dist * cosf(phi) * sinf(theta);
    eye[1] = vp->cam_center[1] + vp->cam_dist * sinf(phi);
    eye[2] = vp->cam_center[2] + vp->cam_dist * cosf(phi) * cosf(theta);
}

/* =========================================================================
 * GtkGLArea callbacks
 * ========================================================================= */

static void
on_realize(GtkGLArea *area, gpointer data)
{
    DC_GlViewport *vp = data;
    gtk_gl_area_make_current(area);

    if (gtk_gl_area_get_error(area)) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP, "gl_viewport: GL context error");
        return;
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "gl_viewport: GL %s, GLSL %s",
           glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

    vp->mesh_prog = link_program(MESH_VERT_SRC, MESH_FRAG_SRC);
    vp->line_prog = link_program(LINE_VERT_SRC, LINE_FRAG_SRC);
    vp->pick_prog = link_program(PICK_VERT_SRC, PICK_FRAG_SRC);

    if (!vp->mesh_prog || !vp->line_prog || !vp->pick_prog) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP, "gl_viewport: shader compilation failed");
        return;
    }

    build_grid(vp, 100.0f, 10.0f);
    build_axes(vp, 50.0f);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    vp->gl_ready = 1;
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "gl_viewport: GL initialized");
}

static void
on_unrealize(GtkGLArea *area, gpointer data)
{
    DC_GlViewport *vp = data;
    gtk_gl_area_make_current(area);

    if (vp->mesh_prog) glDeleteProgram(vp->mesh_prog);
    if (vp->line_prog) glDeleteProgram(vp->line_prog);
    if (vp->pick_prog) glDeleteProgram(vp->pick_prog);
    if (vp->pick_fbo) glDeleteFramebuffers(1, &vp->pick_fbo);
    if (vp->pick_rbo_color) glDeleteRenderbuffers(1, &vp->pick_rbo_color);
    if (vp->pick_rbo_depth) glDeleteRenderbuffers(1, &vp->pick_rbo_depth);
    for (int i = 0; i < vp->obj_count; i++) {
        if (vp->objects[i].vao) {
            glDeleteVertexArrays(1, &vp->objects[i].vao);
            glDeleteBuffers(1, &vp->objects[i].vbo);
        }
        if (vp->objects[i].face_ebo)
            glDeleteBuffers(1, &vp->objects[i].face_ebo);
        if (vp->objects[i].wire_vao) {
            glDeleteVertexArrays(1, &vp->objects[i].wire_vao);
            glDeleteBuffers(1, &vp->objects[i].wire_vbo);
        }
    }
    if (vp->mesh_vao) { glDeleteVertexArrays(1, &vp->mesh_vao); glDeleteBuffers(1, &vp->mesh_vbo); }
    if (vp->grid_vao) { glDeleteVertexArrays(1, &vp->grid_vao); glDeleteBuffers(1, &vp->grid_vbo); }
    if (vp->axes_vao) { glDeleteVertexArrays(1, &vp->axes_vao); glDeleteBuffers(1, &vp->axes_vbo); }

    vp->gl_ready = 0;
}

static gboolean
on_render(GtkGLArea *area, GdkGLContext *ctx, gpointer data)
{
    (void)ctx;
    DC_GlViewport *vp = data;
    if (!vp->gl_ready) return TRUE;

    int w = gtk_widget_get_width(GTK_WIDGET(area));
    int h = gtk_widget_get_height(GTK_WIDGET(area));
    if (w < 1 || h < 1) return TRUE;

    /* Scale for HiDPI */
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    glViewport(0, 0, w * scale, h * scale);

    glClearColor(0.18f, 0.18f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Build view and projection matrices */
    float eye[3];
    camera_eye(vp, eye);
    float up[3] = {0, 1, 0};

    float view[16], proj[16], vp_mat[16];
    mat4_lookat(view, eye, vp->cam_center, up);

    float aspect = (float)w / (float)h;
    if (vp->ortho) {
        float half = vp->cam_dist * 0.5f;
        mat4_ortho(proj, -half*aspect, half*aspect, -half, half, 0.1f, vp->cam_dist * 10.0f);
    } else {
        mat4_perspective(proj, 45.0f, aspect, 0.1f, vp->cam_dist * 10.0f);
    }

    mat4_mul(vp_mat, proj, view);

    /* Upload mesh if pending */
    if (vp->mesh && !vp->mesh_uploaded)
        upload_mesh(vp);

    /* Upload any pending multi-objects */
    for (int i = 0; i < vp->obj_count; i++) {
        if (vp->objects[i].mesh && !vp->objects[i].uploaded)
            upload_object(vp, i);
    }

    /* --- Draw grid --- */
    if (vp->show_grid && vp->grid_vao) {
        glUseProgram(vp->line_prog);
        glUniformMatrix4fv(glGetUniformLocation(vp->line_prog, "uMVP"), 1, GL_FALSE, vp_mat);
        glBindVertexArray(vp->grid_vao);
        glDrawArrays(GL_LINES, 0, vp->grid_vert_count);
    }

    /* --- Draw axes --- */
    if (vp->show_axes && vp->axes_vao) {
        glUseProgram(vp->line_prog);
        glUniformMatrix4fv(glGetUniformLocation(vp->line_prog, "uMVP"), 1, GL_FALSE, vp_mat);
        glLineWidth(2.0f);
        glBindVertexArray(vp->axes_vao);
        glDrawArrays(GL_LINES, 0, 6);
        glLineWidth(1.0f);
    }

    /* --- Draw mesh (legacy single mesh or multi-object) --- */
    {
        glUseProgram(vp->mesh_prog);

        GLint loc_mvp   = glGetUniformLocation(vp->mesh_prog, "uMVP");
        GLint loc_model = glGetUniformLocation(vp->mesh_prog, "uModel");
        GLint loc_nmat  = glGetUniformLocation(vp->mesh_prog, "uNormalMat");
        GLint loc_light = glGetUniformLocation(vp->mesh_prog, "uLightDir");
        GLint loc_view  = glGetUniformLocation(vp->mesh_prog, "uViewPos");
        GLint loc_color = glGetUniformLocation(vp->mesh_prog, "uColor");

        float nmat[9] = {1,0,0, 0,1,0, 0,0,1};
        glUniformMatrix3fv(loc_nmat, 1, GL_FALSE, nmat);

        float light_dir[3] = {0.5f, 0.8f, 0.3f};
        vec3_normalize(light_dir);
        glUniform3fv(loc_light, 1, light_dir);
        glUniform3fv(loc_view, 1, eye);

        if (vp->obj_count > 0) {
            /* Multi-object rendering with per-object transforms */
            for (int i = 0; i < vp->obj_count; i++) {
                if (!vp->objects[i].mesh || !vp->objects[i].uploaded)
                    continue;

                /* Per-object model matrix (translate offset for live preview) */
                float obj_model[16];
                mat4_identity(obj_model);
                obj_model[12] = vp->objects[i].translate[0];
                obj_model[13] = vp->objects[i].translate[1];
                obj_model[14] = vp->objects[i].translate[2];

                float obj_mvp[16];
                mat4_mul(obj_mvp, vp_mat, obj_model);

                glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, obj_mvp);
                glUniformMatrix4fv(loc_model, 1, GL_FALSE, obj_model);

                float color[3];
                if (i == vp->selected_obj && vp->sel_mode == DC_SEL_OBJECT) {
                    /* Object mode: gold highlight for selected */
                    color[0] = 1.0f; color[1] = 0.7f; color[2] = 0.2f;
                } else {
                    color[0] = 0.6f; color[1] = 0.75f; color[2] = 0.9f;
                }
                glUniform3fv(loc_color, 1, color);

                glBindVertexArray(vp->objects[i].vao);
                glDrawArrays(GL_TRIANGLES, 0, vp->objects[i].mesh->num_vertices);
            }

            /* --- Face highlight: redraw selected face group in gold --- */
            if (vp->sel_mode == DC_SEL_FACE && vp->selected_obj >= 0
                && vp->selected_face >= 0) {
                int si = vp->selected_obj;
                if (si < vp->obj_count && vp->objects[si].face_draw_built
                    && vp->objects[si].uploaded) {

                    float obj_model2[16];
                    mat4_identity(obj_model2);
                    obj_model2[12] = vp->objects[si].translate[0];
                    obj_model2[13] = vp->objects[si].translate[1];
                    obj_model2[14] = vp->objects[si].translate[2];
                    float obj_mvp2[16];
                    mat4_mul(obj_mvp2, vp_mat, obj_model2);

                    glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, obj_mvp2);
                    glUniformMatrix4fv(loc_model, 1, GL_FALSE, obj_model2);

                    float gold[3] = {1.0f, 0.7f, 0.2f};
                    glUniform3fv(loc_color, 1, gold);

                    glBindVertexArray(vp->objects[si].vao);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                                 vp->objects[si].face_ebo);

                    int fg = vp->selected_face;
                    DC_Topo *t = vp->objects[si].topo;
                    if (t && fg >= 0 && fg < t->face_count) {
                        /* Slight polygon offset so face renders on top */
                        glEnable(GL_POLYGON_OFFSET_FILL);
                        glPolygonOffset(-1.0f, -1.0f);

                        glDrawElements(GL_TRIANGLES,
                                       vp->objects[si].face_draw_count[fg],
                                       GL_UNSIGNED_INT,
                                       (void*)((size_t)vp->objects[si].face_draw_start[fg]
                                               * sizeof(GLuint)));

                        glDisable(GL_POLYGON_OFFSET_FILL);
                    }
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                }
            }

        } else if (vp->mesh && vp->mesh_uploaded && vp->mesh_vao) {
            /* Legacy single mesh */
            float model[16];
            mat4_identity(model);
            float mvp[16];
            mat4_mul(mvp, vp_mat, model);
            glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
            glUniformMatrix4fv(loc_model, 1, GL_FALSE, model);

            float color[3] = {0.6f, 0.75f, 0.9f};
            glUniform3fv(loc_color, 1, color);

            glBindVertexArray(vp->mesh_vao);
            glDrawArrays(GL_TRIANGLES, 0, vp->mesh->num_vertices);
        }
    }

    /* --- Wireframe overlay: render edges as GL_LINES --- */
    if (vp->show_wireframe && vp->obj_count > 0 && vp->line_prog) {
        glUseProgram(vp->line_prog);
        GLint wire_mvp_loc = glGetUniformLocation(vp->line_prog, "uMVP");

        glDepthFunc(GL_LEQUAL);
        glLineWidth(1.0f);

        for (int i = 0; i < vp->obj_count; i++) {
            if (!vp->objects[i].mesh || !vp->objects[i].uploaded)
                continue;

            ensure_wire_vbo(vp, i);
            if (!vp->objects[i].wire_built) continue;

            float obj_model_w[16];
            mat4_identity(obj_model_w);
            obj_model_w[12] = vp->objects[i].translate[0];
            obj_model_w[13] = vp->objects[i].translate[1];
            obj_model_w[14] = vp->objects[i].translate[2];
            float wire_mvp[16];
            mat4_mul(wire_mvp, vp_mat, obj_model_w);

            glUniformMatrix4fv(wire_mvp_loc, 1, GL_FALSE, wire_mvp);
            glBindVertexArray(vp->objects[i].wire_vao);
            glDrawArrays(GL_LINES, 0, vp->objects[i].wire_vert_count);
        }

        glDepthFunc(GL_LESS);
    }

    /* --- Edge highlight: redraw selected edge in cyan --- */
    if (vp->sel_mode == DC_SEL_EDGE && vp->selected_obj >= 0
        && vp->selected_edge >= 0 && vp->line_prog) {
        int si = vp->selected_obj;
        if (si < vp->obj_count && vp->objects[si].topo) {
            DC_Topo *t = vp->objects[si].topo;
            int ei = vp->selected_edge;
            if (ei >= 0 && ei < t->edge_count) {
                glUseProgram(vp->line_prog);
                GLint emvp_loc = glGetUniformLocation(vp->line_prog, "uMVP");

                float obj_model_e[16];
                mat4_identity(obj_model_e);
                obj_model_e[12] = vp->objects[si].translate[0];
                obj_model_e[13] = vp->objects[si].translate[1];
                obj_model_e[14] = vp->objects[si].translate[2];
                float edge_mvp[16];
                mat4_mul(edge_mvp, vp_mat, obj_model_e);
                glUniformMatrix4fv(emvp_loc, 1, GL_FALSE, edge_mvp);

                /* Build a tiny 2-vertex VBO for the highlighted edge */
                float edata[12] = {
                    t->edges[ei].a[0], t->edges[ei].a[1], t->edges[ei].a[2],
                    0.0f, 1.0f, 1.0f,  /* cyan */
                    t->edges[ei].b[0], t->edges[ei].b[1], t->edges[ei].b[2],
                    0.0f, 1.0f, 1.0f   /* cyan */
                };

                GLuint evao, evbo;
                glGenVertexArrays(1, &evao);
                glGenBuffers(1, &evbo);
                glBindVertexArray(evao);
                glBindBuffer(GL_ARRAY_BUFFER, evbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(edata), edata, GL_STREAM_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                      6*sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                      6*sizeof(float), (void*)(3*sizeof(float)));
                glEnableVertexAttribArray(1);

                glDepthFunc(GL_LEQUAL);
                glLineWidth(4.0f);
                glDrawArrays(GL_LINES, 0, 2);
                glLineWidth(1.0f);
                glDepthFunc(GL_LESS);

                glDeleteBuffers(1, &evbo);
                glDeleteVertexArrays(1, &evao);
            }
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);

    /* Capture screenshot if requested */
    if (vp->capture_path) {
        int fw = w * scale, fh = h * scale;
        unsigned char *pixels = malloc((size_t)fw * (size_t)fh * 4);
        vp->capture_result = -1;
        if (pixels) {
            glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

            cairo_surface_t *surf = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, fw, fh);
            unsigned char *dst = cairo_image_surface_get_data(surf);
            int stride = cairo_image_surface_get_stride(surf);

            for (int y = 0; y < fh; y++) {
                unsigned char *src_row = pixels + (size_t)(fh - 1 - y) * (size_t)fw * 4;
                unsigned char *dst_row = dst + (size_t)y * stride;
                for (int x = 0; x < fw; x++) {
                    dst_row[x*4+0] = src_row[x*4+2]; /* B */
                    dst_row[x*4+1] = src_row[x*4+1]; /* G */
                    dst_row[x*4+2] = src_row[x*4+0]; /* R */
                    dst_row[x*4+3] = 255;             /* A */
                }
            }
            cairo_surface_mark_dirty(surf);

            cairo_status_t st = cairo_surface_write_to_png(surf, vp->capture_path);
            cairo_surface_destroy(surf);
            free(pixels);

            if (st == CAIRO_STATUS_SUCCESS) {
                dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                       "gl_viewport: captured %dx%d to %s", fw, fh, vp->capture_path);
                vp->capture_result = 0;
            }
        }
        free(vp->capture_path);
        vp->capture_path = NULL;
    }

    return TRUE;
}

/* =========================================================================
 * Mouse gesture handlers
 * ========================================================================= */

static void
on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
    DC_GlViewport *vp = data;

    /* Grab focus so key events (axis constraints) go to GL, not code editor */
    gtk_widget_grab_focus(vp->gl_area);

    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
    GdkModifierType mods = gdk_event_get_modifier_state(event);
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    vp->drag_x = x;
    vp->drag_y = y;

    if (button == 3 || button == 2 || (mods & GDK_SHIFT_MASK)) {
        /* Pan: right/middle click or shift+click */
        vp->dragging = 2;
        memcpy(vp->drag_center, vp->cam_center, sizeof(vp->drag_center));
    } else if (button == 1 && !vp->locked && vp->last_pick_reselect &&
               vp->selected_obj >= 0 && vp->move_cb) {
        /* Move: left-drag on already-selected object */
        vp->dragging = 3;
        /* Signal move start (phase 0) */
        vp->move_cb(vp->selected_obj, 0, 0.0f, 0.0f, 0.0f, vp->move_cb_data);
    } else {
        /* Orbit: default left-drag */
        vp->dragging = 1;
        vp->drag_theta = vp->cam_theta;
        vp->drag_phi = vp->cam_phi;
    }
}

/* Compute camera right and up vectors for screen-to-world projection */
static void
camera_screen_vectors(const DC_GlViewport *vp,
                      float *rx, float *ry, float *rz,
                      float *ux, float *uy, float *uz)
{
    float theta = vp->cam_theta * (float)M_PI / 180.0f;
    float phi   = vp->cam_phi   * (float)M_PI / 180.0f;

    /* Forward (camera toward center) */
    float fx = cosf(phi) * sinf(theta);
    float fy = sinf(phi);
    float fz = cosf(phi) * cosf(theta);

    /* Right (perpendicular in XZ plane) */
    *rx = cosf(theta);
    *ry = 0.0f;
    *rz = -sinf(theta);

    /* Up = cross(forward, right) */
    *ux = fy * (*rz) - fz * (*ry);
    *uy = fz * (*rx) - fx * (*rz);
    *uz = fx * (*ry) - fy * (*rx);
}

static void
on_drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
    (void)gesture;
    DC_GlViewport *vp = data;

    if (vp->dragging == 1) {
        /* Orbit */
        vp->cam_theta = vp->drag_theta + (float)dx * 0.4f;
        vp->cam_phi   = vp->drag_phi   + (float)dy * 0.4f;
        if (vp->cam_phi > 89.0f) vp->cam_phi = 89.0f;
        if (vp->cam_phi < -89.0f) vp->cam_phi = -89.0f;
    } else if (vp->dragging == 2) {
        /* Pan — move center along camera right/up vectors */
        float r_x, r_y, r_z, u_x, u_y, u_z;
        camera_screen_vectors(vp, &r_x, &r_y, &r_z, &u_x, &u_y, &u_z);
        float scale = vp->cam_dist * 0.001f;

        float mx = (float)dx * scale;
        float my = (float)dy * scale;

        vp->cam_center[0] = vp->drag_center[0] - mx * r_x + my * u_x;
        vp->cam_center[1] = vp->drag_center[1]            + my * u_y;
        vp->cam_center[2] = vp->drag_center[2] - mx * r_z + my * u_z;
    } else if (vp->dragging == 3 && vp->move_cb && vp->selected_obj >= 0) {
        /* Move object — project screen delta to world space */
        float r_x, r_y, r_z, u_x, u_y, u_z;
        camera_screen_vectors(vp, &r_x, &r_y, &r_z, &u_x, &u_y, &u_z);
        float scale = vp->cam_dist * 0.001f;

        float mx = (float)dx * scale;
        float my = (float)dy * scale;

        /* World-space delta (opposite of pan: object follows mouse) */
        float wx =  mx * r_x - my * u_x;
        float wy =           - my * u_y;
        float wz =  mx * r_z - my * u_z;

        /* Apply axis constraint (mapped to SCAD axes via GL↔SCAD coords)
         * SCAD_x=GL_x, SCAD_y=-GL_z, SCAD_z=GL_y
         * X key (c=1): SCAD X only → keep GL_x, zero GL_y+GL_z
         * C key (c=2): SCAD Y only → keep GL_z, zero GL_x+GL_y
         * Z key (c=3): SCAD Z only → keep GL_y, zero GL_x+GL_z */
        int c = vp->move_constraint;
        if (c == 1) { wy = 0; wz = 0; }       /* SCAD X only (GL_x) */
        else if (c == 2) { wx = 0; wy = 0; }   /* SCAD Y only (GL_z) */
        else if (c == 3) { wx = 0; wz = 0; }   /* SCAD Z only (GL_y) */

        vp->move_cb(vp->selected_obj, 1, wx, wy, wz, vp->move_cb_data);
    }

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

static void
on_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
    (void)gesture; (void)dx; (void)dy;
    DC_GlViewport *vp = data;

    /* Fire move end callback (phase 2) using the stored GL translate offset.
     * This is the exact delta the user saw visually — no recomputation needed.
     * Recomputing from screen coords would be wrong if constraints changed
     * mid-drag or if the constraint key was released before the mouse button. */
    if (vp->dragging == 3 && vp->move_cb && vp->selected_obj >= 0) {
        int idx = vp->selected_obj;
        vp->move_cb(idx, 2,
                    vp->objects[idx].translate[0],
                    vp->objects[idx].translate[1],
                    vp->objects[idx].translate[2],
                    vp->move_cb_data);
    }

    vp->dragging = 0;
    vp->last_pick_reselect = 0;
    vp->move_constraint = 0;
}

/* Key handler for axis constraints during object move.
 * Z = constrain Z axis, X = constrain X axis, C = constrain Y axis. */
static gboolean
on_key_pressed_gl(GtkEventControllerKey *ctrl, guint keyval,
                  guint keycode, GdkModifierType mods, gpointer data)
{
    (void)ctrl; (void)keycode; (void)mods;
    DC_GlViewport *vp = data;

    switch (keyval) {
    /* Axis constraints for object move */
    case GDK_KEY_x: case GDK_KEY_X:
        vp->move_constraint = 1;
        return TRUE;
    case GDK_KEY_c: case GDK_KEY_C:
        vp->move_constraint = 2;
        return TRUE;
    case GDK_KEY_z: case GDK_KEY_Z:
        vp->move_constraint = 3;
        return TRUE;
    /* Tab cycles selection mode: Object → Face → Edge → Object */
    case GDK_KEY_Tab:
        dc_gl_viewport_cycle_select_mode(vp);
        return TRUE;
    /* W toggles wireframe overlay */
    case GDK_KEY_w: case GDK_KEY_W:
        dc_gl_viewport_toggle_wireframe(vp);
        return TRUE;
    }
    return FALSE;
}

static void
on_key_released_gl(GtkEventControllerKey *ctrl, guint keyval,
                   guint keycode, GdkModifierType mods, gpointer data)
{
    (void)ctrl; (void)keycode; (void)mods;
    DC_GlViewport *vp = data;

    /* Clear constraint when key is released */
    switch (keyval) {
    case GDK_KEY_x: case GDK_KEY_X:
        if (vp->move_constraint == 1) vp->move_constraint = 0;
        break;
    case GDK_KEY_c: case GDK_KEY_C:
        if (vp->move_constraint == 2) vp->move_constraint = 0;
        break;
    case GDK_KEY_z: case GDK_KEY_Z:
        if (vp->move_constraint == 3) vp->move_constraint = 0;
        break;
    }
}

static gboolean
on_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data)
{
    (void)ctrl; (void)dx;
    DC_GlViewport *vp = data;

    float factor = powf(1.1f, (float)dy);
    vp->cam_dist *= factor;
    if (vp->cam_dist < 0.1f) vp->cam_dist = 0.1f;
    if (vp->cam_dist > 100000.0f) vp->cam_dist = 100000.0f;

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
    return TRUE;
}

/* =========================================================================
 * Color-ID pick pass
 * ========================================================================= */

/* Ensure pick FBO matches viewport size */
static void
ensure_pick_fbo(DC_GlViewport *vp, int w, int h)
{
    if (vp->pick_fbo && vp->pick_w == w && vp->pick_h == h)
        return;

    if (vp->pick_fbo) glDeleteFramebuffers(1, &vp->pick_fbo);
    if (vp->pick_rbo_color) glDeleteRenderbuffers(1, &vp->pick_rbo_color);
    if (vp->pick_rbo_depth) glDeleteRenderbuffers(1, &vp->pick_rbo_depth);

    glGenFramebuffers(1, &vp->pick_fbo);
    glGenRenderbuffers(1, &vp->pick_rbo_color);
    glGenRenderbuffers(1, &vp->pick_rbo_depth);

    glBindFramebuffer(GL_FRAMEBUFFER, vp->pick_fbo);

    glBindRenderbuffer(GL_RENDERBUFFER, vp->pick_rbo_color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, vp->pick_rbo_color);

    glBindRenderbuffer(GL_RENDERBUFFER, vp->pick_rbo_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, vp->pick_rbo_depth);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    vp->pick_w = w;
    vp->pick_h = h;
}

/* Encode object index as RGB color (index 0 = (1,0,0)/255, etc.)
 * Background is black (0,0,0) = no object. */
static void
obj_idx_to_color(int idx, float *rgb)
{
    int id = idx + 1; /* 0 reserved for background */
    rgb[0] = (float)(id & 0xFF) / 255.0f;
    rgb[1] = (float)((id >> 8) & 0xFF) / 255.0f;
    rgb[2] = (float)((id >> 16) & 0xFF) / 255.0f;
}

static int
color_to_obj_idx(unsigned char r, unsigned char g, unsigned char b)
{
    int id = r | (g << 8) | (b << 16);
    return id - 1; /* -1 if background (id=0) */
}

/* Set up pick FBO and build view/proj matrices for picking.
 * Returns 0 on failure. On success, pick FBO is bound and ready. */
static int
pick_setup(DC_GlViewport *vp, int px, int py,
           float *vp_mat, int *out_fpx, int *out_fpy)
{
    if (!vp->gl_ready || vp->obj_count == 0 || !vp->pick_prog)
        return 0;

    int w = gtk_widget_get_width(GTK_WIDGET(vp->gl_area));
    int h = gtk_widget_get_height(GTK_WIDGET(vp->gl_area));
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(vp->gl_area));
    int fw = w * scale, fh = h * scale;

    gtk_gl_area_make_current(GTK_GL_AREA(vp->gl_area));
    ensure_pick_fbo(vp, fw, fh);

    float eye[3];
    camera_eye(vp, eye);
    float up[3] = {0, 1, 0};
    float view[16], proj[16];

    mat4_lookat(view, eye, vp->cam_center, up);
    float aspect = (float)w / (float)h;
    if (vp->ortho) {
        float half = vp->cam_dist * 0.5f;
        mat4_ortho(proj, -half*aspect, half*aspect, -half, half,
                   0.1f, vp->cam_dist * 10.0f);
    } else {
        mat4_perspective(proj, 45.0f, aspect, 0.1f, vp->cam_dist * 10.0f);
    }
    mat4_mul(vp_mat, proj, view);

    glBindFramebuffer(GL_FRAMEBUFFER, vp->pick_fbo);
    glViewport(0, 0, fw, fh);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    *out_fpx = px * scale;
    *out_fpy = fh - (py * scale) - 1;
    return 1;
}

/* Read a single pixel from the bound FBO at (fpx, fpy). */
static void
pick_read_pixel(int fpx, int fpy, unsigned char *pixel)
{
    glReadPixels(fpx, fpy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
}

/* Object-mode pick: one color per object. Returns obj_idx or -1. */
static int
do_pick_object(DC_GlViewport *vp, int px, int py)
{
    float vp_mat[16];
    int fpx, fpy;
    if (!pick_setup(vp, px, py, vp_mat, &fpx, &fpy))
        return -1;

    glUseProgram(vp->pick_prog);
    GLint mvp_loc = glGetUniformLocation(vp->pick_prog, "uMVP");
    GLint col_loc = glGetUniformLocation(vp->pick_prog, "uPickColor");

    for (int i = 0; i < vp->obj_count; i++) {
        if (!vp->objects[i].mesh || !vp->objects[i].uploaded)
            continue;

        float obj_model[16];
        mat4_identity(obj_model);
        obj_model[12] = vp->objects[i].translate[0];
        obj_model[13] = vp->objects[i].translate[1];
        obj_model[14] = vp->objects[i].translate[2];

        float obj_mvp[16];
        mat4_mul(obj_mvp, vp_mat, obj_model);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, obj_mvp);

        float rgb[3];
        obj_idx_to_color(i, rgb);
        glUniform3fv(col_loc, 1, rgb);

        glBindVertexArray(vp->objects[i].vao);
        glDrawArrays(GL_TRIANGLES, 0, vp->objects[i].mesh->num_vertices);
    }

    unsigned char pixel[4] = {0};
    pick_read_pixel(fpx, fpy, pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int result = color_to_obj_idx(pixel[0], pixel[1], pixel[2]);
    if (result < 0 || result >= vp->obj_count) result = -1;
    return result;
}

/* Face-mode pick: one color per face group. Sets vp->selected_obj and
 * vp->selected_face. Returns picked obj_idx or -1. */
static int
do_pick_face(DC_GlViewport *vp, int px, int py)
{
    float vp_mat[16];
    int fpx, fpy;
    if (!pick_setup(vp, px, py, vp_mat, &fpx, &fpy))
        return -1;

    glUseProgram(vp->pick_prog);
    GLint mvp_loc = glGetUniformLocation(vp->pick_prog, "uMVP");
    GLint col_loc = glGetUniformLocation(vp->pick_prog, "uPickColor");

    for (int i = 0; i < vp->obj_count; i++) {
        if (!vp->objects[i].mesh || !vp->objects[i].uploaded)
            continue;

        ensure_face_ebo(vp, i);
        if (!vp->objects[i].face_draw_built) continue;
        DC_Topo *t = vp->objects[i].topo;
        if (!t) continue;

        float obj_model[16];
        mat4_identity(obj_model);
        obj_model[12] = vp->objects[i].translate[0];
        obj_model[13] = vp->objects[i].translate[1];
        obj_model[14] = vp->objects[i].translate[2];

        float obj_mvp[16];
        mat4_mul(obj_mvp, vp_mat, obj_model);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, obj_mvp);

        glBindVertexArray(vp->objects[i].vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vp->objects[i].face_ebo);

        /* Draw each face group with a unique pick color */
        for (int g = 0; g < t->face_count; g++) {
            float rgb[3];
            dc_topo_sub_to_color(i, g, rgb);
            glUniform3fv(col_loc, 1, rgb);

            glDrawElements(GL_TRIANGLES,
                           vp->objects[i].face_draw_count[g],
                           GL_UNSIGNED_INT,
                           (void*)((size_t)vp->objects[i].face_draw_start[g]
                                   * sizeof(GLuint)));
        }
    }

    unsigned char pixel[4] = {0};
    pick_read_pixel(fpx, fpy, pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int obj_idx, face_idx;
    dc_topo_color_to_sub(pixel[0], pixel[1], pixel[2], &obj_idx, &face_idx);

    if (obj_idx < 0 || obj_idx >= vp->obj_count) {
        vp->selected_face = -1;
        return -1;
    }

    DC_Topo *t = vp->objects[obj_idx].topo;
    if (!t || face_idx < 0 || face_idx >= t->face_count) {
        vp->selected_face = -1;
        return obj_idx;
    }

    vp->selected_face = face_idx;
    return obj_idx;
}

/* Edge-mode pick: render edges as thick GL_LINES with unique colors.
 * Sets vp->selected_obj and vp->selected_edge. Returns obj_idx or -1. */
static int
do_pick_edge(DC_GlViewport *vp, int px, int py)
{
    float vp_mat[16];
    int fpx, fpy;
    if (!pick_setup(vp, px, py, vp_mat, &fpx, &fpy))
        return -1;

    /* First, render solid faces (so edges on back faces are occluded) */
    glUseProgram(vp->pick_prog);
    GLint mvp_loc = glGetUniformLocation(vp->pick_prog, "uMVP");
    GLint col_loc = glGetUniformLocation(vp->pick_prog, "uPickColor");

    for (int i = 0; i < vp->obj_count; i++) {
        if (!vp->objects[i].mesh || !vp->objects[i].uploaded)
            continue;

        float obj_model[16];
        mat4_identity(obj_model);
        obj_model[12] = vp->objects[i].translate[0];
        obj_model[13] = vp->objects[i].translate[1];
        obj_model[14] = vp->objects[i].translate[2];

        float obj_mvp[16];
        mat4_mul(obj_mvp, vp_mat, obj_model);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, obj_mvp);

        /* Draw solid geometry as background (pick color 0 = background) */
        float bg[3] = {0, 0, 0};
        glUniform3fv(col_loc, 1, bg);
        glBindVertexArray(vp->objects[i].vao);
        glDrawArrays(GL_TRIANGLES, 0, vp->objects[i].mesh->num_vertices);
    }

    /* Now render edges on top with per-edge pick colors.
     * Use the line shader's VAO but the pick program. We need a temporary
     * VBO with per-edge pick colors, or we can reuse the pick shader with
     * a uniform per edge. Since edges are few (~100s), uniform-per-edge is fine. */
    glLineWidth(5.0f);
    glDepthFunc(GL_LEQUAL); /* edges draw on top of coplanar faces */

    /* We render edges manually: 2 vertices per edge, using the pick shader.
     * The pick shader ignores aNormal (location 0), uses aPos (location 1).
     * But our line VBO has aPos at location 0 — so we need to use the
     * line program VAO layout. Actually, the pick shader has aNormal at 0
     * and aPos at 1 (matching the mesh VBO). For line-based rendering we
     * need aPos at location 0. We'll use the line_prog shader with a
     * uniform-color trick — or better, build a temporary VBO.
     *
     * Simplest approach: build a temporary VBO per object with edge
     * positions, draw each edge (2 verts) individually with pick uniform. */

    /* Use a temporary VAO/VBO for edge pick rendering */
    GLuint tmp_vao, tmp_vbo;
    glGenVertexArrays(1, &tmp_vao);
    glGenBuffers(1, &tmp_vbo);
    glBindVertexArray(tmp_vao);
    glBindBuffer(GL_ARRAY_BUFFER, tmp_vbo);

    /* Pick shader layout: location 0 = aNormal (3f), location 1 = aPos (3f)
     * We'll pack [0,0,0, px,py,pz] per vertex so aPos reads correctly. */

    for (int i = 0; i < vp->obj_count; i++) {
        if (!vp->objects[i].mesh || !vp->objects[i].uploaded)
            continue;

        ensure_wire_vbo(vp, i);
        DC_Topo *t = vp->objects[i].topo;
        if (!t || t->edge_count <= 0) continue;

        float obj_model[16];
        mat4_identity(obj_model);
        obj_model[12] = vp->objects[i].translate[0];
        obj_model[13] = vp->objects[i].translate[1];
        obj_model[14] = vp->objects[i].translate[2];

        float obj_mvp[16];
        mat4_mul(obj_mvp, vp_mat, obj_model);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, obj_mvp);

        /* Build edge pick data: [0,0,0, ax,ay,az, 0,0,0, bx,by,bz] per edge
         * This maps to pick shader: aNormal=(0,0,0), aPos=(position). */
        int nfloats = t->edge_count * 2 * 6;
        float *edata = (float *)malloc((size_t)nfloats * sizeof(float));
        if (!edata) continue;

        for (int e = 0; e < t->edge_count; e++) {
            float *p = edata + e * 12;
            /* Vertex A: aNormal=0, aPos=edge.a */
            p[0]=0; p[1]=0; p[2]=0;
            p[3]=t->edges[e].a[0]; p[4]=t->edges[e].a[1]; p[5]=t->edges[e].a[2];
            /* Vertex B: aNormal=0, aPos=edge.b */
            p[6]=0; p[7]=0; p[8]=0;
            p[9]=t->edges[e].b[0]; p[10]=t->edges[e].b[1]; p[11]=t->edges[e].b[2];
        }

        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)((size_t)nfloats * sizeof(float)),
                     edata, GL_STREAM_DRAW);

        /* Set up vertex attributes matching pick shader */
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float),
                              (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);

        /* Draw each edge with unique pick color */
        for (int e = 0; e < t->edge_count; e++) {
            float rgb[3];
            dc_topo_sub_to_color(i, e, rgb);
            glUniform3fv(col_loc, 1, rgb);
            glDrawArrays(GL_LINES, e * 2, 2);
        }

        free(edata);
    }

    glDeleteBuffers(1, &tmp_vbo);
    glDeleteVertexArrays(1, &tmp_vao);

    glLineWidth(1.0f);
    glDepthFunc(GL_LESS);

    unsigned char pixel[4] = {0};
    pick_read_pixel(fpx, fpy, pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int obj_idx, edge_idx;
    dc_topo_color_to_sub(pixel[0], pixel[1], pixel[2], &obj_idx, &edge_idx);

    if (obj_idx < 0 || obj_idx >= vp->obj_count) {
        vp->selected_edge = -1;
        return -1;
    }

    DC_Topo *t = vp->objects[obj_idx].topo;
    if (!t || edge_idx < 0 || edge_idx >= t->edge_count) {
        vp->selected_edge = -1;
        return obj_idx;
    }

    vp->selected_edge = edge_idx;
    return obj_idx;
}

/* Unified pick dispatch — calls appropriate mode-specific picker. */
static int
do_pick(DC_GlViewport *vp, int px, int py)
{
    switch (vp->sel_mode) {
    case DC_SEL_FACE:
        return do_pick_face(vp, px, py);
    case DC_SEL_EDGE:
        return do_pick_edge(vp, px, py);
    case DC_SEL_OBJECT:
    default:
        return do_pick_object(vp, px, py);
    }
}

/* Upload a single object mesh to GL */
static void
upload_object(DC_GlViewport *vp, int idx)
{
    if (idx < 0 || idx >= vp->obj_count) return;
    if (!vp->objects[idx].mesh || vp->objects[idx].uploaded) return;

    if (!vp->objects[idx].vao) {
        glGenVertexArrays(1, &vp->objects[idx].vao);
        glGenBuffers(1, &vp->objects[idx].vbo);
    }

    DC_StlMesh *m = vp->objects[idx].mesh;
    glBindVertexArray(vp->objects[idx].vao);
    glBindBuffer(GL_ARRAY_BUFFER, vp->objects[idx].vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m->num_vertices * 6 * sizeof(float)),
                 m->data, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float),
                          (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    vp->objects[idx].uploaded = 1;
}

/* Click handler — single click (not drag) triggers pick */
static void
on_click_pressed(GtkGestureClick *gesture, int n_press,
                 double x, double y, gpointer data)
{
    (void)gesture; (void)n_press;
    DC_GlViewport *vp = data;

    /* Always grab focus so axis constraint keys go here, not code editor */
    gtk_widget_grab_focus(vp->gl_area);

    if (vp->locked || vp->obj_count == 0) return;

    int old_sel = vp->selected_obj;
    int obj = do_pick(vp, (int)x, (int)y);

    /* Track if clicking on already-selected object (for move mode) */
    vp->last_pick_reselect = (obj >= 0 && obj == old_sel) ? 1 : 0;
    vp->selected_obj = obj;

    /* Clear sub-element selection when object changes */
    if (obj != old_sel) {
        vp->selected_face = -1;
        vp->selected_edge = -1;
    }

    /* Notify callback */
    if (vp->pick_cb) {
        if (obj >= 0 && obj < vp->obj_count) {
            vp->pick_cb(obj, vp->objects[obj].line_start,
                        vp->objects[obj].line_end, vp->pick_cb_data);
        } else {
            vp->pick_cb(-1, 0, 0, vp->pick_cb_data);
        }
    }

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

/* =========================================================================
 * Public API
 * ========================================================================= */

DC_GlViewport *
dc_gl_viewport_new(void)
{
    DC_GlViewport *vp = calloc(1, sizeof(*vp));
    if (!vp) return NULL;

    /* Default camera */
    vp->cam_dist = 200.0f;
    vp->cam_theta = 45.0f;
    vp->cam_phi = 30.0f;
    vp->show_grid = 1;
    vp->show_axes = 1;
    vp->selected_obj = -1;
    vp->sel_mode = DC_SEL_OBJECT;
    vp->selected_face = -1;
    vp->selected_edge = -1;

    /* Create GtkGLArea */
    vp->gl_area = gtk_gl_area_new();
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(vp->gl_area), TRUE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(vp->gl_area), FALSE);
    gtk_widget_set_hexpand(vp->gl_area, TRUE);
    gtk_widget_set_vexpand(vp->gl_area, TRUE);
    gtk_widget_set_focusable(vp->gl_area, TRUE);

    g_signal_connect(vp->gl_area, "realize",   G_CALLBACK(on_realize),   vp);
    g_signal_connect(vp->gl_area, "unrealize", G_CALLBACK(on_unrealize), vp);
    g_signal_connect(vp->gl_area, "render",    G_CALLBACK(on_render),    vp);

    /* Mouse gestures */
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 0);
    g_signal_connect(drag, "drag-begin",  G_CALLBACK(on_drag_begin),  vp);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), vp);
    g_signal_connect(drag, "drag-end",    G_CALLBACK(on_drag_end),    vp);
    gtk_widget_add_controller(vp->gl_area, GTK_EVENT_CONTROLLER(drag));

    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), vp);
    gtk_widget_add_controller(vp->gl_area, scroll);

    /* Click gesture for object picking (single click, button 1) */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click_pressed), vp);
    gtk_widget_add_controller(vp->gl_area, GTK_EVENT_CONTROLLER(click));

    /* Key controller for axis constraints (Z/X/C) */
    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_key_pressed_gl), vp);
    g_signal_connect(key_ctrl, "key-released",
                     G_CALLBACK(on_key_released_gl), vp);
    gtk_widget_add_controller(vp->gl_area, key_ctrl);

    /* Queue an initial render once mapped */
    g_signal_connect_swapped(vp->gl_area, "map",
        G_CALLBACK(gtk_gl_area_queue_render), vp->gl_area);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "gl_viewport created");
    return vp;
}

void
dc_gl_viewport_free(DC_GlViewport *vp)
{
    if (!vp) return;
    dc_stl_free(vp->mesh);
    for (int i = 0; i < vp->obj_count; i++) {
        dc_stl_free(vp->objects[i].mesh);
        dc_topo_free(vp->objects[i].topo);
        free(vp->objects[i].face_draw_start);
        free(vp->objects[i].face_draw_count);
    }
    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "gl_viewport freed");
    free(vp);
}

GtkWidget *
dc_gl_viewport_widget(DC_GlViewport *vp)
{
    return vp ? vp->gl_area : NULL;
}

int
dc_gl_viewport_load_stl(DC_GlViewport *vp, const char *stl_path)
{
    if (!vp || !stl_path) return -1;

    DC_StlMesh *mesh = dc_stl_load(stl_path);
    if (!mesh) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "gl_viewport: failed to load %s", stl_path);
        return -1;
    }

    dc_stl_free(vp->mesh);
    vp->mesh = mesh;
    vp->mesh_uploaded = 0;

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "gl_viewport: loaded %s (%d tris, extent=%.1f)",
           stl_path, mesh->num_triangles, mesh->extent);

    /* Auto-fit camera */
    dc_gl_viewport_reset_camera(vp);

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
    return 0;
}

void
dc_gl_viewport_clear_mesh(DC_GlViewport *vp)
{
    if (!vp) return;
    dc_stl_free(vp->mesh);
    vp->mesh = NULL;
    vp->mesh_uploaded = 0;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

void
dc_gl_viewport_reset_camera(DC_GlViewport *vp)
{
    if (!vp) return;

    if (vp->mesh) {
        vp->cam_center[0] = vp->mesh->center[0];
        vp->cam_center[1] = vp->mesh->center[1];
        vp->cam_center[2] = vp->mesh->center[2];
        vp->cam_dist = vp->mesh->extent * 2.0f;
    } else {
        vp->cam_center[0] = vp->cam_center[1] = vp->cam_center[2] = 0;
        vp->cam_dist = 200.0f;
    }

    vp->cam_theta = 45.0f;
    vp->cam_phi = 30.0f;

    /* Rebuild grid to match model scale */
    if (vp->gl_ready) {
        gtk_gl_area_make_current(GTK_GL_AREA(vp->gl_area));
        if (vp->grid_vao) {
            glDeleteVertexArrays(1, &vp->grid_vao);
            glDeleteBuffers(1, &vp->grid_vbo);
            vp->grid_vao = vp->grid_vbo = 0;
        }
        float grid_size = vp->cam_dist * 2.0f;
        float grid_step = grid_size / 20.0f;
        build_grid(vp, grid_size, grid_step);

        if (vp->axes_vao) {
            glDeleteVertexArrays(1, &vp->axes_vao);
            glDeleteBuffers(1, &vp->axes_vbo);
            vp->axes_vao = vp->axes_vbo = 0;
        }
        build_axes(vp, grid_size * 0.5f);
    }

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

void
dc_gl_viewport_toggle_ortho(DC_GlViewport *vp)
{
    if (!vp) return;
    vp->ortho = !vp->ortho;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

void
dc_gl_viewport_toggle_grid(DC_GlViewport *vp)
{
    if (!vp) return;
    vp->show_grid = !vp->show_grid;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

void
dc_gl_viewport_toggle_axes(DC_GlViewport *vp)
{
    if (!vp) return;
    vp->show_axes = !vp->show_axes;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

/* =========================================================================
 * Multi-object API
 * ========================================================================= */

void
dc_gl_viewport_clear_objects(DC_GlViewport *vp)
{
    if (!vp) return;

    if (vp->gl_ready) {
        gtk_gl_area_make_current(GTK_GL_AREA(vp->gl_area));
        for (int i = 0; i < vp->obj_count; i++) {
            if (vp->objects[i].vao) {
                glDeleteVertexArrays(1, &vp->objects[i].vao);
                glDeleteBuffers(1, &vp->objects[i].vbo);
            }
            if (vp->objects[i].face_ebo)
                glDeleteBuffers(1, &vp->objects[i].face_ebo);
            if (vp->objects[i].wire_vao) {
                glDeleteVertexArrays(1, &vp->objects[i].wire_vao);
                glDeleteBuffers(1, &vp->objects[i].wire_vbo);
            }
        }
    }

    for (int i = 0; i < vp->obj_count; i++) {
        dc_stl_free(vp->objects[i].mesh);
        dc_topo_free(vp->objects[i].topo);
        free(vp->objects[i].face_draw_start);
        free(vp->objects[i].face_draw_count);
    }

    memset(vp->objects, 0, sizeof(vp->objects));
    vp->obj_count = 0;
    vp->selected_obj = -1;
    vp->selected_face = -1;
    vp->selected_edge = -1;

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

int
dc_gl_viewport_add_object(DC_GlViewport *vp, const char *stl_path,
                           int line_start, int line_end)
{
    if (!vp || !stl_path || vp->obj_count >= 256) return -1;

    DC_StlMesh *mesh = dc_stl_load(stl_path);
    if (!mesh) {
        dc_log(DC_LOG_WARN, DC_LOG_EVENT_APP,
               "gl_viewport: failed to load object STL %s", stl_path);
        return -1;
    }

    int idx = vp->obj_count++;
    vp->objects[idx].mesh = mesh;
    vp->objects[idx].vao = 0;
    vp->objects[idx].vbo = 0;
    vp->objects[idx].uploaded = 0;
    vp->objects[idx].line_start = line_start;
    vp->objects[idx].line_end = line_end;

    /* Upload immediately if GL is ready */
    if (vp->gl_ready) {
        gtk_gl_area_make_current(GTK_GL_AREA(vp->gl_area));
        upload_object(vp, idx);
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "gl_viewport: added object %d (%d tris, lines %d-%d)",
           idx, mesh->num_triangles, line_start, line_end);

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
    return idx;
}

int
dc_gl_viewport_get_selected(DC_GlViewport *vp)
{
    return vp ? vp->selected_obj : -1;
}

int
dc_gl_viewport_get_object_lines(DC_GlViewport *vp, int obj_idx,
                                 int *line_start, int *line_end)
{
    if (!vp || obj_idx < 0 || obj_idx >= vp->obj_count) return -1;
    if (line_start) *line_start = vp->objects[obj_idx].line_start;
    if (line_end) *line_end = vp->objects[obj_idx].line_end;
    return 0;
}

void
dc_gl_viewport_set_pick_callback(DC_GlViewport *vp,
                                  DC_GlPickCb cb, void *userdata)
{
    if (!vp) return;
    vp->pick_cb = cb;
    vp->pick_cb_data = userdata;
}

void
dc_gl_viewport_select_object(DC_GlViewport *vp, int obj_idx)
{
    if (!vp) return;
    if (obj_idx >= vp->obj_count) return;

    vp->selected_obj = obj_idx;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));

    if (vp->pick_cb) {
        if (obj_idx >= 0 && obj_idx < vp->obj_count) {
            vp->pick_cb(obj_idx, vp->objects[obj_idx].line_start,
                        vp->objects[obj_idx].line_end, vp->pick_cb_data);
        } else {
            vp->pick_cb(-1, 0, 0, vp->pick_cb_data);
        }
    }
}

int
dc_gl_viewport_get_object_count(DC_GlViewport *vp)
{
    return vp ? vp->obj_count : 0;
}

void
dc_gl_viewport_get_camera_center(DC_GlViewport *vp, float *x, float *y, float *z)
{
    if (!vp) return;
    if (x) *x = vp->cam_center[0];
    if (y) *y = vp->cam_center[1];
    if (z) *z = vp->cam_center[2];
}

void
dc_gl_viewport_set_camera_center(DC_GlViewport *vp, float x, float y, float z)
{
    if (!vp) return;
    vp->cam_center[0] = x;
    vp->cam_center[1] = y;
    vp->cam_center[2] = z;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

float
dc_gl_viewport_get_camera_dist(DC_GlViewport *vp)
{
    return vp ? vp->cam_dist : 0.0f;
}

void
dc_gl_viewport_set_camera_dist(DC_GlViewport *vp, float dist)
{
    if (!vp) return;
    vp->cam_dist = dist;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

void
dc_gl_viewport_get_camera_angles(DC_GlViewport *vp, float *theta, float *phi)
{
    if (!vp) return;
    if (theta) *theta = vp->cam_theta;
    if (phi)   *phi   = vp->cam_phi;
}

void
dc_gl_viewport_set_camera_angles(DC_GlViewport *vp, float theta, float phi)
{
    if (!vp) return;
    vp->cam_theta = theta;
    vp->cam_phi   = phi;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

int
dc_gl_viewport_get_ortho(DC_GlViewport *vp)
{
    return vp ? vp->ortho : 0;
}

int
dc_gl_viewport_get_grid(DC_GlViewport *vp)
{
    return vp ? vp->show_grid : 0;
}

int
dc_gl_viewport_get_axes(DC_GlViewport *vp)
{
    return vp ? vp->show_axes : 0;
}

void
dc_gl_viewport_set_move_callback(DC_GlViewport *vp,
                                  DC_GlMoveCb cb, void *userdata)
{
    if (!vp) return;
    vp->move_cb = cb;
    vp->move_cb_data = userdata;
}

void
dc_gl_viewport_set_object_translate(DC_GlViewport *vp, int obj_idx,
                                     float x, float y, float z)
{
    if (!vp || obj_idx < 0 || obj_idx >= vp->obj_count) return;
    vp->objects[obj_idx].translate[0] = x;
    vp->objects[obj_idx].translate[1] = y;
    vp->objects[obj_idx].translate[2] = z;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

void
dc_gl_viewport_fit_all_objects(DC_GlViewport *vp)
{
    if (!vp || vp->obj_count == 0) return;

    float bmin[3] = { 1e30f,  1e30f,  1e30f};
    float bmax[3] = {-1e30f, -1e30f, -1e30f};

    for (int i = 0; i < vp->obj_count; i++) {
        DC_StlMesh *m = vp->objects[i].mesh;
        if (!m) continue;
        for (int j = 0; j < 3; j++) {
            if (m->min[j] < bmin[j]) bmin[j] = m->min[j];
            if (m->max[j] > bmax[j]) bmax[j] = m->max[j];
        }
    }

    vp->cam_center[0] = (bmin[0] + bmax[0]) * 0.5f;
    vp->cam_center[1] = (bmin[1] + bmax[1]) * 0.5f;
    vp->cam_center[2] = (bmin[2] + bmax[2]) * 0.5f;

    float dx = bmax[0] - bmin[0];
    float dy = bmax[1] - bmin[1];
    float dz = bmax[2] - bmin[2];
    float extent = dx > dy ? dx : dy;
    if (dz > extent) extent = dz;
    vp->cam_dist = extent * 2.0f;
    if (vp->cam_dist < 1.0f) vp->cam_dist = 1.0f;

    /* Rebuild grid and axes to match new scale */
    if (vp->gl_ready) {
        gtk_gl_area_make_current(GTK_GL_AREA(vp->gl_area));
        if (vp->grid_vao) {
            glDeleteVertexArrays(1, &vp->grid_vao);
            glDeleteBuffers(1, &vp->grid_vbo);
            vp->grid_vao = vp->grid_vbo = 0;
        }
        float grid_size = vp->cam_dist * 2.0f;
        float grid_step = grid_size / 20.0f;
        build_grid(vp, grid_size, grid_step);

        if (vp->axes_vao) {
            glDeleteVertexArrays(1, &vp->axes_vao);
            glDeleteBuffers(1, &vp->axes_vbo);
            vp->axes_vao = vp->axes_vbo = 0;
        }
        build_axes(vp, grid_size * 0.5f);
    }

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

int
dc_gl_viewport_capture_png(DC_GlViewport *vp, const char *path)
{
    if (!vp || !vp->gl_ready || !path) return -1;

    /* Set capture flag — on_render will do the actual pixel readback */
    free(vp->capture_path);
    vp->capture_path = strdup(path);
    vp->capture_result = -1;

    /* Queue a render and process the GTK event loop until it fires */
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));

    /* Spin the main loop until capture completes (capture_path set to NULL) */
    int spins = 0;
    while (vp->capture_path && spins < 200) {
        g_main_context_iteration(NULL, TRUE);
        spins++;
    }

    return vp->capture_result;
}

void
dc_gl_viewport_select_object_quiet(DC_GlViewport *vp, int obj_idx)
{
    if (!vp) return;
    if (obj_idx >= vp->obj_count) return;
    vp->selected_obj = obj_idx;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

void
dc_gl_viewport_set_locked(DC_GlViewport *vp, int locked)
{
    if (vp) vp->locked = locked;
}

int
dc_gl_viewport_get_locked(DC_GlViewport *vp)
{
    return vp ? vp->locked : 0;
}

/* =========================================================================
 * Selection mode + topology
 * ========================================================================= */

/* Build topology for an object if not already built. */
static void
ensure_topo(DC_GlViewport *vp, int obj_idx)
{
    if (obj_idx < 0 || obj_idx >= vp->obj_count) return;
    if (vp->objects[obj_idx].topo) return;
    DC_StlMesh *m = vp->objects[obj_idx].mesh;
    if (!m || m->num_triangles <= 0) return;
    vp->objects[obj_idx].topo = dc_topo_build(m->data, m->num_triangles);
}

/* Build face-group EBO for per-face drawing (for pick pass and highlight).
 * Creates an element buffer with vertex indices sorted by face group,
 * plus face_draw_start/count arrays for per-group glDrawElements calls. */
static void
ensure_face_ebo(DC_GlViewport *vp, int obj_idx)
{
    if (obj_idx < 0 || obj_idx >= vp->obj_count) return;
    if (vp->objects[obj_idx].face_draw_built) return;

    ensure_topo(vp, obj_idx);
    DC_Topo *t = vp->objects[obj_idx].topo;
    if (!t) return;

    /* Build index array: vertex indices sorted by face group */
    int total_indices = t->num_triangles * 3;
    GLuint *indices = (GLuint *)malloc((size_t)total_indices * sizeof(GLuint));
    int *starts = (int *)malloc((size_t)t->face_count * sizeof(int));
    int *counts = (int *)malloc((size_t)t->face_count * sizeof(int));
    if (!indices || !starts || !counts) {
        free(indices); free(starts); free(counts);
        return;
    }

    int idx = 0;
    for (int g = 0; g < t->face_count; g++) {
        starts[g] = idx;
        DC_FaceGroup *fg = &t->face_groups[g];
        for (int i = 0; i < fg->tri_count; i++) {
            int tri = fg->tri_indices[i];
            indices[idx++] = (GLuint)(tri * 3);
            indices[idx++] = (GLuint)(tri * 3 + 1);
            indices[idx++] = (GLuint)(tri * 3 + 2);
        }
        counts[g] = fg->tri_count * 3;
    }

    /* Upload EBO */
    if (!vp->objects[obj_idx].face_ebo)
        glGenBuffers(1, &vp->objects[obj_idx].face_ebo);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vp->objects[obj_idx].face_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(total_indices * (int)sizeof(GLuint)),
                 indices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    free(indices);

    /* Store draw params */
    free(vp->objects[obj_idx].face_draw_start);
    free(vp->objects[obj_idx].face_draw_count);
    vp->objects[obj_idx].face_draw_start = starts;
    vp->objects[obj_idx].face_draw_count = counts;
    vp->objects[obj_idx].face_draw_built = 1;
}

/* Build wireframe VBO for edge overlay rendering.
 * Creates a GL_LINES VBO with [x,y,z, r,g,b] per vertex. */
static void
ensure_wire_vbo(DC_GlViewport *vp, int obj_idx)
{
    if (obj_idx < 0 || obj_idx >= vp->obj_count) return;
    if (vp->objects[obj_idx].wire_built) return;

    ensure_topo(vp, obj_idx);
    DC_Topo *t = vp->objects[obj_idx].topo;
    if (!t || t->edge_count <= 0) return;

    /* 2 verts per edge * 6 floats per vert */
    int vert_count = t->edge_count * 2;
    float *data = (float *)malloc((size_t)vert_count * 6 * sizeof(float));
    if (!data) return;

    float *p = data;
    for (int i = 0; i < t->edge_count; i++) {
        DC_Edge *e = &t->edges[i];
        /* Vertex A: pos + color */
        p[0] = e->a[0]; p[1] = e->a[1]; p[2] = e->a[2];
        p[3] = 0.05f;   p[4] = 0.05f;   p[5] = 0.05f; /* dark gray */
        p += 6;
        /* Vertex B: pos + color */
        p[0] = e->b[0]; p[1] = e->b[1]; p[2] = e->b[2];
        p[3] = 0.05f;   p[4] = 0.05f;   p[5] = 0.05f;
        p += 6;
    }

    /* Upload to GL */
    if (!vp->objects[obj_idx].wire_vao) {
        glGenVertexArrays(1, &vp->objects[obj_idx].wire_vao);
        glGenBuffers(1, &vp->objects[obj_idx].wire_vbo);
    }

    glBindVertexArray(vp->objects[obj_idx].wire_vao);
    glBindBuffer(GL_ARRAY_BUFFER, vp->objects[obj_idx].wire_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)vert_count * 6 * sizeof(float)),
                 data, GL_STATIC_DRAW);

    /* Position: location 0 */
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    /* Color: location 1 */
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float),
                          (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    free(data);

    vp->objects[obj_idx].wire_vert_count = vert_count;
    vp->objects[obj_idx].wire_built = 1;
}

DC_SelectMode
dc_gl_viewport_get_select_mode(DC_GlViewport *vp)
{
    return vp ? vp->sel_mode : DC_SEL_OBJECT;
}

void
dc_gl_viewport_set_select_mode(DC_GlViewport *vp, DC_SelectMode mode)
{
    if (!vp) return;
    if (mode < DC_SEL_OBJECT || mode > DC_SEL_EDGE) return;
    vp->sel_mode = mode;
    vp->selected_face = -1;
    vp->selected_edge = -1;

    /* Build topology + GL resources for all objects if entering face/edge mode */
    if (mode != DC_SEL_OBJECT) {
        if (vp->gl_ready)
            gtk_gl_area_make_current(GTK_GL_AREA(vp->gl_area));
        for (int i = 0; i < vp->obj_count; i++) {
            ensure_topo(vp, i);
            if (vp->gl_ready) {
                ensure_face_ebo(vp, i);
                ensure_wire_vbo(vp, i);
            }
        }
    }

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

void
dc_gl_viewport_cycle_select_mode(DC_GlViewport *vp)
{
    if (!vp) return;
    DC_SelectMode next = (DC_SelectMode)((vp->sel_mode + 1) % 3);
    dc_gl_viewport_set_select_mode(vp, next);
    static const char *names[] = {"Object", "Face", "Edge"};
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "select mode: %s", names[next]);
}

int
dc_gl_viewport_get_selected_face(DC_GlViewport *vp)
{
    return vp ? vp->selected_face : -1;
}

int
dc_gl_viewport_get_selected_edge(DC_GlViewport *vp)
{
    return vp ? vp->selected_edge : -1;
}

int
dc_gl_viewport_get_face_count(DC_GlViewport *vp, int obj_idx)
{
    if (!vp || obj_idx < 0 || obj_idx >= vp->obj_count) return 0;
    ensure_topo(vp, obj_idx);
    DC_Topo *t = vp->objects[obj_idx].topo;
    return t ? t->face_count : 0;
}

int
dc_gl_viewport_get_edge_count(DC_GlViewport *vp, int obj_idx)
{
    if (!vp || obj_idx < 0 || obj_idx >= vp->obj_count) return 0;
    ensure_topo(vp, obj_idx);
    DC_Topo *t = vp->objects[obj_idx].topo;
    return t ? t->edge_count : 0;
}

void
dc_gl_viewport_toggle_wireframe(DC_GlViewport *vp)
{
    if (!vp) return;
    vp->show_wireframe = !vp->show_wireframe;
    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

int
dc_gl_viewport_get_wireframe(DC_GlViewport *vp)
{
    return vp ? vp->show_wireframe : 0;
}
