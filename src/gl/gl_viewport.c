#define _POSIX_C_SOURCE 200809L
#include "gl/gl_viewport.h"
#include "gl/stl_loader.h"
#include "core/log.h"

#include <epoxy/gl.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
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

    /* Mesh */
    DC_StlMesh  *mesh;
    GLuint       mesh_vao;
    GLuint       mesh_vbo;
    int          mesh_uploaded;

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
    int          gl_ready;

    /* Drag state */
    double       drag_x, drag_y;
    float        drag_theta, drag_phi;
    float        drag_center[3];    /* cam_center at drag start */
    int          dragging;  /* 0=none, 1=orbit, 2=pan */
};

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

    if (!vp->mesh_prog || !vp->line_prog) {
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

    /* --- Draw mesh --- */
    if (vp->mesh && vp->mesh_uploaded && vp->mesh_vao) {
        glUseProgram(vp->mesh_prog);

        float model[16];
        mat4_identity(model);

        float mvp[16];
        mat4_mul(mvp, vp_mat, model);

        glUniformMatrix4fv(glGetUniformLocation(vp->mesh_prog, "uMVP"), 1, GL_FALSE, mvp);
        glUniformMatrix4fv(glGetUniformLocation(vp->mesh_prog, "uModel"), 1, GL_FALSE, model);

        /* Normal matrix = transpose(inverse(upper-left 3x3 of model)) — identity for now */
        float nmat[9] = {1,0,0, 0,1,0, 0,0,1};
        glUniformMatrix3fv(glGetUniformLocation(vp->mesh_prog, "uNormalMat"), 1, GL_FALSE, nmat);

        float light_dir[3] = {0.5f, 0.8f, 0.3f};
        vec3_normalize(light_dir);
        glUniform3fv(glGetUniformLocation(vp->mesh_prog, "uLightDir"), 1, light_dir);
        glUniform3fv(glGetUniformLocation(vp->mesh_prog, "uViewPos"), 1, eye);

        float color[3] = {0.6f, 0.75f, 0.9f};  /* OpenSCAD-ish blue */
        glUniform3fv(glGetUniformLocation(vp->mesh_prog, "uColor"), 1, color);

        glBindVertexArray(vp->mesh_vao);
        glDrawArrays(GL_TRIANGLES, 0, vp->mesh->num_vertices);
    }

    glBindVertexArray(0);
    glUseProgram(0);

    return TRUE;
}

/* =========================================================================
 * Mouse gesture handlers
 * ========================================================================= */

static void
on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
    DC_GlViewport *vp = data;

    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
    GdkModifierType mods = gdk_event_get_modifier_state(event);
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    vp->drag_x = x;
    vp->drag_y = y;

    if (button == 3 || button == 2 || (mods & GDK_SHIFT_MASK)) {
        vp->dragging = 2;
        memcpy(vp->drag_center, vp->cam_center, sizeof(vp->drag_center));
    } else {
        vp->dragging = 1;
        vp->drag_theta = vp->cam_theta;
        vp->drag_phi = vp->cam_phi;
    }
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
        float theta = vp->cam_theta * (float)M_PI / 180.0f;
        float phi   = vp->cam_phi   * (float)M_PI / 180.0f;
        float scale = vp->cam_dist * 0.001f;

        /* Camera right vector (perpendicular to view in XZ plane) */
        float rx = cosf(theta);
        float rz = -sinf(theta);

        /* Camera up vector (cross of forward and right, simplified) */
        float ux = sinf(phi) * sinf(theta);
        float uy = cosf(phi);
        float uz = sinf(phi) * cosf(theta);

        float mx = (float)dx * scale;
        float my = (float)dy * scale;

        vp->cam_center[0] = vp->drag_center[0] - mx * rx + my * ux;
        vp->cam_center[1] = vp->drag_center[1]           + my * uy;
        vp->cam_center[2] = vp->drag_center[2] - mx * rz + my * uz;
    }

    gtk_gl_area_queue_render(GTK_GL_AREA(vp->gl_area));
}

static void
on_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
    (void)gesture; (void)dx; (void)dy;
    DC_GlViewport *vp = data;
    vp->dragging = 0;
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
