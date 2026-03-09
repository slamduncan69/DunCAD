#define _POSIX_C_SOURCE 200809L
#include "ui/scad_preview.h"
#include "ui/code_editor.h"
#include "gl/gl_viewport.h"
#include "scad/scad_runner.h"
#include "scad/scad_splitter.h"
#include "ui/transform_panel.h"
#include "core/log.h"

/* Trinity Site — native OpenSCAD interpreter (replaces OpenSCAD subprocess) */
#include "ts_eval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct DC_ScadPreview {
    GtkWidget      *container;      /* GtkBox(V): toolbar + GL viewport */
    DC_GlViewport  *viewport;       /* real-time 3D viewport */
    GtkWidget      *status_label;
    GtkWidget      *progress_bar;   /* pulsing progress during render */
    GtkWidget      *render_btn;
    GtkWidget      *reset_btn;
    GtkWidget      *ortho_btn;
    GtkWidget      *grid_btn;
    GtkWidget      *axes_btn;
    DC_CodeEditor  *code_ed;        /* borrowed */
    char           *tmp_scad;       /* temp .scad path (owned) */
    char           *tmp_stl;        /* temp .stl path (owned) */
    int             rendering;
    guint           pulse_id;       /* timer source for progress pulse */

    /* Multi-object render state */
    DC_ScadStatements *stmts;       /* current split (owned) */
    int             render_idx;     /* which statement we're rendering next */
    int             render_total;   /* total statements to render */
    int             render_ok;      /* how many succeeded */

    /* Preamble: includes, variables, $fn/$fa/$fs — prepended to each object */
    char           *preamble;       /* owned, collected from non-geometry stmts */

    /* Transform panel overlay */
    DC_TransformPanel *transform;
};

/* -------------------------------------------------------------------------
 * Progress bar pulse timer
 * ---------------------------------------------------------------------- */
static gboolean
pulse_progress(gpointer data)
{
    DC_ScadPreview *pv = data;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(pv->progress_bar));
    return G_SOURCE_CONTINUE;
}

static void
progress_start(DC_ScadPreview *pv)
{
    gtk_widget_set_visible(pv->progress_bar, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pv->progress_bar), 0.0);
    pv->pulse_id = g_timeout_add(80, pulse_progress, pv);
}

static void
progress_stop(DC_ScadPreview *pv)
{
    if (pv->pulse_id) {
        g_source_remove(pv->pulse_id);
        pv->pulse_id = 0;
    }
    gtk_widget_set_visible(pv->progress_bar, FALSE);
}

/* -------------------------------------------------------------------------
 * Async render via worker thread (keeps GTK main loop responsive)
 * ---------------------------------------------------------------------- */
typedef struct {
    ts_mesh         mesh;
    ts_parse_error  err;
    double          elapsed;
    char           *stl_path;  /* where the STL was written (owned) */
} RenderResult;

/* Worker thread: interpret source and write STL (no GTK calls!) */
static void
render_thread_func(GTask *task, gpointer source_obj,
                   gpointer task_data, GCancellable *cancel)
{
    (void)source_obj;
    (void)cancel;
    char *source = task_data;

    RenderResult *res = calloc(1, sizeof(*res));
    if (!res) return;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    memset(&res->err, 0, sizeof(res->err));
    res->mesh = ts_interpret(source, &res->err);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    res->elapsed = (double)(t1.tv_sec - t0.tv_sec)
                 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;

    /* Write STL in the worker thread too */
    if (res->err.msg[0] == '\0' && res->mesh.tri_count > 0) {
        res->stl_path = strdup("/tmp/duncad-preview.stl");
        if (ts_mesh_write_stl(&res->mesh, res->stl_path) != 0) {
            free(res->stl_path);
            res->stl_path = NULL;
        }
    }

    g_task_return_pointer(task, res, NULL);
}

/* Main thread callback: load result into GL viewport */
static void
render_done_cb(GObject *source_obj, GAsyncResult *result, gpointer userdata)
{
    (void)source_obj;
    DC_ScadPreview *pv = userdata;
    RenderResult *res = g_task_propagate_pointer(G_TASK(result), NULL);

    pv->rendering = 0;
    progress_stop(pv);
    gtk_widget_set_sensitive(pv->render_btn, TRUE);

    if (!res) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Render failed");
        return;
    }

    if (res->err.msg[0]) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Parse error (line %d): %.200s",
                 res->err.line, res->err.msg);
        gtk_label_set_text(GTK_LABEL(pv->status_label), msg);
        ts_mesh_free(&res->mesh);
        free(res->stl_path);
        free(res);
        return;
    }

    if (res->mesh.tri_count == 0) {
        gtk_label_set_text(GTK_LABEL(pv->status_label),
                           "No geometry produced");
        ts_mesh_free(&res->mesh);
        free(res->stl_path);
        free(res);
        return;
    }

    if (res->stl_path) {
        int rc = dc_gl_viewport_load_stl(pv->viewport, res->stl_path);
        if (rc == 0) {
            char status[128];
            snprintf(status, sizeof(status),
                     "Trinity Site: %d tris in %.3fs — drag to orbit",
                     res->mesh.tri_count, res->elapsed);
            gtk_label_set_text(GTK_LABEL(pv->status_label), status);
        } else {
            gtk_label_set_text(GTK_LABEL(pv->status_label),
                               "Render OK but STL load failed");
        }
        unlink(res->stl_path);
        free(res->stl_path);
    } else {
        gtk_label_set_text(GTK_LABEL(pv->status_label),
                           "Failed to write temp STL");
    }

    ts_mesh_free(&res->mesh);
    free(res);
}

static void
do_render(DC_ScadPreview *pv)
{
    if (!pv->code_ed) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "No code editor connected");
        return;
    }
    if (pv->rendering) return;

    char *text = dc_code_editor_get_text(pv->code_ed);
    if (!text || !*text) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Nothing to render");
        free(text);
        return;
    }

    /* Clear viewport and launch async render */
    dc_gl_viewport_clear_objects(pv->viewport);
    dc_gl_viewport_clear_mesh(pv->viewport);

    pv->rendering = 1;
    gtk_widget_set_sensitive(pv->render_btn, FALSE);
    gtk_label_set_text(GTK_LABEL(pv->status_label),
                       "Rendering with Trinity Site...");
    progress_start(pv);

    GTask *task = g_task_new(NULL, NULL, render_done_cb, pv);
    g_task_set_task_data(task, text, free);
    g_task_run_in_thread(task, render_thread_func);
    g_object_unref(task);
}

/* -------------------------------------------------------------------------
 * Button callbacks
 * ---------------------------------------------------------------------- */
static void on_render_clicked(GtkButton *b, gpointer d) { (void)b; do_render(d); }

static void on_reset_clicked(GtkButton *b, gpointer d)
{ (void)b; dc_gl_viewport_reset_camera(((DC_ScadPreview*)d)->viewport); }

static void on_ortho_clicked(GtkButton *b, gpointer d)
{ (void)b; dc_gl_viewport_toggle_ortho(((DC_ScadPreview*)d)->viewport); }

static void on_grid_clicked(GtkButton *b, gpointer d)
{ (void)b; dc_gl_viewport_toggle_grid(((DC_ScadPreview*)d)->viewport); }

static void on_axes_clicked(GtkButton *b, gpointer d)
{ (void)b; dc_gl_viewport_toggle_axes(((DC_ScadPreview*)d)->viewport); }

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

DC_ScadPreview *
dc_scad_preview_new(void)
{
    DC_ScadPreview *pv = calloc(1, sizeof(*pv));
    if (!pv) return NULL;

    pv->tmp_scad = strdup("/tmp/duncad-preview.scad");
    pv->tmp_stl  = strdup("/tmp/duncad-preview.stl");

    /* GL viewport */
    pv->viewport = dc_gl_viewport_new();
    if (!pv->viewport) {
        free(pv->tmp_scad);
        free(pv->tmp_stl);
        free(pv);
        return NULL;
    }

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 2);
    gtk_widget_set_margin_bottom(toolbar, 2);

    pv->render_btn = gtk_button_new_with_label("Render");
    gtk_widget_set_focusable(pv->render_btn, FALSE);
    g_signal_connect(pv->render_btn, "clicked", G_CALLBACK(on_render_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->render_btn);

    pv->reset_btn = gtk_button_new_with_label("Reset");
    gtk_widget_set_focusable(pv->reset_btn, FALSE);
    g_signal_connect(pv->reset_btn, "clicked", G_CALLBACK(on_reset_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->reset_btn);

    pv->ortho_btn = gtk_button_new_with_label("Ortho");
    gtk_widget_set_focusable(pv->ortho_btn, FALSE);
    g_signal_connect(pv->ortho_btn, "clicked", G_CALLBACK(on_ortho_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->ortho_btn);

    pv->grid_btn = gtk_button_new_with_label("Grid");
    gtk_widget_set_focusable(pv->grid_btn, FALSE);
    g_signal_connect(pv->grid_btn, "clicked", G_CALLBACK(on_grid_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->grid_btn);

    pv->axes_btn = gtk_button_new_with_label("Axes");
    gtk_widget_set_focusable(pv->axes_btn, FALSE);
    g_signal_connect(pv->axes_btn, "clicked", G_CALLBACK(on_axes_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->axes_btn);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(toolbar), sep);

    pv->status_label = gtk_label_new("Ready — click Render");
    gtk_label_set_xalign(GTK_LABEL(pv->status_label), 0.0f);
    gtk_widget_set_hexpand(pv->status_label, TRUE);
    gtk_widget_set_opacity(pv->status_label, 0.7);
    gtk_box_append(GTK_BOX(toolbar), pv->status_label);

    /* Progress bar (hidden until render starts) */
    pv->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_size_request(pv->progress_bar, 120, -1);
    gtk_widget_set_valign(pv->progress_bar, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(pv->progress_bar, FALSE);
    gtk_box_append(GTK_BOX(toolbar), pv->progress_bar);

    /* Transform panel (overlay on viewport) */
    pv->transform = dc_transform_panel_new();

    /* Overlay: GL viewport as main child, transform panel floats on top */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), dc_gl_viewport_widget(pv->viewport));
    if (pv->transform) {
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                                dc_transform_panel_widget(pv->transform));
    }
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_widget_set_hexpand(overlay, TRUE);

    /* Container */
    pv->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(pv->container), toolbar);
    gtk_box_append(GTK_BOX(pv->container), overlay);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "scad preview created");
    return pv;
}

void
dc_scad_preview_free(DC_ScadPreview *pv)
{
    if (!pv) return;
    progress_stop(pv);
    dc_transform_panel_free(pv->transform);
    dc_gl_viewport_free(pv->viewport);
    dc_scad_stmts_free(pv->stmts);
    free(pv->preamble);
    free(pv->tmp_scad);
    free(pv->tmp_stl);
    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "scad preview freed");
    free(pv);
}

GtkWidget *
dc_scad_preview_widget(DC_ScadPreview *pv)
{
    return pv ? pv->container : NULL;
}

void
dc_scad_preview_set_code_editor(DC_ScadPreview *pv, DC_CodeEditor *ed)
{
    if (!pv) return;
    pv->code_ed = ed;
    dc_transform_panel_set_code_editor(pv->transform, ed);
}

void
dc_scad_preview_render(DC_ScadPreview *pv)
{
    if (pv) do_render(pv);
}

DC_GlViewport *
dc_scad_preview_get_viewport(DC_ScadPreview *pv)
{
    return pv ? pv->viewport : NULL;
}

DC_TransformPanel *
dc_scad_preview_get_transform(DC_ScadPreview *pv)
{
    return pv ? pv->transform : NULL;
}
