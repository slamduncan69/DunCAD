#define _POSIX_C_SOURCE 200809L
#include "ui/scad_preview.h"
#include "ui/code_editor.h"
#include "gl/gl_viewport.h"
#include "scad/scad_runner.h"
#include "scad/scad_splitter.h"
#include "ui/transform_panel.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct DC_ScadPreview {
    GtkWidget      *container;      /* GtkBox(V): toolbar + GL viewport */
    DC_GlViewport  *viewport;       /* real-time 3D viewport */
    GtkWidget      *status_label;
    GtkWidget      *render_btn;
    GtkWidget      *reset_btn;
    GtkWidget      *ortho_btn;
    GtkWidget      *grid_btn;
    GtkWidget      *axes_btn;
    DC_CodeEditor  *code_ed;        /* borrowed */
    char           *tmp_scad;       /* temp .scad path (owned) */
    char           *tmp_stl;        /* temp .stl path (owned) */
    int             rendering;

    /* Multi-object render state */
    DC_ScadStatements *stmts;       /* current split (owned) */
    int             render_idx;     /* which statement we're rendering next */
    int             render_total;   /* total statements to render */
    int             render_ok;      /* how many succeeded */

    /* Transform panel overlay */
    DC_TransformPanel *transform;
};

/* -------------------------------------------------------------------------
 * Multi-object render pipeline
 *
 * 1. Split SCAD source into top-level statements
 * 2. Render each statement as a separate STL
 * 3. Load each STL as a separate GL object with line range
 * 4. If splitting fails or yields 1 statement, fall back to single-STL
 * ---------------------------------------------------------------------- */
static void render_next_statement(DC_ScadPreview *pv);

static void
on_stmt_render_done(DC_ScadResult *result, void *userdata)
{
    DC_ScadPreview *pv = userdata;
    int idx = pv->render_idx - 1; /* render_idx was incremented before launch */

    if (result && result->exit_code == 0 && pv->stmts && idx < pv->stmts->count) {
        /* Load this statement's STL as an object */
        char stl_path[256];
        snprintf(stl_path, sizeof(stl_path), "/tmp/duncad-obj-%d.stl", idx);

        DC_ScadStatement *s = &pv->stmts->stmts[idx];
        int rc = dc_gl_viewport_add_object(pv->viewport, stl_path,
                                            s->line_start, s->line_end);
        if (rc >= 0) pv->render_ok++;

        unlink(stl_path);
    }

    if (result) dc_scad_result_free(result);

    /* Continue with next statement */
    render_next_statement(pv);
}

static void
render_next_statement(DC_ScadPreview *pv)
{
    if (!pv->stmts || pv->render_idx >= pv->stmts->count) {
        /* All done */
        pv->rendering = 0;
        gtk_widget_set_sensitive(pv->render_btn, TRUE);

        char status[128];
        snprintf(status, sizeof(status),
                 "Rendered %d/%d objects — click to select",
                 pv->render_ok, pv->render_total);
        gtk_label_set_text(GTK_LABEL(pv->status_label), status);

        /* Reset camera to fit all objects */
        dc_gl_viewport_reset_camera(pv->viewport);
        return;
    }

    int idx = pv->render_idx++;
    DC_ScadStatement *s = &pv->stmts->stmts[idx];

    /* Write this statement to a temp SCAD file */
    char scad_path[256];
    snprintf(scad_path, sizeof(scad_path), "/tmp/duncad-obj-%d.scad", idx);
    char stl_path[256];
    snprintf(stl_path, sizeof(stl_path), "/tmp/duncad-obj-%d.stl", idx);

    FILE *f = fopen(scad_path, "w");
    if (!f) {
        render_next_statement(pv);
        return;
    }
    fputs(s->text, f);
    fclose(f);

    char msg[128];
    snprintf(msg, sizeof(msg), "Rendering object %d/%d...", idx + 1, pv->render_total);
    gtk_label_set_text(GTK_LABEL(pv->status_label), msg);

    dc_scad_run_export(scad_path, stl_path, on_stmt_render_done, pv);
}

/* Fallback: single-STL render (when split yields 0 or 1 statement) */
static void
on_single_render_done(DC_ScadResult *result, void *userdata)
{
    DC_ScadPreview *pv = userdata;
    pv->rendering = 0;
    gtk_widget_set_sensitive(pv->render_btn, TRUE);

    if (!result) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Render failed: no result");
        return;
    }

    if (result->exit_code == 0) {
        int rc = dc_gl_viewport_load_stl(pv->viewport, pv->tmp_stl);
        if (rc == 0) {
            char status[128];
            snprintf(status, sizeof(status), "Rendered in %.1fs — drag to orbit, scroll to zoom",
                     result->elapsed_secs);
            gtk_label_set_text(GTK_LABEL(pv->status_label), status);
        } else {
            gtk_label_set_text(GTK_LABEL(pv->status_label), "Render OK but STL load failed");
        }
    } else {
        const char *err = result->stderr_text ? result->stderr_text : "unknown error";
        char msg[256];
        snprintf(msg, sizeof(msg), "Error (exit %d): %.200s", result->exit_code, err);
        char *nl = strchr(msg, '\n');
        if (nl) *nl = '\0';
        gtk_label_set_text(GTK_LABEL(pv->status_label), msg);
    }

    dc_scad_result_free(result);
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

    /* Split into top-level statements */
    dc_scad_stmts_free(pv->stmts);
    pv->stmts = dc_scad_split(text);

    if (pv->stmts && pv->stmts->count > 1) {
        /* Multi-object render: each statement gets its own STL */
        dc_gl_viewport_clear_objects(pv->viewport);
        dc_gl_viewport_clear_mesh(pv->viewport);

        pv->rendering = 1;
        pv->render_idx = 0;
        pv->render_total = pv->stmts->count;
        pv->render_ok = 0;
        gtk_widget_set_sensitive(pv->render_btn, FALSE);

        free(text);
        render_next_statement(pv);
    } else {
        /* Single statement or parse failed — use legacy single-STL path */
        dc_gl_viewport_clear_objects(pv->viewport);

        FILE *f = fopen(pv->tmp_scad, "w");
        if (!f) {
            gtk_label_set_text(GTK_LABEL(pv->status_label), "Cannot write temp file");
            free(text);
            return;
        }
        fputs(text, f);
        fclose(f);
        free(text);

        pv->rendering = 1;
        gtk_widget_set_sensitive(pv->render_btn, FALSE);
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Rendering STL...");

        dc_scad_run_export(pv->tmp_scad, pv->tmp_stl, on_single_render_done, pv);
    }
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
    dc_transform_panel_free(pv->transform);
    dc_gl_viewport_free(pv->viewport);
    dc_scad_stmts_free(pv->stmts);
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
