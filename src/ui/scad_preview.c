#define _POSIX_C_SOURCE 200809L
#include "ui/scad_preview.h"
#include "ui/code_editor.h"
#include "scad/scad_runner.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct DC_ScadPreview {
    GtkWidget      *container;    /* GtkBox(V): toolbar + scrolled picture */
    GtkWidget      *picture;      /* GtkPicture showing the rendered PNG */
    GtkWidget      *status_label; /* "Ready" / "Rendering..." / error text */
    GtkWidget      *render_btn;
    DC_CodeEditor  *code_ed;      /* borrowed — source of SCAD text */
    char           *tmp_scad;     /* temp .scad path (owned) */
    char           *tmp_png;      /* temp .png path (owned) */
};

/* -------------------------------------------------------------------------
 * Render callback (runs in background via GSubprocess, result on main loop)
 * ---------------------------------------------------------------------- */
static void
on_render_done(DC_ScadResult *result, void *userdata)
{
    DC_ScadPreview *pv = userdata;
    gtk_widget_set_sensitive(pv->render_btn, TRUE);

    if (!result) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Render failed: no result");
        return;
    }

    if (result->exit_code == 0) {
        /* Load the rendered PNG */
        GdkTexture *tex = gdk_texture_new_from_filename(pv->tmp_png, NULL);
        if (tex) {
            gtk_picture_set_paintable(GTK_PICTURE(pv->picture),
                                       GDK_PAINTABLE(tex));
            g_object_unref(tex);
        }
        char status[128];
        snprintf(status, sizeof(status), "Rendered in %.1fs", result->elapsed_secs);
        gtk_label_set_text(GTK_LABEL(pv->status_label), status);
        dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
               "scad_preview: render ok (%.1fs)", result->elapsed_secs);
    } else {
        /* Show first line of stderr */
        const char *err = result->stderr_text ? result->stderr_text : "unknown error";
        char msg[256];
        snprintf(msg, sizeof(msg), "Error (exit %d): %.200s",
                 result->exit_code, err);
        /* Truncate at first newline */
        char *nl = strchr(msg, '\n');
        if (nl) *nl = '\0';
        gtk_label_set_text(GTK_LABEL(pv->status_label), msg);
        dc_log(DC_LOG_WARN, DC_LOG_EVENT_APP,
               "scad_preview: render failed (exit %d)", result->exit_code);
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

    char *text = dc_code_editor_get_text(pv->code_ed);
    if (!text || !*text) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Nothing to render");
        free(text);
        return;
    }

    /* Write SCAD to temp file */
    FILE *f = fopen(pv->tmp_scad, "w");
    if (!f) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Cannot write temp file");
        free(text);
        return;
    }
    fputs(text, f);
    fclose(f);
    free(text);

    gtk_widget_set_sensitive(pv->render_btn, FALSE);
    gtk_label_set_text(GTK_LABEL(pv->status_label), "Rendering...");

    /* Get viewport size for image dimensions */
    int w = gtk_widget_get_width(pv->picture);
    int h = gtk_widget_get_height(pv->picture);
    if (w < 200) w = 800;
    if (h < 200) h = 600;

    dc_scad_render_png(pv->tmp_scad, pv->tmp_png, w, h,
                       on_render_done, pv);
}

/* -------------------------------------------------------------------------
 * Button callback
 * ---------------------------------------------------------------------- */
static void
on_render_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    do_render(data);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

DC_ScadPreview *
dc_scad_preview_new(void)
{
    DC_ScadPreview *pv = calloc(1, sizeof(*pv));
    if (!pv) return NULL;

    pv->tmp_scad = strdup("/tmp/duncad-preview.scad");
    pv->tmp_png  = strdup("/tmp/duncad-preview.png");

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 2);
    gtk_widget_set_margin_bottom(toolbar, 2);

    pv->render_btn = gtk_button_new_with_label("Render");
    gtk_widget_set_focusable(pv->render_btn, FALSE);
    g_signal_connect(pv->render_btn, "clicked",
                     G_CALLBACK(on_render_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->render_btn);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(toolbar), sep);

    pv->status_label = gtk_label_new("Ready — click Render or Ctrl+R");
    gtk_label_set_xalign(GTK_LABEL(pv->status_label), 0.0f);
    gtk_widget_set_hexpand(pv->status_label, TRUE);
    gtk_widget_set_opacity(pv->status_label, 0.7);
    gtk_box_append(GTK_BOX(toolbar), pv->status_label);

    /* Image area */
    pv->picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(pv->picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_vexpand(pv->picture, TRUE);
    gtk_widget_set_hexpand(pv->picture, TRUE);

    /* Dark background for the preview area */
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(frame), pv->picture);
    gtk_widget_add_css_class(frame, "view");

    /* Container */
    pv->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(pv->container), toolbar);
    gtk_box_append(GTK_BOX(pv->container), frame);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "scad preview created");
    return pv;
}

void
dc_scad_preview_free(DC_ScadPreview *pv)
{
    if (!pv) return;
    free(pv->tmp_scad);
    free(pv->tmp_png);
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
    if (pv) pv->code_ed = ed;
}

void
dc_scad_preview_render(DC_ScadPreview *pv)
{
    if (pv) do_render(pv);
}
