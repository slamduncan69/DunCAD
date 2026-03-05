#define _POSIX_C_SOURCE 200809L
#include "ui/scad_preview.h"
#include "ui/code_editor.h"
#include "scad/scad_runner.h"
#include "core/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct DC_ScadPreview {
    GtkWidget      *container;      /* GtkBox(V): toolbar + image area */
    GtkWidget      *picture;        /* GtkPicture showing the rendered PNG */
    GtkWidget      *status_label;   /* status text in toolbar */
    GtkWidget      *render_btn;
    GtkWidget      *reset_btn;      /* reset camera to viewall */
    DC_CodeEditor  *code_ed;        /* borrowed — source of SCAD text */
    char           *tmp_scad;       /* temp .scad path (owned) */
    char           *tmp_png;        /* temp .png path (owned) */

    /* Camera state (OpenSCAD convention) */
    DC_ScadCamera   cam;
    int             cam_active;     /* 0 = use viewall, 1 = explicit camera */

    /* Drag state */
    double          drag_start_x;
    double          drag_start_y;
    double          drag_start_rx, drag_start_ry;   /* orbit drag */
    double          drag_start_tx, drag_start_ty;   /* pan drag */
    int             dragging;       /* 1 = orbit, 2 = pan */

    /* Debounce timer for auto-render after interaction */
    guint           render_timer;
    int             rendering;      /* guard against overlapping renders */
};

/* -------------------------------------------------------------------------
 * Status update helper
 * ---------------------------------------------------------------------- */
static void
update_camera_status(DC_ScadPreview *pv)
{
    char buf[256];
    if (!pv->cam_active) {
        snprintf(buf, sizeof(buf), "Camera: auto (viewall)");
    } else {
        snprintf(buf, sizeof(buf),
                 "rot(%.0f,%.0f,%.0f) dist=%.0f",
                 pv->cam.rx, pv->cam.ry, pv->cam.rz, pv->cam.dist);
    }
    gtk_label_set_text(GTK_LABEL(pv->status_label), buf);
}

/* -------------------------------------------------------------------------
 * Render logic
 * ---------------------------------------------------------------------- */
static void
on_render_done(DC_ScadResult *result, void *userdata)
{
    DC_ScadPreview *pv = userdata;
    pv->rendering = 0;
    gtk_widget_set_sensitive(pv->render_btn, TRUE);

    if (!result) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Render failed: no result");
        return;
    }

    if (result->exit_code == 0) {
        GdkTexture *tex = gdk_texture_new_from_filename(pv->tmp_png, NULL);
        if (tex) {
            gtk_picture_set_paintable(GTK_PICTURE(pv->picture),
                                       GDK_PAINTABLE(tex));
            g_object_unref(tex);
        }
        char status[128];
        snprintf(status, sizeof(status), "Rendered in %.1fs", result->elapsed_secs);
        gtk_label_set_text(GTK_LABEL(pv->status_label), status);
    } else {
        const char *err = result->stderr_text ? result->stderr_text : "unknown error";
        char msg[256];
        snprintf(msg, sizeof(msg), "Error (exit %d): %.200s",
                 result->exit_code, err);
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
    gtk_label_set_text(GTK_LABEL(pv->status_label), "Rendering...");

    int w = gtk_widget_get_width(pv->picture);
    int h = gtk_widget_get_height(pv->picture);
    if (w < 200) w = 800;
    if (h < 200) h = 600;

    if (pv->cam_active) {
        dc_scad_render_png_camera(pv->tmp_scad, pv->tmp_png, w, h,
                                   &pv->cam, on_render_done, pv);
    } else {
        dc_scad_render_png(pv->tmp_scad, pv->tmp_png, w, h,
                           on_render_done, pv);
    }
}

/* Debounced render — fires after camera interaction settles */
static gboolean
on_render_timeout(gpointer data)
{
    DC_ScadPreview *pv = data;
    pv->render_timer = 0;
    do_render(pv);
    return G_SOURCE_REMOVE;
}

static void
schedule_render(DC_ScadPreview *pv)
{
    if (pv->render_timer)
        g_source_remove(pv->render_timer);
    pv->render_timer = g_timeout_add(300, on_render_timeout, pv);
}

/* -------------------------------------------------------------------------
 * Mouse gesture handlers
 *
 * OpenSCAD viewport convention:
 *   Left drag:         orbit (rotate rx/ry)
 *   Right drag:        pan (translate tx/ty)
 *   Scroll:            zoom (distance)
 *   Middle drag:       pan (alternative)
 * ---------------------------------------------------------------------- */

/* Ensure camera has sensible defaults when first activated */
static void
activate_camera(DC_ScadPreview *pv)
{
    if (!pv->cam_active) {
        pv->cam_active = 1;
        /* OpenSCAD default-ish view */
        pv->cam.rx = 55.0;
        pv->cam.ry = 0.0;
        pv->cam.rz = 25.0;
        pv->cam.tx = 0.0;
        pv->cam.ty = 0.0;
        pv->cam.tz = 0.0;
        pv->cam.dist = 140.0;
    }
}

static void
on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
    DC_ScadPreview *pv = data;
    activate_camera(pv);

    GdkEvent *event = gtk_event_controller_get_current_event(
        GTK_EVENT_CONTROLLER(gesture));
    GdkModifierType mods = gdk_event_get_modifier_state(event);
    guint button = gtk_gesture_single_get_current_button(
        GTK_GESTURE_SINGLE(gesture));

    pv->drag_start_x = x;
    pv->drag_start_y = y;

    if (button == 3 || button == 2 || (mods & GDK_SHIFT_MASK)) {
        /* Right/middle drag or shift+drag = pan */
        pv->dragging = 2;
        pv->drag_start_tx = pv->cam.tx;
        pv->drag_start_ty = pv->cam.ty;
    } else {
        /* Left drag = orbit */
        pv->dragging = 1;
        pv->drag_start_rx = pv->cam.rx;
        pv->drag_start_ry = pv->cam.ry;
    }
}

static void
on_drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
    (void)gesture;
    DC_ScadPreview *pv = data;

    if (pv->dragging == 1) {
        /* Orbit: dx → rotate around Z, dy → rotate around X */
        pv->cam.rz = pv->drag_start_ry + dx * 0.5;
        pv->cam.rx = pv->drag_start_rx - dy * 0.5;
        /* Clamp rx to avoid gimbal weirdness */
        if (pv->cam.rx > 180.0) pv->cam.rx = 180.0;
        if (pv->cam.rx < 0.0) pv->cam.rx = 0.0;
    } else if (pv->dragging == 2) {
        /* Pan: scale by distance for reasonable speed */
        double scale = pv->cam.dist * 0.005;
        pv->cam.tx = pv->drag_start_tx - dx * scale;
        pv->cam.ty = pv->drag_start_ty + dy * scale;
    }

    update_camera_status(pv);
}

static void
on_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
    (void)gesture; (void)dx; (void)dy;
    DC_ScadPreview *pv = data;
    pv->dragging = 0;
    schedule_render(pv);
}

static gboolean
on_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data)
{
    (void)ctrl; (void)dx;
    DC_ScadPreview *pv = data;
    activate_camera(pv);

    /* Zoom: scroll up = closer (smaller dist), scroll down = farther */
    double factor = pow(1.1, dy);
    pv->cam.dist *= factor;
    if (pv->cam.dist < 1.0) pv->cam.dist = 1.0;
    if (pv->cam.dist > 100000.0) pv->cam.dist = 100000.0;

    update_camera_status(pv);
    schedule_render(pv);
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Button callbacks
 * ---------------------------------------------------------------------- */
static void
on_render_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    do_render(data);
}

static void
on_reset_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    DC_ScadPreview *pv = data;
    pv->cam_active = 0;
    memset(&pv->cam, 0, sizeof(pv->cam));
    update_camera_status(pv);
    do_render(pv);
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

    pv->reset_btn = gtk_button_new_with_label("Reset View");
    gtk_widget_set_focusable(pv->reset_btn, FALSE);
    g_signal_connect(pv->reset_btn, "clicked",
                     G_CALLBACK(on_reset_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->reset_btn);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(toolbar), sep);

    pv->status_label = gtk_label_new("Camera: auto (viewall)");
    gtk_label_set_xalign(GTK_LABEL(pv->status_label), 0.0f);
    gtk_widget_set_hexpand(pv->status_label, TRUE);
    gtk_widget_set_opacity(pv->status_label, 0.7);
    gtk_box_append(GTK_BOX(toolbar), pv->status_label);

    /* Image area */
    pv->picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(pv->picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_vexpand(pv->picture, TRUE);
    gtk_widget_set_hexpand(pv->picture, TRUE);

    /* Dark background frame */
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(frame), pv->picture);
    gtk_widget_add_css_class(frame, "view");
    gtk_widget_set_vexpand(frame, TRUE);

    /* Mouse gestures on the frame (not picture, so we capture the full area) */
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 0); /* any button */
    g_signal_connect(drag, "drag-begin",  G_CALLBACK(on_drag_begin),  pv);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), pv);
    g_signal_connect(drag, "drag-end",    G_CALLBACK(on_drag_end),    pv);
    gtk_widget_add_controller(frame, GTK_EVENT_CONTROLLER(drag));

    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), pv);
    gtk_widget_add_controller(frame, scroll);

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
    if (pv->render_timer)
        g_source_remove(pv->render_timer);
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
