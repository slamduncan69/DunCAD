#define _POSIX_C_SOURCE 200809L
#include "ui/scad_preview.h"
#include "ui/code_editor.h"
#include "gl/gl_viewport.h"
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
#include <ctype.h>

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
    int             rendering;
    guint           progress_id;    /* timer source for progress polling */

    /* Transform panel overlay */
    DC_TransformPanel *transform;

    /* Progressive render state */
    unsigned int    render_gen;     /* generation counter — incremented each render */
    volatile int    hq_cancel;      /* cooperative cancel flag for HQ task */
    int             hq_running;     /* is HQ task in flight? */
    ts_progress     progress;       /* shared progress — worker writes, UI reads */
    int             camera_fitted;  /* 1 after first fit — skip auto-fit on re-renders */
    int             render_pending; /* 1 = re-render queued (dropped while busy) */
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
 * Preamble detection — identifies non-geometry statements
 *
 * Preamble statements are prepended to each geometry statement when
 * rendering per-object. They include: include/use directives,
 * module/function definitions, and variable assignments.
 * ---------------------------------------------------------------------- */
static int
is_preamble(const char *stmt)
{
    const char *p = stmt;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return 1; /* empty = skip */

    /* Skip leading single-line comments so "// comment\nmodule foo()" is
     * correctly identified as a module definition, not geometry. */
    while (p[0] == '/' && p[1] == '/') {
        while (*p && *p != '\n') p++;
        while (*p && isspace((unsigned char)*p)) p++;
    }
    if (!*p) return 1; /* comment-only statement */

    /* include/use directives */
    if (strncmp(p, "include", 7) == 0 &&
        !isalnum((unsigned char)p[7]) && p[7] != '_')
        return 1;
    if (strncmp(p, "use", 3) == 0 &&
        !isalnum((unsigned char)p[3]) && p[3] != '_')
        return 1;

    /* module/function definitions */
    if (strncmp(p, "module", 6) == 0 &&
        !isalnum((unsigned char)p[6]) && p[6] != '_')
        return 1;
    if (strncmp(p, "function", 8) == 0 &&
        !isalnum((unsigned char)p[8]) && p[8] != '_')
        return 1;

    /* Variable assignment: identifier = expr; (but not ==) */
    const char *eq = strchr(p, '=');
    if (eq && eq > p && eq[1] != '=') {
        const char *q = p;
        while (q < eq && (isalnum((unsigned char)*q) || *q == '_' || *q == '$'))
            q++;
        while (q < eq && isspace((unsigned char)*q)) q++;
        if (q == eq) return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Multi-object progressive render pipeline
 *
 * Pass 1 (preview): $fn=12, force_quality — instant low-poly per object
 * Pass 2 (HQ):      $fn=100 — full quality per object, cancellable
 *
 * Each pass splits the SCAD source into top-level statements, identifies
 * preamble (variables, includes, module defs), and renders each geometry
 * statement separately with preamble prepended. Results are loaded as
 * separate GL objects for color-ID picking.
 * ---------------------------------------------------------------------- */

typedef struct {
    char *stl_path;     /* owned temp file path */
    int   line_start;   /* 1-based source line range */
    int   line_end;
} ObjSlot;

typedef struct {
    ObjSlot        *objs;
    int             obj_count;
    ts_parse_error  err;        /* first error encountered */
    double          elapsed;
    unsigned int    gen;
    int             is_hq;
    int             total_tris;
} RenderResult;

static void
render_result_free(RenderResult *res)
{
    if (!res) return;
    for (int i = 0; i < res->obj_count; i++)
        free(res->objs[i].stl_path);
    free(res->objs);
    free(res);
}

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

/* Worker thread: split source, render each geometry statement, write STLs */
static void
render_thread_func(GTask *task, gpointer source_obj,
                   gpointer task_data, GCancellable *cancellable)
{
    (void)source_obj;
    (void)cancellable;
    RenderTaskData *td = task_data;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Split source into top-level statements */
    DC_ScadStatements *stmts = dc_scad_split(td->source);
    if (!stmts || stmts->count == 0) {
        RenderResult *res = calloc(1, sizeof(*res));
        res->gen = td->gen;
        res->is_hq = td->is_hq;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        res->elapsed = (double)(t1.tv_sec - t0.tv_sec)
                     + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        g_task_return_pointer(task, res, NULL);
        dc_scad_stmts_free(stmts);
        return;
    }

    /* Build preamble: concatenate all preamble statements */
    size_t preamble_cap = 1;
    for (int i = 0; i < stmts->count; i++) {
        if (is_preamble(stmts->stmts[i].text))
            preamble_cap += strlen(stmts->stmts[i].text) + 1;
    }

    char *preamble = malloc(preamble_cap);
    preamble[0] = '\0';
    for (int i = 0; i < stmts->count; i++) {
        if (is_preamble(stmts->stmts[i].text)) {
            strcat(preamble, stmts->stmts[i].text);
            strcat(preamble, "\n");
        }
    }

    /* Count geometry statements */
    int geo_count = 0;
    for (int i = 0; i < stmts->count; i++) {
        if (!is_preamble(stmts->stmts[i].text))
            geo_count++;
    }

    if (geo_count == 0) {
        RenderResult *res = calloc(1, sizeof(*res));
        res->gen = td->gen;
        res->is_hq = td->is_hq;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        res->elapsed = (double)(t1.tv_sec - t0.tv_sec)
                     + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        g_task_return_pointer(task, res, NULL);
        free(preamble);
        dc_scad_stmts_free(stmts);
        return;
    }

    /* Set up progress: one tick per geometry statement */
    if (td->progress) {
        td->progress->done = 0;
        td->progress->total = geo_count;
    }

    /* Allocate result */
    RenderResult *res = calloc(1, sizeof(*res));
    res->gen = td->gen;
    res->is_hq = td->is_hq;
    res->objs = calloc((size_t)geo_count, sizeof(ObjSlot));

    /* Set up interpret options */
    ts_interpret_opts opts = {0};
    if (td->is_hq) {
        opts.fn_override = 100;
        opts.fa_override = 1;
        opts.fs_override = 0.4;
        opts.force_quality = 0;
        opts.cancel = td->cancel;
    } else {
        opts.fn_override = 12;
        opts.fa_override = 12;
        opts.fs_override = 2.0;
        opts.force_quality = 1;
        opts.cancel = NULL;
    }

    int obj_idx = 0;
    for (int i = 0; i < stmts->count; i++) {
        if (is_preamble(stmts->stmts[i].text))
            continue;

        /* Check cancellation */
        if (td->cancel && *td->cancel)
            break;

        /* Build source: preamble + this statement */
        size_t src_len = strlen(preamble) + strlen(stmts->stmts[i].text) + 2;
        char *src = malloc(src_len);
        snprintf(src, src_len, "%s%s", preamble, stmts->stmts[i].text);

        ts_parse_error err = {0};
        opts.progress = NULL; /* track at statement level, not internal */
        ts_mesh mesh = ts_interpret_ex(src, &err, &opts);
        free(src);

        /* Store first error */
        if (err.msg[0] && res->err.msg[0] == '\0')
            res->err = err;

        if (mesh.tri_count > 0) {
            char path[128];
            snprintf(path, sizeof(path), "/tmp/duncad-obj-%02d-%s.stl",
                     obj_idx, td->is_hq ? "hq" : "lq");
            if (ts_mesh_write_stl(&mesh, path) == 0) {
                res->objs[obj_idx].stl_path = strdup(path);
                res->objs[obj_idx].line_start = stmts->stmts[i].line_start;
                res->objs[obj_idx].line_end = stmts->stmts[i].line_end;
                res->total_tris += mesh.tri_count;
                obj_idx++;
            }
        }
        ts_mesh_free(&mesh);

        if (td->progress)
            td->progress->done++;
    }
    res->obj_count = obj_idx;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    res->elapsed = (double)(t1.tv_sec - t0.tv_sec)
                 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;

    /* If cancelled, clean up */
    if (td->cancel && *td->cancel) {
        render_result_free(res);
        res = NULL;
    }

    free(preamble);
    dc_scad_stmts_free(stmts);

    g_task_return_pointer(task, res, NULL);
}

/* Forward declarations */
static void launch_hq_render(DC_ScadPreview *pv, const char *source);
static void do_render(DC_ScadPreview *pv);

/* Main thread callback for BOTH preview and HQ results */
static void
render_done_cb(GObject *source_obj, GAsyncResult *result, gpointer userdata)
{
    (void)source_obj;
    DC_ScadPreview *pv = userdata;
    RenderResult *res = g_task_propagate_pointer(G_TASK(result), NULL);

    /* NULL = cancelled */
    if (!res) {
        if (pv->hq_running)
            pv->hq_running = 0;
        return;
    }

    /* Stale generation — discard */
    if (res->gen != pv->render_gen) {
        render_result_free(res);
        return;
    }

    if (res->is_hq) {
        pv->hq_running = 0;
        progress_stop(pv);
    } else {
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
        render_result_free(res);
        return;
    }

    if (res->obj_count == 0) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "No geometry produced");
        if (!res->is_hq) progress_stop(pv);
        render_result_free(res);
        return;
    }

    /* Load objects into viewport */
    dc_gl_viewport_clear_objects(pv->viewport);
    dc_gl_viewport_clear_mesh(pv->viewport);

    int loaded = 0;
    for (int i = 0; i < res->obj_count; i++) {
        if (res->objs[i].stl_path) {
            int idx = dc_gl_viewport_add_object(pv->viewport,
                res->objs[i].stl_path,
                res->objs[i].line_start,
                res->objs[i].line_end);
            if (idx >= 0) loaded++;
            unlink(res->objs[i].stl_path);
        }
    }

    if (loaded > 0) {
        /* Fit camera only on first preview render (not on re-renders from movement) */
        if (!res->is_hq && !pv->camera_fitted) {
            DC_LOG_INFO_APP("fit_all_objects: fitting camera (first render), gen=%u", pv->render_gen);
            dc_gl_viewport_fit_all_objects(pv->viewport);
            pv->camera_fitted = 1;
        } else {
            DC_LOG_INFO_APP("fit_all_objects: SKIPPED (is_hq=%d fitted=%d)",
                            res->is_hq, pv->camera_fitted);
        }

        char status[192];
        if (res->is_hq) {
            snprintf(status, sizeof(status),
                     "HQ: %d objs, %d tris in %.3fs",
                     loaded, res->total_tris, res->elapsed);
        } else {
            snprintf(status, sizeof(status),
                     "Preview: %d objs, %d tris in %.3fs — refining...",
                     loaded, res->total_tris, res->elapsed);
        }
        gtk_label_set_text(GTK_LABEL(pv->status_label), status);
    } else {
        gtk_label_set_text(GTK_LABEL(pv->status_label),
                           "No geometry loaded");
    }

    /* If preview just completed, check for queued re-render or launch HQ */
    if (!res->is_hq) {
        if (pv->render_pending) {
            /* A render was requested while we were busy — re-render with
             * fresh code (user may have moved objects since we started) */
            pv->render_pending = 0;
            render_result_free(res);
            dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                   "executing queued re-render");
            do_render(pv);
            return;
        }
        if (res->obj_count > 0) {
            RenderTaskData *td = g_task_get_task_data(G_TASK(result));
            if (td && td->source) {
                launch_hq_render(pv, td->source);
            }
        }
    }

    render_result_free(res);
}

static void
launch_hq_render(DC_ScadPreview *pv, const char *source)
{
    /* Cancel any existing HQ render.
     * Don't reset immediately — old thread needs time to see the flag.
     * Reset only after setting up the new task, right before spawning it.
     * Old HQ results are discarded by generation counter anyway. */
    pv->hq_cancel = 1;

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

    /* Reset cancel flag just before launch — old HQ had time to see =1 */
    pv->hq_cancel = 0;

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
    if (pv->rendering) {
        pv->render_pending = 1;
        dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,
               "render queued (busy), will re-render on completion");
        return;
    }

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

    /* Don't clear objects here — they stay visible until results arrive.
     * clear_objects is called in the result callback before loading new meshes. */

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
static void on_render_clicked(GtkButton *b, gpointer d)
{
    (void)b;
    DC_ScadPreview *pv = d;
    pv->camera_fitted = 0; /* explicit render → refit camera */
    do_render(pv);
}

static void on_reset_clicked(GtkButton *b, gpointer d)
{
    (void)b;
    DC_ScadPreview *pv = d;
    dc_gl_viewport_fit_all_objects(pv->viewport);
    pv->camera_fitted = 1; /* still fitted, but user explicitly asked */
}

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

    /* GL viewport */
    pv->viewport = dc_gl_viewport_new();
    if (!pv->viewport) {
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

void
dc_scad_preview_render_refit(DC_ScadPreview *pv)
{
    if (!pv) return;
    pv->camera_fitted = 0;
    do_render(pv);
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

const char *
dc_scad_preview_get_status(DC_ScadPreview *pv)
{
    if (!pv || !pv->status_label) return "";
    return gtk_label_get_text(GTK_LABEL(pv->status_label));
}

int
dc_scad_preview_is_rendering(DC_ScadPreview *pv)
{
    if (!pv) return 0;
    return pv->rendering || pv->hq_running;
}
