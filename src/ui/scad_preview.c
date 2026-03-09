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
    GtkWidget      *progress_bar;   /* determinate progress during render */
    GtkWidget      *render_btn;
    GtkWidget      *reset_btn;
    GtkWidget      *ortho_btn;
    GtkWidget      *grid_btn;
    GtkWidget      *axes_btn;
    DC_CodeEditor  *code_ed;        /* borrowed */
    char           *tmp_scad;       /* temp .scad path (owned) */
    char           *tmp_stl;        /* temp .stl path (owned) */
    int             rendering;
    guint           progress_id;    /* timer source for progress polling */

    /* Multi-object render state (legacy, kept for preamble) */
    DC_ScadStatements *stmts;       /* current split (owned) */
    int             render_idx;
    int             render_total;
    int             render_ok;
    char           *preamble;       /* owned */

    /* Transform panel overlay */
    DC_TransformPanel *transform;

    /* Progressive render state */
    unsigned int    render_gen;     /* generation counter — incremented each render */
    volatile int    hq_cancel;      /* cooperative cancel flag for HQ task */
    int             hq_running;     /* is HQ task in flight? */
    ts_progress     progress;       /* shared progress — worker writes, UI reads */
};

/* -------------------------------------------------------------------------
 * Progress bar — polls shared ts_progress from worker thread
 * ---------------------------------------------------------------------- */
static gboolean
poll_progress(gpointer data)
{
    DC_ScadPreview *pv = data;
    int done  = pv->progress.done;
    int total = pv->progress.total;

    if (total > 0) {
        double frac = (double)done / (double)total;
        if (frac > 1.0) frac = 1.0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pv->progress_bar), frac);
    } else {
        /* Total not yet known — pulse */
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(pv->progress_bar));
    }
    return G_SOURCE_CONTINUE;
}

static void
progress_start(DC_ScadPreview *pv)
{
    pv->progress.done  = 0;
    pv->progress.total = 0;
    gtk_widget_set_visible(pv->progress_bar, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pv->progress_bar), 0.0);
    pv->progress_id = g_timeout_add(100, poll_progress, pv);
}

static void
progress_stop(DC_ScadPreview *pv)
{
    if (pv->progress_id) {
        g_source_remove(pv->progress_id);
        pv->progress_id = 0;
    }
    gtk_widget_set_visible(pv->progress_bar, FALSE);
}

/* -------------------------------------------------------------------------
 * Two-pass progressive render pipeline
 *
 * Pass 1 (preview): $fn=12, $fa=12, $fs=2.0  — instant low-poly
 * Pass 2 (HQ):      $fn=100, $fa=1, $fs=0.4  — full quality, cancellable
 *
 * Generation counter prevents stale results from overwriting newer ones.
 * ---------------------------------------------------------------------- */

typedef struct {
    ts_mesh         mesh;
    ts_parse_error  err;
    double          elapsed;
    char           *stl_path;       /* where the STL was written (owned) */
    unsigned int    gen;            /* generation this result belongs to */
    int             is_hq;          /* 0=preview, 1=high-quality */
} RenderResult;

typedef struct {
    char           *source;         /* owned copy of SCAD source */
    unsigned int    gen;            /* generation counter */
    int             is_hq;          /* 0=preview, 1=high-quality */
    volatile int   *cancel;         /* points to pv->hq_cancel (HQ only) */
    ts_progress    *progress;       /* shared progress tracker */
} RenderTaskData;

static void
render_task_data_free(gpointer p)
{
    RenderTaskData *td = p;
    free(td->source);
    free(td);
}

/* Worker thread: interpret source and write STL (no GTK calls!) */
static void
render_thread_func(GTask *task, gpointer source_obj,
                   gpointer task_data, GCancellable *cancellable)
{
    (void)source_obj;
    (void)cancellable;
    RenderTaskData *td = task_data;

    RenderResult *res = calloc(1, sizeof(*res));
    if (!res) return;

    res->gen   = td->gen;
    res->is_hq = td->is_hq;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    memset(&res->err, 0, sizeof(res->err));

    ts_interpret_opts opts = {0};
    if (td->is_hq) {
        /* Full quality — 3D print parameters */
        opts.fn_override = 100;
        opts.fa_override = 1;
        opts.fs_override = 0.4;
        opts.force_quality = 0;  /* respect source $fn if set */
        opts.cancel      = td->cancel;
    } else {
        /* Preview — fast low-poly, FORCE overrides source $fn */
        opts.fn_override = 12;
        opts.fa_override = 12;
        opts.fs_override = 2.0;
        opts.force_quality = 1;  /* ignore source $fn/$fa/$fs */
        opts.cancel      = NULL; /* preview is fast, no cancel needed */
    }
    opts.progress = td->progress;
    res->mesh = ts_interpret_ex(td->source, &res->err, &opts);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    res->elapsed = (double)(t1.tv_sec - t0.tv_sec)
                 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;

    /* Check if cancelled (HQ only) */
    if (td->cancel && *td->cancel) {
        ts_mesh_free(&res->mesh);
        free(res);
        g_task_return_pointer(task, NULL, NULL);
        return;
    }

    /* Write STL in the worker thread */
    if (res->err.msg[0] == '\0' && res->mesh.tri_count > 0) {
        const char *path = td->is_hq ? "/tmp/duncad-preview-hq.stl"
                                      : "/tmp/duncad-preview-lq.stl";
        res->stl_path = strdup(path);
        if (ts_mesh_write_stl(&res->mesh, res->stl_path) != 0) {
            free(res->stl_path);
            res->stl_path = NULL;
        }
    }

    g_task_return_pointer(task, res, NULL);
}

/* Forward declaration */
static void launch_hq_render(DC_ScadPreview *pv, const char *source);

/* Main thread callback for BOTH preview and HQ results */
static void
render_done_cb(GObject *source_obj, GAsyncResult *result, gpointer userdata)
{
    (void)source_obj;
    DC_ScadPreview *pv = userdata;
    RenderResult *res = g_task_propagate_pointer(G_TASK(result), NULL);

    /* NULL = cancelled */
    if (!res) {
        if (pv->hq_running) {
            pv->hq_running = 0;
        }
        return;
    }

    /* Stale generation — discard */
    if (res->gen != pv->render_gen) {
        ts_mesh_free(&res->mesh);
        free(res->stl_path);
        free(res);
        return;
    }

    if (res->is_hq) {
        /* HQ pass completed */
        pv->hq_running = 0;
        progress_stop(pv);
    } else {
        /* Preview pass completed — re-enable render button */
        pv->rendering = 0;
        gtk_widget_set_sensitive(pv->render_btn, TRUE);
    }

    /* Handle errors */
    if (res->err.msg[0]) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Parse error (line %d): %.200s",
                 res->err.line, res->err.msg);
        gtk_label_set_text(GTK_LABEL(pv->status_label), msg);
        if (!res->is_hq) progress_stop(pv);
        ts_mesh_free(&res->mesh);
        free(res->stl_path);
        free(res);
        return;
    }

    if (res->mesh.tri_count == 0) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "No geometry produced");
        if (!res->is_hq) progress_stop(pv);
        ts_mesh_free(&res->mesh);
        free(res->stl_path);
        free(res);
        return;
    }

    /* Load mesh into viewport */
    if (res->stl_path) {
        dc_gl_viewport_clear_objects(pv->viewport);
        dc_gl_viewport_clear_mesh(pv->viewport);
        int rc = dc_gl_viewport_load_stl(pv->viewport, res->stl_path);
        if (rc == 0) {
            char status[192];
            if (res->is_hq) {
                snprintf(status, sizeof(status),
                         "HQ: %d tris in %.3fs",
                         res->mesh.tri_count, res->elapsed);
            } else {
                snprintf(status, sizeof(status),
                         "Preview: %d tris in %.3fs — refining...",
                         res->mesh.tri_count, res->elapsed);
            }
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

    /* If preview just completed, launch HQ in background */
    if (!res->is_hq && res->mesh.tri_count > 0) {
        RenderTaskData *td = g_task_get_task_data(G_TASK(result));
        if (td && td->source) {
            launch_hq_render(pv, td->source);
        }
    }

    ts_mesh_free(&res->mesh);
    free(res);
}

static void
launch_hq_render(DC_ScadPreview *pv, const char *source)
{
    /* Cancel any existing HQ render */
    pv->hq_cancel = 1;
    pv->hq_cancel = 0;  /* reset for new HQ task */

    /* Reset progress for HQ pass */
    pv->progress.done  = 0;
    pv->progress.total = 0;

    RenderTaskData *td = calloc(1, sizeof(*td));
    td->source   = strdup(source);
    td->gen      = pv->render_gen;
    td->is_hq    = 1;
    td->cancel   = &pv->hq_cancel;
    td->progress = &pv->progress;

    pv->hq_running = 1;

    GTask *task = g_task_new(NULL, NULL, render_done_cb, pv);
    g_task_set_task_data(task, td, render_task_data_free);
    g_task_run_in_thread(task, render_thread_func);
    g_object_unref(task);

    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "HQ render launched (gen %u)",
           pv->render_gen);
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

    /* Cancel any in-flight HQ render */
    pv->hq_cancel = 1;

    /* Increment generation — all older results will be discarded */
    pv->render_gen++;

    /* Clear viewport and launch preview render */
    dc_gl_viewport_clear_objects(pv->viewport);
    dc_gl_viewport_clear_mesh(pv->viewport);

    pv->rendering = 1;
    gtk_widget_set_sensitive(pv->render_btn, FALSE);
    gtk_label_set_text(GTK_LABEL(pv->status_label),
                       "Preview rendering...");
    progress_start(pv);

    RenderTaskData *td = calloc(1, sizeof(*td));
    td->source   = text;  /* takes ownership */
    td->gen      = pv->render_gen;
    td->is_hq    = 0;
    td->cancel   = NULL;
    td->progress = &pv->progress;

    GTask *task = g_task_new(NULL, NULL, render_done_cb, pv);
    g_task_set_task_data(task, td, render_task_data_free);
    g_task_run_in_thread(task, render_thread_func);
    g_object_unref(task);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "progressive render started (gen %u)",
           pv->render_gen);
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
    pv->hq_cancel = 1;  /* signal any in-flight HQ to stop */
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
